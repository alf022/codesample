#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "BaseBountyComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNoParamsDelegateBaseBountyComponent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBountySelected, int32, BountySlot);

class UBountyModeData;
class UBountyCollectionData;
class UBaseBountyObject;
class UAbilitySystemComponent;

USTRUCT()
struct FBountyQueueEntry
{
	GENERATED_BODY()

public:

	FBountyQueueEntry()
	{
		Level = 0;
		Collection = nullptr;
		Mode = nullptr;
	}

	UPROPERTY()
	float Level;

	UPROPERTY()
	UBountyCollectionData* Collection;

	UPROPERTY()
	UBountyModeData* Mode;
};


UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class CAMERAPLAY_API UBaseBountyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBaseBountyComponent();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, BlueprintAssignable, BlueprintCallable, Category = "Bounty")
	FNoParamsDelegateBaseBountyComponent OnNewBountyGenerated;
		
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, BlueprintAssignable, BlueprintCallable, Category = "Bounty")
	FNoParamsDelegateBaseBountyComponent OnBountySelectionEnd;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, BlueprintAssignable, BlueprintCallable, Category = "Bounty")
	FOnBountySelected OnBountySelected;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, BlueprintAssignable, BlueprintCallable, Category = "Bounty")
	FOnBountySelected OnBountyInvalidated;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, BlueprintAssignable, BlueprintCallable, Category = "Bounty")
	FNoParamsDelegateBaseBountyComponent OnBountySkipped;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, BlueprintAssignable, BlueprintCallable, Category = "Bounty")
	FNoParamsDelegateBaseBountyComponent OnBountyRerolled;

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	int32 GetLevel() const;
	
	UFUNCTION(BlueprintCallable, Category = "Bounty")
	UBountyModeData* GetBountyModeData() const;

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	UBountyCollectionData* GetBountyCollectionData() const;

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	TArray<UBaseBountyObject*> GetBountyObjects() const;

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	UBaseBountyObject* GetBountyObjectInSlot(int32 InSlot) const;

	APawn* GetPawnOwner();

	class UBaseAbilitySystemComponent* GetOwnerASC();

	class UGameplayDataSubsystem* GetGameplayDataSubsystem();

	class UGameEventComponent* GetGameEventComponent() const;

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	void GenerateBountyFromCollection(int32 InLevel, UBountyCollectionData* InBountyCollectionData);

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	void GenerateBountyFromMode(int32 InLevel, UBountyModeData* InBountyModeData);
	
	UFUNCTION(BlueprintCallable, Category = "Bounty")
	bool IsBountySelected(int32 InSlot);

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	bool IsSelectionEnding() const;

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	bool SelectBounty(int InSlot);

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	bool CanSkipBounty();

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	void SkipBounty();

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	float GetRerollCost() const;

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	bool CanRerollBounty();

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	void RerollBounty();

	UFUNCTION(BlueprintCallable, Category = "Bounty")
	bool IsSelectingBounty() const;

	bool AreAllBountyObjectsInitialized() const;
	
	UPROPERTY(Transient)
	FRandomStream Stream;

protected:	
	UPROPERTY(EditAnywhere, Category = "Bounty")
	float OnBountySelectedDelay = .5f;

	UPROPERTY(Transient)
	APawn* OwnerPawn = nullptr;

	UPROPERTY(Transient)
	class UBaseAbilitySystemComponent* ASC = nullptr;

	UPROPERTY(Transient)
	class UGameplayDataSubsystem* GDS = nullptr;

	UPROPERTY(Transient)
	TArray<FBountyQueueEntry> BountyQueue;

	UPROPERTY(Transient)
	int32 CurrentRerolls = 0;

	UPROPERTY(Transient)
	TArray<UBaseBountyObject*> BountyObjects;
	
	UPROPERTY(Transient)
	FTimerHandle OnBountySelectedTimer;

	UPROPERTY(Transient)
	float AccumulatedDeltaOnBountySelected = 0;

	UPROPERTY(Transient)
	bool bIsTickTimerActive = false;

	UPROPERTY(Transient)
	TArray <int32> SelectedBounties;

	UPROPERTY(Transient)
	int32 CurrentSelections = 0;

	void AddBountyToQueue(float InLevel, UBountyCollectionData* InCollection, UBountyModeData* InMode);
	void RemoveCurrentQueueEntry();
	bool HasValidQueueEntry() const;

	void GenerateBountyFromQueue_Internal();

	void GenerateBountyFromMode_Internal();

	void ValidateBounties();
	void EndBountySelection();
	void StartBountySelectedTick();
	void OnBountySelectedTick(float InDeltaTime);

	void ClearBountyObjects();
	bool CanGenerateBounty() const;


	void OnBountyObjectInitialized();

	bool HasGoldCost(float InCost, bool bIgnoreMultiplier = false);
	void RemoveGoldCost(float InCost, bool bIgnoreMultiplier = false);

	bool CanSelectBounty(int32 InSlot);

	void RerollBounty_Internal();
};
