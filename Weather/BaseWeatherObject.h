//Copyright 2020 Marchetti S. Alfredo I. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WeatherManager/WeatherTypes.h"
#include "GameplayTagContainer.h"
#include "BaseWeatherObject.generated.h"

class UWeatherManager;

/**
 * This is the basic object for weather processes, effects, tasks and conditions.
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, Meta = (DisplayName = "Base Weather Object"))
class WEATHEREDITOR_API UBaseWeatherObject : public UObject
{
	GENERATED_BODY()

public:
	UBaseWeatherObject();
	virtual class UWorld* GetWorld() const override;	
	virtual int32 GetFunctionCallspace(UFunction* Function, FFrame* Stack) override;
	virtual bool CallRemoteFunction(UFunction* Function, void* Parameters, FOutParmRec* OutParms, FFrame* Stack) override;	

	EWeather_State GetState() const;
	
	/**
	*	Overrideable function to get the duration in seconds.
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Base Weather Object")
	float GetDuration();

	float GetElapsedTime();
	float GetElapsedStartTime();
	float GetElapsedTimePercent();

	FGameplayTagContainer GetTags() const;
	float GetStartTimePercent() const;

	/*
	*	Call to start the Object.
	*	Before calling this method, ensure the varaibles in the object are set correctly via a preinitialization method.
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Base Weather Object")
	void Initialize(float StartTimePercentIn = 0.f, float InterpolationSpeedIn = 1.f);

	/**Overrideable function with the logic for reset of the object variables and stop all actions.*/
	UFUNCTION(BlueprintNativeEvent, Category = "Base Weather Object")
	void TerminateObject();

protected:
	UPROPERTY(BlueprintReadOnly, Category = "Base Weather Object")
	UWeatherManager* WeatherManagerReference;

	/**
	*	Enables or disables the entire object.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Base Weather Object")
	bool bIsEnabled = true;

	/* Tags for general purpose and identification*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Base Weather Object")
	FGameplayTagContainer Tags;

	/**
	*	Base duration.
	*	If set to 0, the will end instantly.
	*	If set to negative, the will never end.
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Time Management")
	float Duration = 2400.f;

	/**
	*	How fast the timer ticks.
	*	If <0 disables tick
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Time Management")
	float TickTime = -1.0f;

	UPROPERTY()
	float StartTimeSeconds = 0.f;

	UPROPERTY()
	FTimerHandle DurationTimer = FTimerHandle();

	UPROPERTY()
	FTimerHandle TickTimer = FTimerHandle();

	/**
	*	Specific static time to use for start.
	*	Must be a value between 0 and 1 and it represents a percentage in the curves to evaluate all parameters.
	*/
	UPROPERTY()
	float StartTimePercent = 0.f;

	UPROPERTY()
	float InterpolationSpeed = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Base Weather Object")
	EWeather_State State = EWeather_State::Inactive;

	void SetState(EWeather_State StateIn);
	void BindEvents();
	void StartObjectDuration();
	void StartObjectTick();
	void StopObjectTick();
	
	/**	Overrideable function with the task logic on duration timer tick.*/
	UFUNCTION(BlueprintNativeEvent, Category = "Base Weather Object")
	void ObjectTick();

	/**
	*	Overrideable function with the logic for when a process event happens
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Base Weather Object")
	void OnAnyProcessEvent(EWeather_DelegateDirection Event, UBaseWeatherProcess* Process);
	
	/**
	*	Overrideable function with the logic for when a effect event happens
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Base Weather Object")
	void OnAnyEffectEvent(EWeather_DelegateDirection Event, UBaseWeatherEffect* Effect);

	/**
	*	Overrideable function with the logic for when global weather tags change.
	*/
	UFUNCTION(BlueprintNativeEvent, Category = "Base Weather Object")
	void OnGlobalWeatherTagsChanged(const FGameplayTagContainer& GlobalWeatherTags);

	/**Overrideable function with the task logic when end.*/
	UFUNCTION(BlueprintNativeEvent, Category = "Base Weather Object")
	void OnEnd();

	void ClearTimers();
};
