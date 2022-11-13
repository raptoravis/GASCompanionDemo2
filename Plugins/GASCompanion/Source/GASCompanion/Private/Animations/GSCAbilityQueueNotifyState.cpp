// Copyright 2021 Mickael Daniel. All Rights Reserved.


#include "Animations/GSCAbilityQueueNotifyState.h"

#include "GSCDelegates.h"
#include "Abilities/GSCBlueprintFunctionLibrary.h"
#include "Components/GSCAbilityQueueComponent.h"
#include "UI/GSCUWDebugAbilityQueue.h"
#include "GSCLog.h"

void UGSCAbilityQueueNotifyState::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration)
{
	GSC_LOG(Log, TEXT("UGSCAbilityQueueNotifyState:NotifyBegin()"))
	AActor* Owner = MeshComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	UGSCAbilityQueueComponent* AbilityQueueComponent = UGSCBlueprintFunctionLibrary::GetAbilityQueueComponent(Owner);
	if (!AbilityQueueComponent)
	{
		return;
	}

	if (!AbilityQueueComponent->bAbilityQueueEnabled)
	{
		return;
	}


	GSC_LOG(Log, TEXT("UGSCAbilityQueueNotifyState:NotifyBegin() Open Ability Queue for %d allowed abilities"), AllowedAbilities.Num())
	AbilityQueueComponent->OpenAbilityQueue();
	AbilityQueueComponent->SetAllowAllAbilitiesForAbilityQueue(bAllowAllAbilities);
	AbilityQueueComponent->UpdateAllowedAbilitiesForAbilityQueue(AllowedAbilities);

	// Notify debug widgets if it's on screen
	FGSCDelegates::OnAddAbilityQueueFromMontageRow.Broadcast(Animation);
}

void UGSCAbilityQueueNotifyState::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation)
{
	GSC_LOG(Log, TEXT("UGSCAbilityQueueNotifyState:NotifyEnd()"))

	AActor* Owner = MeshComp->GetOwner();
	if (!Owner)
	{
		return;
	}

	UGSCAbilityQueueComponent* AbilityQueueComponent = UGSCBlueprintFunctionLibrary::GetAbilityQueueComponent(Owner);
	if (!AbilityQueueComponent)
	{
		return;
	}

	if (!AbilityQueueComponent->bAbilityQueueEnabled)
	{
		return;
	}

	AbilityQueueComponent->CloseAbilityQueue();
}

FString UGSCAbilityQueueNotifyState::GetNotifyName_Implementation() const
{
	return "AbilityQueueWindow";
}
