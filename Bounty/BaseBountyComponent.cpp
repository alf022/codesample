//Copyright 2020 Marchetti S. Alfredo I. All Rights Reserved.


#include "Bounty/BaseBountyComponent.h"
#include "Bounty/BaseBountyObject.h"
#include "AbilitySystem/AbilitySystemComponents/BaseAbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/AttributeSets/PlayerRewardsAttributeSet.h"
#include "GameFramework/PlayerState.h"
#include "Bounty/BountyCollectionData.h"
#include "Bounty/BountyModeData.h"
#include "Bounty/BountyObjectData.h"
#include "AbilitySystem/GameplayData/GameplayDataSubsystem.h"
#include "AbilitySystem/GameplayData/AlwaysLoadedData.h"
#include "AbilitySystem/GlobalTags.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameModeBase.h"
#include "GameEvents/GameEventComponent.h"

UBaseBountyComponent::UBaseBountyComponent() : Super()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

void UBaseBountyComponent::BeginPlay()
{
	Super::BeginPlay();

	Stream = FRandomStream();
	Stream.GenerateNewSeed();	
}

void UBaseBountyComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	OnBountySelectedTick(DeltaTime);
}

int32 UBaseBountyComponent::GetLevel() const
{
	if (!BountyQueue.IsValidIndex(0))
	{
		return 1;
	}

	return BountyQueue[0].Level;
}

UBountyModeData* UBaseBountyComponent::GetBountyModeData() const
{
	if (!BountyQueue.IsValidIndex(0))
	{
		return nullptr;
	}
	return BountyQueue[0].Mode;
}

UBountyCollectionData* UBaseBountyComponent::GetBountyCollectionData() const
{
	if (!BountyQueue.IsValidIndex(0))
	{
		return nullptr;
	}
	return BountyQueue[0].Collection;
}

TArray<UBaseBountyObject*> UBaseBountyComponent::GetBountyObjects() const
{
	return BountyObjects;
}

UBaseBountyObject* UBaseBountyComponent::GetBountyObjectInSlot(int32 InSlot) const
{
	if (!BountyObjects.IsValidIndex(InSlot))
	{
		return nullptr;
	}

	return BountyObjects[InSlot];
}

bool UBaseBountyComponent::IsSelectingBounty() const
{
	return BountyObjects.Num() > 0 || IsSelectionEnding();
}

APawn* UBaseBountyComponent::GetPawnOwner()
{
	if (!IsValid(OwnerPawn))
	{
		const APlayerState* PS = Cast<APlayerState>(GetOwner());
		ensure(PS);
		OwnerPawn = PS->GetPawn();
	}

	return OwnerPawn;
}

UBaseAbilitySystemComponent* UBaseBountyComponent::GetOwnerASC()
{
	if (IsValid(ASC))
	{
		return ASC;
	}

	const APlayerState* PS = Cast<APlayerState>(GetOwner());
	ensure(PS);
		
	if (!PS->GetPawn())
	{
		return nullptr;
	}

	ASC = Cast<UBaseAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PS->GetPawn()));		
	return ASC;
}

UGameplayDataSubsystem* UBaseBountyComponent::GetGameplayDataSubsystem()
{
	if (GDS)
	{
		return GDS;
	}

	const APlayerState* PS = Cast<APlayerState>(GetOwner());
	ensure(PS);

	GDS = PS->GetGameInstance()->GetSubsystem<UGameplayDataSubsystem>();

	return GDS;
}

UGameEventComponent* UBaseBountyComponent::GetGameEventComponent() const
{
	AGameModeBase* GM = GetWorld()->GetAuthGameMode();
	if (GM)
	{
		return GM->FindComponentByClass<UGameEventComponent>();
	}
	return nullptr;
}

void UBaseBountyComponent::ClearBountyObjects()
{
	for (const auto& object : BountyObjects)
	{
		if (object)
		{
			object->MarkAsGarbage();
		}
	}

	BountyObjects.Empty();
	SelectedBounties.Empty();
}

bool UBaseBountyComponent::CanGenerateBounty() const
{
	return BountyObjects.IsEmpty();
}

void UBaseBountyComponent::GenerateBountyFromCollection(int32 InLevel, UBountyCollectionData* InBountyCollectionData)
{
	AddBountyToQueue(InLevel, InBountyCollectionData, nullptr);
	GenerateBountyFromQueue_Internal();
}

void UBaseBountyComponent::GenerateBountyFromMode(int32 InLevel, UBountyModeData* InBountyModeData)
{	
	AddBountyToQueue(InLevel, nullptr, InBountyModeData);
	GenerateBountyFromQueue_Internal();	
}

void UBaseBountyComponent::AddBountyToQueue(float InLevel, UBountyCollectionData* InCollection, UBountyModeData* InMode)
{
	FBountyQueueEntry Entry;
	Entry.Level = InLevel;
	Entry.Mode = InMode;
	Entry.Collection = InCollection;

	if (Entry.Collection && !Entry.Mode)
	{
		Entry.Mode = Entry.Collection->GetBountyModeForLevel(Stream, Entry.Level);
	}
		
	BountyQueue.Add(Entry);
}

