//Copyright 2020 Marchetti S. Alfredo I. All Rights Reserved.

#include "WeatherObjects/BaseWeatherObject.h"
#include "WeatherManager/WeatherManager.h"
#include "TimerManager.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "BaseWeatherProcess.h"

UBaseWeatherObject::UBaseWeatherObject() : Super()
{
}

UWorld* UBaseWeatherObject::GetWorld() const
{
	if (HasAllFlags(RF_ClassDefaultObject))
	{
		// If we are a CDO, we must return nullptr instead of calling Outer->GetWorld() to fool UObject::ImplementsGetWorld.
		return nullptr;
	}
	return GetOuter()->GetWorld();
}

int32 UBaseWeatherObject::GetFunctionCallspace(UFunction* Function, FFrame* Stack)
{
	if (HasAnyFlags(RF_ClassDefaultObject) || !IsSupportedForNetworking())
	{
		return FunctionCallspace::Local;
	}
	check(GetOuter() != nullptr);
	return GetOuter()->GetFunctionCallspace(Function, Stack);
}

bool UBaseWeatherObject::CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack)
{
	check(!HasAnyFlags(RF_ClassDefaultObject));
	check(GetOuter() != nullptr);

	AActor* Owner = CastChecked<AActor>(GetOuter());

	bool bProcessed = false;

	FWorldContext* const Context = GEngine->GetWorldContextFromWorld(GetWorld());
	if (Context != nullptr)
	{
		for (FNamedNetDriver& Driver : Context->ActiveNetDrivers)
		{
			if (Driver.NetDriver != nullptr && Driver.NetDriver->ShouldReplicateFunction(Owner, Function))
			{
				Driver.NetDriver->ProcessRemoteFunction(Owner, Function, Parameters, OutParms, Stack, this);
				bProcessed = true;
			}
		}
	}

	return bProcessed;
}

EWeather_State UBaseWeatherObject::GetState() const
{
	return State;
}

float UBaseWeatherObject::GetDuration_Implementation()
{
	if (InterpolationSpeed <= 0)
	{
		return Duration;
	}
	
	return Duration / InterpolationSpeed;
}

float UBaseWeatherObject::GetElapsedTime()
{
	if (InterpolationSpeed == 0)
	{		
		return GetElapsedStartTime();
	}

	if (!GetWorld())
	{
		return 0.f;
	}

	if (GetDuration() < 0)
	{
		return GetWorld()->GetTimeSeconds() - StartTimeSeconds;
	}

	return GetWorld()->GetTimerManager().GetTimerElapsed(DurationTimer) + GetElapsedStartTime();
}

float UBaseWeatherObject::GetElapsedStartTime()
{
	return GetDuration() * StartTimePercent;
}

float UBaseWeatherObject::GetElapsedTimePercent()
{	
	if (InterpolationSpeed == 0)
	{
		return StartTimePercent;
	}

	switch (GetState())
	{
	case EWeather_State::Inactive:
	{
		return 0.f;		
	}
	case EWeather_State::Active:
	{
		return FMath::Clamp(GetElapsedTime() / GetDuration(), 0.f, 1.f);
	}
	case EWeather_State::Completed:
	{
		return 1.f;
	}
	default:
		break;
	}

	return 0.f;
}

FGameplayTagContainer UBaseWeatherObject::GetTags() const
{
	return Tags;
}

float UBaseWeatherObject::GetStartTimePercent() const
{
	return StartTimePercent;
}

void UBaseWeatherObject::BindEvents()
{
	if (!WeatherManagerReference->OnProcessEvent.IsAlreadyBound(this, &UBaseWeatherObject::OnAnyProcessEvent))
	{
		WeatherManagerReference->OnProcessEvent.AddDynamic(this, &UBaseWeatherObject::OnAnyProcessEvent);
	}

	if (!WeatherManagerReference->OnEffectEvent.IsAlreadyBound(this, &UBaseWeatherObject::OnAnyEffectEvent))
	{
		WeatherManagerReference->OnEffectEvent.AddDynamic(this, &UBaseWeatherObject::OnAnyEffectEvent);
	}

	if (!WeatherManagerReference->OnGlobalWeatherTagsChangedEvent.IsAlreadyBound(this, &UBaseWeatherObject::OnGlobalWeatherTagsChanged))
	{
		WeatherManagerReference->OnGlobalWeatherTagsChangedEvent.AddDynamic(this, &UBaseWeatherObject::OnGlobalWeatherTagsChanged);
	}
}

