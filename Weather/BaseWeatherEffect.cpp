//Copyright 2020 Marchetti S. Alfredo I. All Rights Reserved.

#include "WeatherObjects/BaseWeatherEffect.h"
#include "WeatherObjects/BaseWeatherProcess.h"
#include "WeatherObjects/BaseWeatherCondition.h"
#include "WeatherObjects/BaseWeatherTask.h"
#include "WeatherManager/WeatherManager.h"
#include "TimerManager.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "WeatherEditor.h"//log

UBaseWeatherEffect::UBaseWeatherEffect() : Super()
{
	Duration = -1.f; //Manually ends only by default.
	TickTime = -1.0f; // Do not tick, effects are triggered when other effect ends.
}

float UBaseWeatherEffect::GetDuration_Implementation()
{
	switch (DurationType)
	{
	case EWeather_DurationType::InSeconds:
	{
		return Duration;
	}		
	case EWeather_DurationType::InPercent:
	{	
		return WeatherProcessReference->GetDuration() * Duration;
	}	
	default:
		break;
	}	
	
	return Duration;
}

FRandomStream UBaseWeatherEffect::GetStream()
{
	return Stream;
}

UBaseWeatherProcess* UBaseWeatherEffect::GetParentProcess() const
{
	return WeatherProcessReference;
}

float UBaseWeatherEffect::GetEffectTriggerDelayInSeconds() const
{
	switch (TriggerDelayType)
	{
	case EWeather_DurationType::InSeconds:
	{
		return TriggerDelay;	
	}
	case EWeather_DurationType::InPercent:
	{
		const float DurationMulti = FMath::Clamp(TriggerDelay, 0.0f, 1.0f); //make sure multiplier is percentual			
		return WeatherProcessReference->GetDuration() * DurationMulti;
	}	
	default:
		break;
	}

	return TriggerDelay;
}

bool UBaseWeatherEffect::AreAllTasksCompleted() const
{
	bool bCompleted = true;
	for (UBaseWeatherTask* task : OnActivateTasks)
	{
		if (task->GetState() != EWeather_State::Completed)
		{
			bCompleted = false;
			break;
		}
	}
		
	return bCompleted;
}

void UBaseWeatherEffect::TryToActivate()
{
	if (!CanTrigger(EffectTrack)) 
	{
		EffectTrack.LastTriggerAttemptTime = GetWorld()->GetTimeSeconds();
		return;
	}

	if (!CanActivate(EffectTrack))
	{
		EffectTrack.LastTriggerAttemptTime = GetWorld()->GetTimeSeconds();
		return;
	}

	EffectTrack.TimesTriggered += 1;
	EffectTrack.LastTriggerAttemptTime = GetWorld()->GetTimeSeconds();

	if (DurationType == EWeather_DurationType::InPercent && Duration == 1.f && WeatherProcessReference->GetStartTimePercent() > 0.f)
	{
		UE_LOG(WeatherEditorLog, Log, TEXT("UBaseWeatherEffect::TryToActivate - Effect Initialized with advanced percent. %s. Interpolation Speed: %f"), *GetName(), InterpolationSpeed);
		Initialize(WeatherProcessReference->GetStartTimePercent(), InterpolationSpeed); //Init for effects that match duration of process must be scaled to the process start time.
	}
	else
	{
		Initialize();
	}	
}

void UBaseWeatherEffect::ClearTrack()
{
	EffectTrack = FWeather_TemporalEffect_Track();
}

bool UBaseWeatherEffect::CanTrigger(const FWeather_TemporalEffect_Track& EffectTrackIn)
{
	const float localTriggerDelay = GetEffectTriggerDelayInSeconds();

	if (localTriggerDelay <= 0)
	{
		return true;
	}

	return GetWorld()->GetTimeSeconds() - EffectTrackIn.LastEndTime >= localTriggerDelay;
}

bool UBaseWeatherEffect::CanActivate(const FWeather_TemporalEffect_Track& EffectTrackIn)
{
	if (!bIsEnabled)
	{
		return false;
	}

	if (GetState() != EWeather_State::Inactive)
	{
		return false;
	}

	return CanActivateBasedOnRoll() && CanActivateForTrack(EffectTrackIn) && CanActivateForConditions();
}

bool UBaseWeatherEffect::CanActivateBasedOnRoll() const
{
	const float roll = Stream.FRandRange(0.f, 100.f);
	return roll <= TriggerChance;
}

bool UBaseWeatherEffect::CanActivateForTrack(const FWeather_TemporalEffect_Track& EffectTrackIn)
{
	if (MaximunTriggerTimes > 0 && MaximunTriggerTimes >= EffectTrackIn.TimesTriggered)
	{
		return false;
	}

	if (InactiveTime > 0 && WeatherProcessReference->GetElapsedTime() - EffectTrackIn.LastEndTime < InactiveTime)
	{
		return false;
	}
			
	return true;
}