void UBaseBountyComponent::RemoveCurrentQueueEntry()
{
	BountyQueue.RemoveAt(0);
}

bool UBaseBountyComponent::HasValidQueueEntry() const
{
	return BountyQueue.Num() > 0;
}

void UBaseBountyComponent::GenerateBountyFromQueue_Internal()
{
	if (!HasValidQueueEntry())
	{
		return;
	}

	if (!CanGenerateBounty())
	{
		return;
	}

	OnBountySelectedTimer.Invalidate();
	CurrentRerolls = 0;
	
	GenerateBountyFromMode_Internal();
}

void UBaseBountyComponent::GenerateBountyFromMode_Internal()
{
	ClearBountyObjects();

	CurrentSelections = 0; 

	TArray<UBountyObjectData*> selectedBountyObjectsData = GetBountyModeData()->GetBountyObjectsDataForLevel(Stream, GetLevel(), this);

	for (const auto& objectData : selectedBountyObjectsData)
	{
		UBaseBountyObject* Object = NewObject<UBaseBountyObject>(this, objectData->BountyObjectClass);
		BountyObjects.Add(Object);
		Object->CallOrRegister_OnBountyInitialized(FOnBountyInitialized::FDelegate::CreateUObject(this, &UBaseBountyComponent::OnBountyObjectInitialized));
		Object->PreInitializeBountyObject(objectData, this);
		Object->InitializeBountyObject(objectData, this);
	}
}

bool UBaseBountyComponent::AreAllBountyObjectsInitialized() const
{
	for (const auto& object : BountyObjects)
	{
		if (!object->IsBountyInitialized())
		{
			return false;
		}
	}

	return true;
}

void UBaseBountyComponent::OnBountyObjectInitialized()
{
	if (!AreAllBountyObjectsInitialized())
	{
		return;
	}

	// Order objects based on lenght, just for UI purposes. Would be better done in UI, but we dont care for now.
	if (BountyObjects.Num() > 3)
	{
		int32 Objects_Long = 0;
		for (const auto& object : BountyObjects)
		{
			const int32 DescLen = object->GetDescriptionLength();
			UE_LOG(LogTemp, Warning, TEXT("DESC LEN: %i for %s"), DescLen, *object->GetFName().ToString());
			if(DescLen >= 250)
			{
				Objects_Long++;			
			}
		}

		if(Objects_Long > 4)
		{
			Algo::Sort(BountyObjects, [this](UBaseBountyObject* A, UBaseBountyObject* B)
			{
				return A->GetDescriptionLength() >= B->GetDescriptionLength();
			});	
		}
		else
		{
			Algo::Sort(BountyObjects, [this](UBaseBountyObject* A, UBaseBountyObject* B)
			{
				if(A->GetDescriptionLength() <  250 && B->GetDescriptionLength() <  250)
				{
					return true;
				}
				
				return A->GetDescriptionLength() <= B->GetDescriptionLength();
			});	
		}
	}	

	UGameplayStatics::SetGamePaused(this, true);	
	OnNewBountyGenerated.Broadcast();
}

bool UBaseBountyComponent::HasGoldCost(float InCost, bool bIgnoreMultiplier)
{
	if (InCost <= 0)
	{
		return true;
	}

	if (!GetOwnerASC())
	{
		return false;
	}
	
	float Cost = 0.f;
	if (!bIgnoreMultiplier)
	{
		const float Multiplier = GetOwnerASC()->GetNumericAttribute(UPlayerRewardsAttributeSet::GetEssenceMultiplierAttribute());
		Cost = InCost * (1 + Multiplier / 100);
	}
	else
	{
		Cost = InCost;
	}

	return Cost <= GetOwnerASC()->GetNumericAttribute(UPlayerRewardsAttributeSet::GetEssenceAttribute());
}

void UBaseBountyComponent::RemoveGoldCost(float InCost, bool bIgnoreMultiplier)
{
	const FGameplayEffectSpecHandle Handle = GetOwnerASC()->MakeOutgoingSpec(
		GetGameplayDataSubsystem()->GetAlwaysLoadedData()->RemoveGoldGameplayEffect,
		1, GetOwnerASC()->MakeEffectContext()
		);
		
	Handle.Data.Get()->SetSetByCallerMagnitude(UGlobalTags::SetByCaller_Amount(), -InCost);

	if(bIgnoreMultiplier)
	{	
		Handle.Data.Get()->AddDynamicAssetTag(UGlobalTags::Effect_Essence_IgnoreMultiplier());
	}
	
	GetOwnerASC()->ApplyGameplayEffectSpecToSelf(*Handle.Data.Get());		
}

bool UBaseBountyComponent::IsBountySelected(int32 InSlot)
{
	return SelectedBounties.Contains(InSlot);
}

