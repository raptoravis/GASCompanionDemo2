// Copyright 2021 Mickael Daniel. All Rights Reserved.


#include "GameFeatures/Actions/GSCGameFeatureAction_AddAbilities.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "EngineUtils.h"
#include "GameFeaturesSubsystemSettings.h"
#include "Components/GSCAbilityInputBindingComponent.h"
#include "Abilities/GSCAbilitySystemComponent.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Components/GSCCoreComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h" // for FWorldDelegates::OnStartGameInstance
#include "Engine/Engine.h" // for FWorldContext
#include "GSCLog.h"

#define LOCTEXT_NAMESPACE "GASCompanion"

void UGSCGameFeatureAction_AddAbilities::OnGameFeatureActivating()
{
	if (!ensureAlways(ActiveExtensions.Num() == 0) || !ensureAlways(ComponentRequests.Num() == 0))
	{
		Reset();
	}

	GameInstanceStartHandle = FWorldDelegates::OnStartGameInstance.AddUObject(this, &UGSCGameFeatureAction_AddAbilities::HandleGameInstanceStart);

	check(ComponentRequests.Num() == 0);

	// Add to any worlds with associated game instances that have already been initialized
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		AddToWorld(WorldContext);
	}

	Super::OnGameFeatureActivating();
}

void UGSCGameFeatureAction_AddAbilities::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	Super::OnGameFeatureDeactivating(Context);

	FWorldDelegates::OnStartGameInstance.Remove(GameInstanceStartHandle);

	Reset();
}

#if WITH_EDITORONLY_DATA
void UGSCGameFeatureAction_AddAbilities::AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData)
{
	if (!UAssetManager::IsValid())
	{
		return;
	}

	auto AddBundleAsset = [&AssetBundleData](const FSoftObjectPath& SoftObjectPath)
	{
		AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateClient, SoftObjectPath);
		AssetBundleData.AddBundleAsset(UGameFeaturesSubsystemSettings::LoadStateServer, SoftObjectPath);
	};

	for (const FGSCGameFeatureAbilitiesEntry& Entry : AbilitiesList)
	{
		for (const FGSCGameFeatureAbilityMapping& Ability : Entry.GrantedAbilities)
		{
			AddBundleAsset(Ability.AbilityType.ToSoftObjectPath());
			if (!Ability.InputAction.IsNull())
			{
				AddBundleAsset(Ability.InputAction.ToSoftObjectPath());
			}
		}

		for (const FGSCGameFeatureAttributeSetMapping& Attributes : Entry.GrantedAttributes)
		{
			AddBundleAsset(Attributes.AttributeSet.ToSoftObjectPath());
			if (!Attributes.InitializationData.IsNull())
			{
				AddBundleAsset(Attributes.InitializationData.ToSoftObjectPath());
			}
		}

		for (const FGSCGameFeatureGameplayEffectMapping& Effect : Entry.GrantedEffects) 
		{
			AddBundleAsset(Effect.EffectType.ToSoftObjectPath());
		}
	}
}
#endif