void UBaseWeatherObject::Initialize_Implementation(float StartTimePercentIn, float InterpolationSpeedIn)
{
	if (!bIsEnabled)
	{
		return;
	}

	StartTimeSeconds = GetWorld()->GetTimeSeconds();
	StartTimePercent = FMath::Clamp(StartTimePercentIn, 0.f, 1.f);
	InterpolationSpeed = FMath::Max(0.0f, InterpolationSpeedIn);

	SetState(EWeather_State::Active);	

	StartObjectDuration();
	StartObjectTick();
}

void UBaseWeatherObject::TerminateObject_Implementation()
{
	SetState(EWeather_State::Inactive);
	ClearTimers();
}

void UBaseWeatherObject::SetState(EWeather_State StateIn)
{
	State = StateIn;
}

void UBaseWeatherObject::StartObjectDuration()
{
	if (GetDuration() < 0) //No end if is negative
	{
		return;
	}
	else if (GetDuration() == 0)
	{
		OnEnd();
	}
	else if (InterpolationSpeed > 0)
	{		
		if (GetWorld()->GetTimerManager().IsTimerActive(DurationTimer))
		{
			GetWorld()->GetTimerManager().ClearTimer(DurationTimer);
		}

		GetWorld()->GetTimerManager().SetTimer(DurationTimer, FTimerDelegate::CreateUObject(this, &UBaseWeatherObject::OnEnd), GetDuration() - GetElapsedStartTime(), false); //The duration is only for the remaining.
	}
}

void UBaseWeatherObject::StartObjectTick()
{
	if (TickTime < 0)
	{
		return;
	}

	if (GetDuration() == 0)
	{
		return; //This is when the object ends instantly, no time to tick.
	}

	StopObjectTick();
	GetWorld()->GetTimerManager().SetTimer(TickTimer, FTimerDelegate::CreateUObject(this, &UBaseWeatherObject::ObjectTick), TickTime, true);
	ObjectTick();
}

void UBaseWeatherObject::StopObjectTick()
{
	if (!GetWorld())
	{
		return;
	}

	if (GetWorld()->GetTimerManager().IsTimerActive(TickTimer))
	{
		GetWorld()->GetTimerManager().ClearTimer(TickTimer);
	}
}

void UBaseWeatherObject::ObjectTick_Implementation()
{
	// Add your logic here
}

void UBaseWeatherObject::OnAnyProcessEvent_Implementation(EWeather_DelegateDirection Event, UBaseWeatherProcess* Process)
{
	// Add your logic here
}

void UBaseWeatherObject::OnAnyEffectEvent_Implementation(EWeather_DelegateDirection Event, UBaseWeatherEffect* Effect)
{
	// Add your logic here
}

void UBaseWeatherObject::OnGlobalWeatherTagsChanged_Implementation(const FGameplayTagContainer& GlobalWeatherTags)
{
	// Add your logic here
}

void UBaseWeatherObject::OnEnd_Implementation()
{
	if (GetState() != EWeather_State::Active)
	{
		return;
	}

	ClearTimers();
	SetState(EWeather_State::Inactive);
}

void UBaseWeatherObject::ClearTimers()
{
	if (!GetWorld())
	{
		return;
	}

	if (GetWorld()->GetTimerManager().IsTimerActive(DurationTimer))
	{
		GetWorld()->GetTimerManager().ClearTimer(DurationTimer);
	}

	if (GetWorld()->GetTimerManager().IsTimerActive(TickTimer))
	{
		GetWorld()->GetTimerManager().ClearTimer(TickTimer);
	}
}