bool UBaseBountyComponent::CanSelectBounty(int32 InSlot)
{
	if (IsBountySelected(InSlot))
	{
		return false;
	}

	UBaseBountyObject* object = GetBountyObjectInSlot(InSlot);
	if (!object)
	{		
		return false;
	}

	if (!GetBountyModeData()->bEnforceBountyCostOnSelect)
	{
		return true;
	}

	return HasGoldCost(object->GetCost());
}

bool UBaseBountyComponent::SelectBounty(int InSlot)
{
	if (!CanSelectBounty(InSlot))
	{
		return false;
	}

	if (GetBountyModeData()->bEnforceBountyCostOnSelect)
	{
		RemoveGoldCost(GetBountyObjectInSlot(InSlot)->GetCost());
	}	

	CurrentSelections++;
	SelectedBounties.Add(InSlot);
	OnBountySelected.Broadcast(InSlot);

	GetBountyObjectInSlot(InSlot)->OnBountySelected();

	const bool IsMaxSelection = GetBountyModeData()->MaxSelections > 0 && CurrentSelections >= GetBountyModeData()->MaxSelections;
	if (GetBountyModeData()->MaxSelections != 1 && !IsMaxSelection)
	{
		ValidateBounties();
	}

	const bool IsLastBounty = SelectedBounties.Num() == BountyObjects.Num();
	if (IsLastBounty)
	{
		if (GetBountyModeData()->bCanBeRerolled && GetBountyModeData()->bAutoRerollWhenLastSelected)
		{
			RerollBounty_Internal();
			return true;
		}
	}
		
	if (IsLastBounty || IsMaxSelection)
	{
		EndBountySelection();
	}
	
	return true;
}

void UBaseBountyComponent::ValidateBounties()
{
	for (int32 i = 0; i < BountyObjects.Num(); i++)
	{
		if (!BountyObjects[i])
		{
			continue;
		}

		if (SelectedBounties.Contains(i))
		{
			continue;
		}

		if (!BountyObjects[i]->IsBountyValid())
		{
			SelectedBounties.Add(i); //we mark as selected, but is invalidated, i dont think there is a need to have a separated array for now
			OnBountyInvalidated.Broadcast(i);
		}
	}
}

bool UBaseBountyComponent::IsSelectionEnding() const
{
	return bIsTickTimerActive;
}

void UBaseBountyComponent::EndBountySelection()
{
	ClearBountyObjects();
	OnBountySelectionEnd.Broadcast();
	RemoveCurrentQueueEntry();
		
	StartBountySelectedTick();	
}

void UBaseBountyComponent::StartBountySelectedTick()
{
	AccumulatedDeltaOnBountySelected = 0;
	bIsTickTimerActive = true;
}

void UBaseBountyComponent::OnBountySelectedTick(float InDeltaTime)
{
	if(!bIsTickTimerActive)
	{
		return;
	}

	AccumulatedDeltaOnBountySelected += InDeltaTime;

	if (AccumulatedDeltaOnBountySelected >= OnBountySelectedDelay)
	{
		bIsTickTimerActive = false;

		if (HasValidQueueEntry())
		{
			GenerateBountyFromQueue_Internal();
		}
		else
		{
			UGameplayStatics::SetGamePaused(this, false);
		}		
	}
}

bool UBaseBountyComponent::CanSkipBounty()
{
	return GetBountyModeData()->bCanBeSkipped;
}

void UBaseBountyComponent::SkipBounty()
{
	if (!CanSkipBounty())
	{
		return;
	}

	OnBountySkipped.Broadcast();
	EndBountySelection();
}

float UBaseBountyComponent::GetRerollCost() const
{
	if(!GetBountyModeData())
	{
		return 0.f;
	}
	
	float BaseCost = 50.f;
	if(GetBountyModeData()->RerollCost.GetRichCurveConst())
	{
		BaseCost = GetBountyModeData()->RerollCost.GetRichCurveConst()->Eval(GetLevel());
	}
	
	return  BaseCost + GetBountyModeData()->RerollIncrementPerAplication * CurrentRerolls;
}

bool UBaseBountyComponent::CanRerollBounty()
{
	if (!GetBountyModeData()->bCanBeRerolled)
	{
		return false;
	}

	if (!HasGoldCost(GetRerollCost(), true))
	{
		return false;
	}

	if (GetBountyModeData()->MaxRerolls <= 0)
	{
		return true;
	}

	return CurrentRerolls <= GetBountyModeData()->MaxRerolls;
}

void UBaseBountyComponent::RerollBounty()
{	
	if (!CanRerollBounty())
	{
		return;
	}

	RemoveGoldCost(GetRerollCost(), true);

	RerollBounty_Internal();
}

void UBaseBountyComponent::RerollBounty_Internal()
{
	ClearBountyObjects();
	CurrentRerolls++;
	OnBountyRerolled.Broadcast();
	GenerateBountyFromMode_Internal();
}