#if WITH_EDITOR
EDataValidationResult UGSCGameFeatureAction_AddAbilities::IsDataValid(TArray<FText>& ValidationErrors)
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(ValidationErrors), EDataValidationResult::Valid);

	int32 EntryIndex = 0;
	for (const FGSCGameFeatureAbilitiesEntry& Entry : AbilitiesList)
	{
		if (Entry.ActorClass.IsNull())
		{
			Result = EDataValidationResult::Invalid;
			ValidationErrors.Add(FText::Format(LOCTEXT("EntryHasNullActor", "Null ActorClass at index {0} in AbilitiesList"), FText::AsNumber(EntryIndex)));
		}

		if (Entry.GrantedAbilities.IsEmpty() && Entry.GrantedAttributes.IsEmpty() && Entry.GrantedEffects.IsEmpty())
		{
			Result = EDataValidationResult::Invalid;
			ValidationErrors.Add(LOCTEXT("EntryHasNoAddOns", "Granted Abilities / Attributes / Effects are all empty. This action should grant at least one of these."));
		}

		int32 AbilityIndex = 0;
		for (const FGSCGameFeatureAbilityMapping& Ability : Entry.GrantedAbilities)
		{
			if (Ability.AbilityType.IsNull())
			{
				Result = EDataValidationResult::Invalid;
				ValidationErrors.Add(FText::Format(LOCTEXT("EntryHasNullAbility", "Null AbilityType at index {0} in AbilitiesList[{1}].GrantedAbilities"), FText::AsNumber(AbilityIndex), FText::AsNumber(EntryIndex)));
			}
			++AbilityIndex;
		}

		int32 AttributesIndex = 0;
		for (const FGSCGameFeatureAttributeSetMapping& Attributes : Entry.GrantedAttributes)
		{
			if (Attributes.AttributeSet.IsNull())
			{
				Result = EDataValidationResult::Invalid;
				ValidationErrors.Add(FText::Format(LOCTEXT("EntryHasNullAttributeSet", "Null AttributeSetType at index {0} in AbilitiesList[{1}].GrantedAttributes"), FText::AsNumber(AttributesIndex), FText::AsNumber(EntryIndex)));
			}
			++AttributesIndex;
		}

		int32 EffectsIndex = 0;
		for (const FGSCGameFeatureGameplayEffectMapping& Effect : Entry.GrantedEffects) 
		{
			if (Effect.EffectType.IsNull())
			{
				Result = EDataValidationResult::Invalid;
				ValidationErrors.Add(FText::Format(LOCTEXT("EntryHasNullEffect", "Null GameplayEffectType at index {0} in AbilitiesList[{1}].GrantedEffects"), FText::AsNumber(EffectsIndex), FText::AsNumber(EntryIndex)));
			}
			++EffectsIndex;
		}

		++EntryIndex;
	}

	return Result;
}
#endif

void UGSCGameFeatureAction_AddAbilities::Reset()
{
	while (ActiveExtensions.Num() != 0)
	{
		const auto ExtensionIt = ActiveExtensions.CreateIterator();
		RemoveActorAbilities(ExtensionIt->Key);
	}

	ComponentRequests.Empty();
}

void UGSCGameFeatureAction_AddAbilities::HandleActorExtension(AActor* Actor, const FName EventName, const int32 EntryIndex)
{
	if (AbilitiesList.IsValidIndex(EntryIndex))
	{
		GSC_LOG(Verbose, TEXT("UGSCGameFeatureAction_AddAbilities::HandleActorExtension '%s'. EventName: %s"), *Actor->GetPathName(), *EventName.ToString());
		const FGSCGameFeatureAbilitiesEntry& Entry = AbilitiesList[EntryIndex];
		if (EventName == UGameFrameworkComponentManager::NAME_ExtensionRemoved || EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved)
		{
			GSC_LOG(Verbose, TEXT("UGSCGameFeatureAction_AddAbilities::HandleActorExtension remove '%s'. Abilities will be removed."), *Actor->GetPathName());
			RemoveActorAbilities(Actor);
		}
		else if (EventName == UGameFrameworkComponentManager::NAME_ExtensionAdded || EventName == UGameFrameworkComponentManager::NAME_GameActorReady)
		{
			GSC_LOG(Verbose, TEXT("UGSCGameFeatureAction_AddAbilities::HandleActorExtension add '%s'. Abilities will be granted."), *Actor->GetPathName());
			AddActorAbilities(Actor, Entry);
		}
	}
}