bool UBaseWeatherEffect::CanActivateForConditions_Implementation()
{
	if (OnActivateConditions.Num() <= 0)
	{
		return true;
	}

	bool bCanActivate = true;
	switch (ConditionMode)
	{
	case EWeather_TaskConditionMode::And:
	{
		bCanActivate = true;
		for (UBaseWeatherCondition* condition : OnActivateConditions)
		{
			if (condition && !condition->GetConditionValue(WeatherManagerReference, WeatherProcessReference, this))
			{
				bCanActivate = false;
				break;
			}
		}
		return bCanActivate;
	}	
	case EWeather_TaskConditionMode::Or:
	{
		bCanActivate = false;
		for (UBaseWeatherCondition* condition : OnActivateConditions)
		{
			if (condition && condition->GetConditionValue(WeatherManagerReference, WeatherProcessReference, this))
			{
				bCanActivate = true;
				break;
			}
		}
		return bCanActivate;
	}	
	default:
		break;
	}

	return true;
}

void UBaseWeatherEffect::PreInitialize_Implementation(UWeatherManager* WeatherManagerReferenceIn, UBaseWeatherProcess* WeatherProcessReferenceIn, int32 effectIndexIn)
{
	if (!bIsEnabled)
	{
		return;
	}

	if (!WeatherManagerReferenceIn || !WeatherProcessReferenceIn)
	{
		return;
	}

	WeatherManagerReference = WeatherManagerReferenceIn;
	WeatherProcessReference = WeatherProcessReferenceIn;
	
	Stream = FRandomStream(effectIndexIn);
	Stream.GenerateNewSeed();

	ClearTrack();

	BindEvents();
	UE_LOG(WeatherEditorLog, Log, TEXT("UBaseWeatherEffect::PreInitialize_Implementation - Effect Pre-Initialized %s. Index:%i."), *GetName(), effectIndexIn);
}

void UBaseWeatherEffect::Initialize_Implementation(float StartTimePercentIn, float InterpolationSpeedIn)
{
	if (!bIsEnabled)
	{
		UE_LOG(WeatherEditorLog, Log, TEXT("UBaseWeatherEffect::Initialize_Implementation - Effect %s. Init failed: Not enabled."), *GetName());
		return;
	}	

	if (!WeatherManagerReference || !WeatherProcessReference)
	{
		UE_LOG(WeatherEditorLog, Error, TEXT("UBaseWeatherEffect::Initialize_Implementation - Effect %s. Init failed: Invalid references."), *GetName());
		return;
	}
	
	InitializeAllTasks(StartTimePercentIn, InterpolationSpeedIn);
	Super::Initialize_Implementation(StartTimePercentIn, InterpolationSpeedIn);
	WeatherManagerReference->OnEffectEvent.Broadcast(EWeather_DelegateDirection::Start, this);
	UE_LOG(WeatherEditorLog, Log, TEXT("UBaseWeatherEffect::Initialize_Implementation - Effect Initialized %s."), *GetName());
}

void UBaseWeatherEffect::InitializeAllTasks(float StartTimePercentIn, float InterpolationSpeedIn)
{
	for (UBaseWeatherTask* task : OnActivateTasks)
	{
		if (task)
		{
			task->PreInitialize(WeatherManagerReference, this);
			task->Initialize(StartTimePercentIn, InterpolationSpeedIn);
		}
	}
}

void UBaseWeatherEffect::EndTask(UBaseWeatherTask* TaskIn)
{	
	if (AreAllTasksCompleted())
	{
		OnEnd(); // Force end of effect
	}
}

void UBaseWeatherEffect::OnEnd_Implementation()
{	
	if (GetState() != EWeather_State::Active)
	{
		return;
	}

	Super::OnEnd_Implementation();
	EndAllActiveTasks();		
	TerminateAllTasks();

	if (WeatherManagerReference && WeatherProcessReference)
	{
		EffectTrack.LastEndTime = GetWorld()->GetTimeSeconds();

		WeatherManagerReference->OnEffectEvent.Broadcast(EWeather_DelegateDirection::End, this);
		UE_LOG(WeatherEditorLog, Log, TEXT("UBaseWeatherEffect::EndEffect - Effect ended."));	
	}
}

void UBaseWeatherEffect::EndAllActiveTasks()
{
	for (UBaseWeatherTask* task : OnActivateTasks)
	{
		if (task && task->GetState() == EWeather_State::Active)
		{
			task->ForceEnd();
		}
	}
}

void UBaseWeatherEffect::TerminateAllTasks()
{
	for (UBaseWeatherTask* task : OnActivateTasks)
	{
		if (task)
		{
			task->TerminateObject();
		}
	}
}

void UBaseWeatherEffect::TerminateObject_Implementation()
{
	Super::TerminateObject_Implementation();

	ClearTrack();
	TerminateAllTasks();
}