void UGSCGameFeatureAction_AddAbilities::AddActorAbilities(AActor* Actor, const FGSCGameFeatureAbilitiesEntry& AbilitiesEntry)
{
	if (!IsValid(Actor))
	{
		GSC_LOG(Error, TEXT("Failed to find/add an ability component. Target Actor is not valid"));
		return;
	}

	// TODO: Remove coupling to UGSCAbilitySystemComponent. Should work off just an UAbilitySystemComponent
	// Right now, required because of TryBindAbilityInput and necessity for OnGiveAbilityDelegate, but delegate could be reworked to be from an Interface

	// Go through IAbilitySystemInterface::GetAbilitySystemComponent() to handle target pawn using ASC on Player State
	UGSCAbilitySystemComponent* ExistingASC = Cast<UGSCAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor));
	UGSCAbilitySystemComponent* AbilitySystemComponent = ExistingASC ? ExistingASC : FindOrAddComponentForActor<UGSCAbilitySystemComponent>(Actor, AbilitiesEntry);
	if (!AbilitySystemComponent)
	{
		GSC_LOG(Error, TEXT("Failed to find/add an ability component to '%s'. Abilities will not be granted."), *Actor->GetPathName());
		return;
	}

	AActor* OwnerActor = AbilitySystemComponent->GetOwnerActor();
	AActor* AvatarActor = AbilitySystemComponent->GetAvatarActor();
	
	GSC_LOG(Display, TEXT("Trying to add actor abilities from Game Feature action for Owner: %s, Avatar: %s, Original Actor: %s"), *GetNameSafe(OwnerActor), *GetNameSafe(AvatarActor), *GetNameSafe(Actor));

	// Handle cleaning up of previous attributes / abilities in case of respawns
	FActorExtensions* ActorExtensions = ActiveExtensions.Find(OwnerActor);
	if (ActorExtensions)
	{
		if (AbilitySystemComponent->bResetAttributesOnSpawn)
		{
			// ASC wants reset, remove attributes
			for (UAttributeSet* AttribSetInstance : ActorExtensions->Attributes)
			{
				AbilitySystemComponent->GetSpawnedAttributes_Mutable().Remove(AttribSetInstance);
			}
		}

		if (AbilitySystemComponent->bResetAbilitiesOnSpawn)
		{
			// ASC wants reset, remove abilities
			// TODO: Quid Actor if ActorClass in GameFeature DataAsset is PlayerState ?
			UGSCAbilityInputBindingComponent* InputComponent = AvatarActor ? AvatarActor->FindComponentByClass<UGSCAbilityInputBindingComponent>() : nullptr;
			for (const FGameplayAbilitySpecHandle& AbilityHandle : ActorExtensions->Abilities)
			{
				if (InputComponent)
				{
					InputComponent->ClearInputBinding(AbilityHandle);
				}

				// Only Clear abilities on authority
				if (AbilitySystemComponent->IsOwnerActorAuthoritative())
				{
					AbilitySystemComponent->SetRemoveAbilityOnEnd(AbilityHandle);
				}
			}

			// Clear any delegate handled bound previously for this actor
			for (FDelegateHandle DelegateHandle : ActorExtensions->InputBindingDelegateHandles)
			{
				AbilitySystemComponent->OnGiveAbilityDelegate.Remove(DelegateHandle);
				DelegateHandle.Reset();
			}
		}

		// Remove effects
		for (const FActiveGameplayEffectHandle& EffectHandle : ActorExtensions->EffectHandles)
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(EffectHandle);
		}

		ActiveExtensions.Remove(OwnerActor);
	}
	

	FActorExtensions AddedExtensions;
	AddedExtensions.Abilities.Reserve(AbilitiesEntry.GrantedAbilities.Num());
	AddedExtensions.Attributes.Reserve(AbilitiesEntry.GrantedAttributes.Num());
	AddedExtensions.EffectHandles.Reserve(AbilitiesEntry.GrantedEffects.Num());

	for (const FGSCGameFeatureAbilityMapping& Ability : AbilitiesEntry.GrantedAbilities)
	{
		if (!Ability.AbilityType.IsNull())
		{
			// Try to grant the ability first
			FGameplayAbilitySpec AbilitySpec;
			FGameplayAbilitySpecHandle AbilityHandle;
			TryGrantAbility(AbilitySystemComponent, Ability.AbilityType.LoadSynchronous(), AbilityHandle, AbilitySpec);

			// Handle Input Mapping now
			if (!Ability.InputAction.IsNull())
			{
				TryBindAbilityInput(AbilitySystemComponent, Ability, AbilitiesEntry, AbilityHandle, AbilitySpec, AddedExtensions);
			}

			AddedExtensions.Abilities.Add(AbilityHandle);
		}
	}

	for (const FGSCGameFeatureAttributeSetMapping& Attributes : AbilitiesEntry.GrantedAttributes)
	{
		if (!Attributes.AttributeSet.IsNull() && AbilitySystemComponent->IsOwnerActorAuthoritative())
		{
			TryGrantAttributes(AbilitySystemComponent, Attributes, AddedExtensions);
		}
	}

	for (const FGSCGameFeatureGameplayEffectMapping& Effect : AbilitiesEntry.GrantedEffects)
	{
		if (!Effect.EffectType.IsNull())
		{
			TryGrantGameplayEffect(AbilitySystemComponent, Effect.EffectType.LoadSynchronous(), Effect.Level, AddedExtensions);
		}
	}

	// GSCCore component could be added to avatars
	if (UGSCCoreComponent* CoreComponent = AvatarActor->FindComponentByClass<UGSCCoreComponent>())
	{
		// Make sure to notify we may have added attributes
		CoreComponent->RegisterAbilitySystemDelegates(AbilitySystemComponent);
	}

	ActiveExtensions.Add(OwnerActor, AddedExtensions);
}

void UGSCGameFeatureAction_AddAbilities::RemoveActorAbilities(const AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (FActorExtensions* ActorExtensions = ActiveExtensions.Find(Actor))
	{
		UAbilitySystemComponent* AbilitySystemComponent = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		if (AbilitySystemComponent)
		{
			// Remove effects
			for (const FActiveGameplayEffectHandle& EffectHandle : ActorExtensions->EffectHandles)
			{
				AbilitySystemComponent->RemoveActiveGameplayEffect(EffectHandle);
			}
			
			// Remove attributes
			for (UAttributeSet* AttribSetInstance : ActorExtensions->Attributes)
			{
				AbilitySystemComponent->GetSpawnedAttributes_Mutable().Remove(AttribSetInstance);
			}

			// Remove abilities
			UGSCAbilityInputBindingComponent* InputComponent = Actor->FindComponentByClass<UGSCAbilityInputBindingComponent>();
			for (const FGameplayAbilitySpecHandle& AbilityHandle : ActorExtensions->Abilities)
			{
				if (InputComponent)
				{
					InputComponent->ClearInputBinding(AbilityHandle);
				}

				// Only Clear abilities on authority
				if (AbilitySystemComponent->IsOwnerActorAuthoritative())
				{
					AbilitySystemComponent->SetRemoveAbilityOnEnd(AbilityHandle);
				}
			}
		}
		else
		{
			GSC_LOG(Warning, TEXT("UGSCGameFeatureAction_AddAbilities::RemoveActorAbilities: Not able to find AbilitySystemComponent for %s.\n\n- This may happen for Player State ASC when game is shut downed."), *GetNameSafe(Actor))
		}

		// We need to clean up give ability delegates
		if (UGSCAbilitySystemComponent* ASC = Cast<UGSCAbilitySystemComponent>(AbilitySystemComponent))
		{
			// Clear any delegate handled bound previously for this actor
			for (FDelegateHandle InputBindingDelegateHandle : ActorExtensions->InputBindingDelegateHandles)
			{
				ASC->OnGiveAbilityDelegate.Remove(InputBindingDelegateHandle);
				InputBindingDelegateHandle.Reset();
			}
		}

		ActiveExtensions.Remove(Actor);
	}
}

UActorComponent* UGSCGameFeatureAction_AddAbilities::FindOrAddComponentForActor(UClass* ComponentType, const AActor* Actor, const FGSCGameFeatureAbilitiesEntry& AbilitiesEntry)
{
	UActorComponent* Component = Actor->FindComponentByClass(ComponentType);

	bool bMakeComponentRequest = (Component == nullptr);
	if (Component)
	{
		// Check to see if this component was created from a different `UGameFrameworkComponentManager` request.
		// `Native` is what `CreationMethod` defaults to for dynamically added components.
		if (Component->CreationMethod == EComponentCreationMethod::Native)
		{
			// Attempt to tell the difference between a true native component and one created by the GameFrameworkComponent system.
			// If it is from the UGameFrameworkComponentManager, then we need to make another request (requests are ref counted).
			const UObject* ComponentArchetype = Component->GetArchetype();
			bMakeComponentRequest = ComponentArchetype->HasAnyFlags(RF_ClassDefaultObject);
		}
	}

	if (bMakeComponentRequest)
	{
		const UWorld* World = Actor->GetWorld();
		const UGameInstance* GameInstance = World->GetGameInstance();

		if (UGameFrameworkComponentManager* ComponentMan = UGameInstance::GetSubsystem<UGameFrameworkComponentManager>(GameInstance))
		{
			const TSoftClassPtr<AActor> ActorClass = Actor->GetClass();
			const TSharedPtr<FComponentRequestHandle> RequestHandle = ComponentMan->AddComponentRequest(ActorClass, ComponentType);
			ComponentRequests.Add(RequestHandle);
		}

		if (!Component)
		{
			Component = Actor->FindComponentByClass(ComponentType);
			ensureAlways(Component);
		}
	}

	return Component;
}

void UGSCGameFeatureAction_AddAbilities::AddToWorld(const FWorldContext& WorldContext)
{
	const UWorld* World = WorldContext.World();
	const UGameInstance* GameInstance = WorldContext.OwningGameInstance;

	if ((GameInstance != nullptr) && (World != nullptr) && World->IsGameWorld())
	{
		UGameFrameworkComponentManager* ComponentMan = UGameInstance::GetSubsystem<UGameFrameworkComponentManager>(GameInstance);

		if (ComponentMan)
		{
			int32 EntryIndex = 0;

			GSC_LOG(Verbose, TEXT("Adding abilities for %s to world %s"), *GetPathNameSafe(this), *World->GetDebugDisplayName());

			for (const FGSCGameFeatureAbilitiesEntry& Entry : AbilitiesList)
			{
				if (!Entry.ActorClass.IsNull())
				{
					const UGameFrameworkComponentManager::FExtensionHandlerDelegate AddAbilitiesDelegate = UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(
						this,
						&UGSCGameFeatureAction_AddAbilities::HandleActorExtension,
						EntryIndex
					);
					TSharedPtr<FComponentRequestHandle> ExtensionRequestHandle = ComponentMan->AddExtensionHandler(Entry.ActorClass, AddAbilitiesDelegate);

					ComponentRequests.Add(ExtensionRequestHandle);
					EntryIndex++;
				}
			}
		}
	}
}

// ReSharper disable once CppParameterMayBeConstPtrOrRef
void UGSCGameFeatureAction_AddAbilities::HandleGameInstanceStart(UGameInstance* GameInstance)
{
	if (const FWorldContext* WorldContext = GameInstance->GetWorldContext())
	{
		AddToWorld(*WorldContext);
	}
}

void UGSCGameFeatureAction_AddAbilities::TryGrantAbility(UGSCAbilitySystemComponent* AbilitySystemComponent, const TSubclassOf<UGameplayAbility> AbilityType, FGameplayAbilitySpecHandle& AbilityHandle, FGameplayAbilitySpec& AbilitySpec)
{
	check(AbilityType);
	check(AbilitySystemComponent);

	AbilitySpec = FGameplayAbilitySpec(AbilityType);
	
	// Try to grant the ability first
	if (AbilitySystemComponent->IsOwnerActorAuthoritative())
	{
		// Only Grant abilities on authority, and only if we should (ability not granted yet or wants reset on spawn)
		if (AbilitySystemComponent->ShouldGrantAbility(AbilityType))
		{
			GSC_LOG(Verbose, TEXT("AddActorAbilities: Authority, Grant Ability (%s) with input ID: %d"), *AbilityType->GetName())
			AbilityHandle = AbilitySystemComponent->GiveAbility(AbilitySpec);
		}
		else
		{
			// In case granting is prevented because of ability already existing, return the existing handle
			const FGameplayAbilitySpec* ExistingAbilitySpec = AbilitySystemComponent->FindAbilitySpecFromClass(AbilityType);
			if (ExistingAbilitySpec)
			{
				AbilityHandle = ExistingAbilitySpec->Handle;
			}
		}
	}
	else
	{
		// For clients, try to get ability spec and update handle used later on for input binding
		const FGameplayAbilitySpec* ExistingAbilitySpec = AbilitySystemComponent->FindAbilitySpecFromClass(AbilityType);
		if (ExistingAbilitySpec)
		{
			AbilityHandle = ExistingAbilitySpec->Handle;
		}
		
		GSC_LOG(Verbose, TEXT("AddActorAbilities: Not Authority, try to find ability handle from spec: %s"), *AbilityHandle.ToString())
	}
}

void UGSCGameFeatureAction_AddAbilities::TryBindAbilityInput(UGSCAbilitySystemComponent* AbilitySystemComponent, const FGSCGameFeatureAbilityMapping& AbilityMapping, const FGSCGameFeatureAbilitiesEntry& AbilitiesEntry, FGameplayAbilitySpecHandle AbilityHandle, FGameplayAbilitySpec AbilitySpec, FActorExtensions& AddedExtensions)
{
	check(AbilitySystemComponent);

	AActor* OwnerActor = AbilitySystemComponent->GetOwnerActor();
	AActor* AvatarActor = AbilitySystemComponent->GetAvatarActor();

	// UGSCAbilityInputBindingComponent is a PawnComponent, ensure owner of it is actually a pawn
	APawn* TargetPawn = Cast<APawn>(OwnerActor);
	if (!TargetPawn)
	{
		if (APawn* AvatarPawn = Cast<APawn>(AvatarActor))
		{
			TargetPawn = AvatarPawn;
		}
	}

	if (TargetPawn)
	{
		UGSCAbilityInputBindingComponent* InputComponent = FindOrAddComponentForActor<UGSCAbilityInputBindingComponent>(TargetPawn, AbilitiesEntry);
		if (InputComponent)
		{
			GSC_LOG(Verbose, TEXT("AddActorAbilities: TryBindAbilityInput - Try to setup input binding for '%s': '%s' (%s)"), *AbilityMapping.InputAction.ToString(), *AbilityHandle.ToString(), *AbilitySpec.Handle.ToString())
			if (AbilityHandle.IsValid())
			{
				// Setup input binding if AbilityHandle is valid and already granted (on authority, or when Game Features is active by default)
				InputComponent->SetInputBinding(AbilityMapping.InputAction.LoadSynchronous(), AbilityMapping.TriggerEvent, AbilityHandle);
			}
			else
			{
				// Register a delegate triggered when ability is granted and available on clients (needed when Game Features are made active during play)
				UInputAction* InputAction = AbilityMapping.InputAction.LoadSynchronous();
				const FDelegateHandle DelegateHandle = AbilitySystemComponent->OnGiveAbilityDelegate.AddUObject(this, &UGSCGameFeatureAction_AddAbilities::HandleOnGiveAbility, InputComponent, InputAction, AbilityMapping.TriggerEvent, AbilitySpec);
				AddedExtensions.InputBindingDelegateHandles.Add(DelegateHandle);
			}
		}
		else
		{
			GSC_LOG(Error, TEXT("Failed to find/add an ability input binding component to '%s' -- FindOrAddComponentForActor failed."), *TargetPawn->GetPathName());
		}
	}
	else
	{
		GSC_LOG(Error, TEXT("Failed to find/add an ability input binding component to '%s' -- are you sure it's a pawn class ?"), *GetNameSafe(OwnerActor));
	}
}

void UGSCGameFeatureAction_AddAbilities::TryGrantAttributes(UAbilitySystemComponent* AbilitySystemComponent, const FGSCGameFeatureAttributeSetMapping& AttributeSetMapping, FActorExtensions& AddedExtensions)
{
	check(AbilitySystemComponent);

	AActor* OwnerActor = AbilitySystemComponent->GetOwnerActor();
	if (!IsValid(OwnerActor))
	{
		GSC_LOG(Error, TEXT("AddActorAbilities: TryGrantAttributes - Ability System Component owner actor is not valid"))
		return;
	}

	const TSubclassOf<UAttributeSet> AttributeSetType = AttributeSetMapping.AttributeSet.LoadSynchronous();
	if (!AttributeSetType)
	{
		GSC_LOG(Error, TEXT("AddActorAbilities: TryGrantAttributes - AttributeSet class is invalid"))
		return;
	}

	// Prevent adding the same attribute set multiple times (if already registered by another GF or on Actor ASC directly)
	if (HasAttributeSet(AbilitySystemComponent, AttributeSetType))
	{
		GSC_LOG(Warning, TEXT("AddActorAbilities: TryGrantAttributes - %s AttributeSet is already added to %s"), *AttributeSetType->GetName(), *OwnerActor->GetName())
		return;
	}

	UAttributeSet* AttributeSet = NewObject<UAttributeSet>(OwnerActor, AttributeSetType);
	if (!AttributeSetMapping.InitializationData.IsNull())
	{
		const UDataTable* InitData = AttributeSetMapping.InitializationData.LoadSynchronous();
		if (InitData)
		{
			AttributeSet->InitFromMetaDataTable(InitData);
		}
	}

	AddedExtensions.Attributes.Add(AttributeSet);
	AbilitySystemComponent->AddAttributeSetSubobject(AttributeSet);
	AbilitySystemComponent->bIsNetDirty = true;
}

void UGSCGameFeatureAction_AddAbilities::TryGrantGameplayEffect(UAbilitySystemComponent* AbilitySystemComponent, TSubclassOf<UGameplayEffect> EffectType, float Level, FActorExtensions& AddedExtensions)
{
	check(AbilitySystemComponent);

	if (!AbilitySystemComponent->IsOwnerActorAuthoritative())
	{
		return;
	}

	if (!EffectType)
	{
		GSC_LOG(Warning, TEXT("UGSCGameFeatureAction_AddAbilities::TryGrantGameplayEffect Trying to apply an effect from an invalid class"))
		return;
	}

	const FGameplayEffectContextHandle EffectContext = AbilitySystemComponent->MakeEffectContext();
	const FGameplayEffectSpecHandle NewHandle = AbilitySystemComponent->MakeOutgoingSpec(EffectType, Level, EffectContext);
	if (NewHandle.IsValid())
	{
		const FActiveGameplayEffectHandle EffectHandle = AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*NewHandle.Data.Get());
		if (EffectHandle.IsValid())
		{
			AddedExtensions.EffectHandles.Add(EffectHandle);
		}
	}
}

// ReSharper disable once CppMemberFunctionMayBeConst
// ReSharper disable once CppParameterMayBeConstPtrOrRef
void UGSCGameFeatureAction_AddAbilities::HandleOnGiveAbility(FGameplayAbilitySpec& AbilitySpec, UGSCAbilityInputBindingComponent* InputComponent, UInputAction* InputAction, const EGSCAbilityTriggerEvent TriggerEvent, const FGameplayAbilitySpec NewAbilitySpec)
{
	GSC_LOG(
		Verbose,
		TEXT("UGSCGameFeatureAction_AddAbilities::HandleOnGiveAbility: %s, Ability: %s, Input: %s (TriggerEvent: %s) - (InputComponent: %s)"),
		*AbilitySpec.Handle.ToString(),
		*GetNameSafe(AbilitySpec.Ability),
		*GetNameSafe(InputAction),
		*UEnum::GetValueAsName(TriggerEvent).ToString(),
		*GetNameSafe(InputComponent)
	);

	if (InputComponent && InputAction && AbilitySpec.Ability == NewAbilitySpec.Ability)
	{
		InputComponent->SetInputBinding(InputAction, TriggerEvent, AbilitySpec.Handle);
	}
}

bool UGSCGameFeatureAction_AddAbilities::HasAttributeSet(UAbilitySystemComponent* AbilitySystemComponent, const TSubclassOf<UAttributeSet> Set)
{
	check(AbilitySystemComponent != nullptr);

	for (const UAttributeSet* SpawnedAttribute : AbilitySystemComponent->GetSpawnedAttributes())
	{
		if (SpawnedAttribute && SpawnedAttribute->IsA(Set))
		{
			return true;
		}
	}

	return false;
}

bool UGSCGameFeatureAction_AddAbilities::HasAbility(UAbilitySystemComponent* AbilitySystemComponent, const TSubclassOf<UGameplayAbility> Ability)
{
	check(AbilitySystemComponent != nullptr);
	
	// Check for activatable abilities, if one is matching the given Ability type, prevent re adding again
	TArray<FGameplayAbilitySpec> AbilitySpecs = AbilitySystemComponent->GetActivatableAbilities();
	for (const FGameplayAbilitySpec& ActivatableAbility : AbilitySpecs)
	{
		if (!ActivatableAbility.Ability)
		{
			continue;
		}

		if (ActivatableAbility.Ability->GetClass() == Ability)
		{
			return true;
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
