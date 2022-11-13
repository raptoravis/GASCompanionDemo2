// Copyright 2021 Mickael Daniel. All Rights Reserved.


#include "Abilities/GSCAbilitySystemComponent.h"

#include "Abilities/GSCBlueprintFunctionLibrary.h"
#include "Abilities/GSCGameplayAbility_MeleeBase.h"
#include "Components/GSCAbilityInputBindingComponent.h"
#include "Components/GSCAbilityQueueComponent.h"
#include "Components/GSCComboManagerComponent.h"
#include "Components/GSCCoreComponent.h"
#include "GameFramework/PlayerState.h"
#include "Animations/GSCNativeAnimInstanceInterface.h"
#include "GSCLog.h"

void UGSCAbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();

	AbilityActivatedCallbacks.AddUObject(this, &UGSCAbilitySystemComponent::OnAbilityActivatedCallback);
	AbilityFailedCallbacks.AddUObject(this, &UGSCAbilitySystemComponent::OnAbilityFailedCallback);
	AbilityEndedCallbacks.AddUObject(this, &UGSCAbilitySystemComponent::OnAbilityEndedCallback);

	// Grant startup effects on begin play instead of from within InitAbilityActorInfo to avoid
	// "ticking" periodic effects when BP is first opened
	GrantStartupEffects();
}

void UGSCAbilitySystemComponent::BeginDestroy()
{
	// Reset ...

	// Clear any delegate handled bound previously for this component
	if (AbilityActorInfo && AbilityActorInfo->OwnerActor.IsValid())
	{
		if (UGameInstance* GameInstance = AbilityActorInfo->OwnerActor->GetGameInstance())
		{
			GameInstance->GetOnPawnControllerChanged().RemoveAll(this);
		}
	}

	OnGiveAbilityDelegate.RemoveAll(this);

	// Remove any added attributes
	for (UAttributeSet* AttribSetInstance : AddedAttributes)
	{
		GetSpawnedAttributes_Mutable().Remove(AttribSetInstance);
	}


	// Clear up abilities / bindings
	UGSCAbilityInputBindingComponent* InputComponent = AbilityActorInfo && AbilityActorInfo->AvatarActor.IsValid() ? AbilityActorInfo->AvatarActor->FindComponentByClass<UGSCAbilityInputBindingComponent>() : nullptr;

	for (const FGSCMappedAbility& DefaultAbilityHandle : DefaultAbilityHandles)
	{
		if (InputComponent)
		{
			InputComponent->ClearInputBinding(DefaultAbilityHandle.Handle);
		}

		// Only Clear abilities on authority
		if (IsOwnerActorAuthoritative())
		{
			SetRemoveAbilityOnEnd(DefaultAbilityHandle.Handle);
		}
	}

	// Clear up any bound delegates in Core Component that were registered from InitAbilityActorInfo
	UGSCCoreComponent* CoreComponent = AbilityActorInfo && AbilityActorInfo->AvatarActor.IsValid() ? AbilityActorInfo->AvatarActor->FindComponentByClass<UGSCCoreComponent>() : nullptr;

	if (CoreComponent)
	{
		CoreComponent->ShutdownAbilitySystemDelegates(this);
	}


	Super::BeginDestroy();
}

void UGSCAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);

	GSC_LOG(Log, TEXT("UGSCAbilitySystemComponent::InitAbilityActorInfo() - Owner: %s, Avatar: %s"), *GetNameSafe(InOwnerActor), *GetNameSafe(InAvatarActor))

	if (AbilityActorInfo)
	{
		if (AbilityActorInfo->AnimInstance == nullptr)
		{
			AbilityActorInfo->AnimInstance = AbilityActorInfo->GetAnimInstance();
		}

		if (UGameInstance* GameInstance = InOwnerActor->GetGameInstance())
		{
			// Sign up for possess / unpossess events so that we can update the cached AbilityActorInfo accordingly
			if (!GameInstance->GetOnPawnControllerChanged().Contains(this, TEXT("OnPawnControllerChanged")))
			{
				GameInstance->GetOnPawnControllerChanged().AddDynamic(this, &UGSCAbilitySystemComponent::OnPawnControllerChanged);
			}
		}

		UAnimInstance* AnimInstance = AbilityActorInfo->GetAnimInstance();
		if (IGSCNativeAnimInstanceInterface* AnimInstanceInterface = Cast<IGSCNativeAnimInstanceInterface>(AnimInstance))
		{
			GSC_LOG(Verbose, TEXT("UGSCAbilitySystemComponent::InitAbilityActorInfo Initialize `%s` AnimInstance with Ability System"), *GetNameSafe(AnimInstance))
			AnimInstanceInterface->InitializeWithAbilitySystem(this);
		}
	}

	GrantDefaultAbilitiesAndAttributes(InOwnerActor, InAvatarActor);

	// For PlayerState client pawns, setup and update owner on companion components if pawns have them
	UGSCCoreComponent* CoreComponent = UGSCBlueprintFunctionLibrary::GetCompanionCoreComponent(InAvatarActor);
	if (CoreComponent)
	{
		CoreComponent->SetupOwner();
		CoreComponent->RegisterAbilitySystemDelegates(this);
		CoreComponent->SetStartupAbilitiesGranted(true);
	}

	// Broadcast to Blueprint InitAbilityActorInfo was called
	//
	// This will happen multiple times for both client / server
	OnInitAbilityActorInfo.Broadcast();
	if (CoreComponent)
	{
		CoreComponent->OnInitAbilityActorInfo.Broadcast();
	}
}


void UGSCAbilitySystemComponent::AbilityLocalInputPressed(const int32 InputID)
{
	// Consume the input if this InputID is overloaded with GenericConfirm/Cancel and the GenericConfim/Cancel callback is bound
	if (IsGenericConfirmInputBound(InputID))
	{
		LocalInputConfirm();
		return;
	}

	if (IsGenericCancelInputBound(InputID))
	{
		LocalInputCancel();
		return;
	}

	// ---------------------------------------------------------

	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (Spec.InputID == InputID && Spec.Ability)
		{
			Spec.InputPressed = true;

			if (Spec.Ability->IsA(UGSCGameplayAbility_MeleeBase::StaticClass()))
			{
				// Ability is a combo ability, try to activate via Combo Component
				if (!IsValid(ComboComponent))
				{
					// Combo Component ref is not set yet, set it once
					ComboComponent = UGSCBlueprintFunctionLibrary::GetComboManagerComponent(GetAvatarActor());
					if (ComboComponent)
					{
						ComboComponent->SetupOwner();
					}
				}

				// Regardless of active or not active, always try to activate the combo. Combo Component will take care of gating activation or queuing next combo
				if (IsValid(ComboComponent))
				{
					// We have a valid combo component, active combo
					ComboComponent->ActivateComboAbility(Spec.Ability->GetClass());
				}
				else
				{
					GSC_LOG(Error, TEXT("UGSCAbilitySystemComponent::AbilityLocalInputPressed - Trying to activate combo without a Combo Manager Component on the Avatar Actor. Make sure to add the component in Blueprint."))
				}
			}
			else
			{
				// Ability is not a combo ability, go through normal workflow
				if (Spec.IsActive())
				{
					if (Spec.Ability->bReplicateInputDirectly && IsOwnerActorAuthoritative() == false)
					{
						ServerSetInputPressed(Spec.Handle);
					}

					AbilitySpecInputPressed(Spec);

					// Invoke the InputPressed event. This is not replicated here. If someone is listening, they may replicate the InputPressed event to the server.
					InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Spec.Handle, Spec.ActivationInfo.GetActivationPredictionKey());
				}
				else
				{
					TryActivateAbility(Spec.Handle);
				}
			}
		}
	}
}

FGameplayAbilitySpecHandle UGSCAbilitySystemComponent::GrantAbility(const TSubclassOf<UGameplayAbility> Ability, const bool bRemoveAfterActivation)
{
	FGameplayAbilitySpecHandle AbilityHandle;
	if (!IsOwnerActorAuthoritative())
	{
		GSC_LOG(Error, TEXT("UGSCAbilitySystemComponent::GrantAbility Called on non authority"));
		return AbilityHandle;
	}

	if (Ability)
	{
		FGameplayAbilitySpec AbilitySpec(Ability);
		AbilitySpec.RemoveAfterActivation = bRemoveAfterActivation;

		AbilityHandle = GiveAbility(AbilitySpec);
	}
	return AbilityHandle;
}

void UGSCAbilitySystemComponent::OnAbilityActivatedCallback(UGameplayAbility* Ability)
{
	GSC_LOG(Log, TEXT("UGSCAbilitySystemComponent::OnAbilityActivatedCallback %s"), *Ability->GetName());
	const AActor* Avatar = GetAvatarActor();
	if (!Avatar)
	{
		GSC_LOG(Error, TEXT("UGSCAbilitySystemComponent::OnAbilityActivated No OwnerActor for this ability: %s"), *Ability->GetName());
		return;
	}

	const UGSCCoreComponent* CoreComponent = UGSCBlueprintFunctionLibrary::GetCompanionCoreComponent(Avatar);
	if (CoreComponent)
	{
		CoreComponent->OnAbilityActivated.Broadcast(Ability);
	}
}

void UGSCAbilitySystemComponent::OnAbilityFailedCallback(const UGameplayAbility* Ability, const FGameplayTagContainer& Tags)
{
	GSC_LOG(Log, TEXT("UGSCAbilitySystemComponent::OnAbilityFailedCallback %s"), *Ability->GetName());

	const AActor* Avatar = GetAvatarActor();
	if (!Avatar)
	{
		GSC_LOG(Warning, TEXT("UGSCAbilitySystemComponent::OnAbilityFailed No OwnerActor for this ability: %s Tags: %s"), *Ability->GetName(), *Tags.ToString());
		return;
	}

	const UGSCCoreComponent* CoreComponent = UGSCBlueprintFunctionLibrary::GetCompanionCoreComponent(Avatar);
	UGSCAbilityQueueComponent* AbilityQueueComponent = UGSCBlueprintFunctionLibrary::GetAbilityQueueComponent(Avatar);
	if (CoreComponent)
	{
		CoreComponent->OnAbilityFailed.Broadcast(Ability, Tags);
	}

	if (AbilityQueueComponent)
	{
		AbilityQueueComponent->OnAbilityFailed(Ability, Tags);
	}
}

void UGSCAbilitySystemComponent::OnAbilityEndedCallback(UGameplayAbility* Ability)
{
	GSC_LOG(Log, TEXT("UGSCAbilitySystemComponent::OnAbilityEndedCallback %s"), *Ability->GetName());
	const AActor* Avatar = GetAvatarActor();
	if (!Avatar)
	{
		GSC_LOG(Warning, TEXT("UGSCAbilitySystemComponent::OnAbilityEndedCallback No OwnerActor for this ability: %s"), *Ability->GetName());
		return;
	}

	const UGSCCoreComponent* CoreComponent = UGSCBlueprintFunctionLibrary::GetCompanionCoreComponent(Avatar);
	UGSCAbilityQueueComponent* AbilityQueueComponent = UGSCBlueprintFunctionLibrary::GetAbilityQueueComponent(Avatar);
	if (CoreComponent)
	{
		CoreComponent->OnAbilityEnded.Broadcast(Ability);
	}

	if (AbilityQueueComponent)
	{
		AbilityQueueComponent->OnAbilityEnded(Ability);
	}
}

bool UGSCAbilitySystemComponent::ShouldGrantAbility(const TSubclassOf<UGameplayAbility> Ability)
{
	if (bResetAbilitiesOnSpawn)
	{
		// User wants abilities to be granted each time InitAbilityActor is called
		return true;
	}

	// Check for activatable abilities, if one is matching the given Ability type, prevent re adding again
	TArray<FGameplayAbilitySpec> AbilitySpecs = GetActivatableAbilities();
	for (const FGameplayAbilitySpec& ActivatableAbility : AbilitySpecs)
	{
		if (!ActivatableAbility.Ability)
		{
			continue;
		}

		if (ActivatableAbility.Ability->GetClass() == Ability)
		{
			return false;
		}
	}

	return true;
}

void UGSCAbilitySystemComponent::GrantDefaultAbilitiesAndAttributes(AActor* InOwnerActor, AActor* InAvatarActor)
{
	GSC_LOG(Log, TEXT("UGSCAbilitySystemComponent::GrantDefaultAbilitiesAndAttributes() - Owner: %s, Avatar: %s"), *InOwnerActor->GetName(), *InAvatarActor->GetName())

	if (bResetAttributesOnSpawn)
	{
		// Reset/Remove abilities if we had already added them
		for (UAttributeSet* AttributeSet : AddedAttributes)
		{
			GetSpawnedAttributes_Mutable().Remove(AttributeSet);
		}

		AddedAttributes.Empty(GrantedAttributes.Num());
	}


	if (bResetAbilitiesOnSpawn)
	{
		for (const FGSCMappedAbility& DefaultAbilityHandle : DefaultAbilityHandles)
		{
			SetRemoveAbilityOnEnd(DefaultAbilityHandle.Handle);
		}

		for (FDelegateHandle InputBindingDelegateHandle : InputBindingDelegateHandles)
		{
			// Clear any delegate handled bound previously for this actor
			OnGiveAbilityDelegate.Remove(InputBindingDelegateHandle);
			InputBindingDelegateHandle.Reset();
		}

		DefaultAbilityHandles.Empty(GrantedAbilities.Num());
		InputBindingDelegateHandles.Empty();
	}

	UGSCAbilityInputBindingComponent* InputComponent = IsValid(InAvatarActor) ? InAvatarActor->FindComponentByClass<UGSCAbilityInputBindingComponent>() : nullptr;

	// Startup abilities
	for (const FGSCAbilityInputMapping GrantedAbility : GrantedAbilities)
	{
		const TSubclassOf<UGameplayAbility> Ability = GrantedAbility.Ability;
		UInputAction* InputAction = GrantedAbility.InputAction;

		if (!Ability)
		{
			continue;
		}


		FGameplayAbilitySpec NewAbilitySpec(Ability);

		// Try to grant the ability first
		if (IsOwnerActorAuthoritative() && ShouldGrantAbility(Ability))
		{
			// Only Grant abilities on authority
			GSC_LOG(Log, TEXT("UGSCAbilitySystemComponent::GrantDefaultAbilitiesAndAttributes - Authority, Grant Ability (%s)"), *NewAbilitySpec.Ability->GetClass()->GetName())
			FGameplayAbilitySpecHandle AbilityHandle = GiveAbility(NewAbilitySpec);
			DefaultAbilityHandles.Add(FGSCMappedAbility(AbilityHandle, NewAbilitySpec, InputAction));
		}

		// We don't grant here but try to get the spec already granted or register delegate to handle input binding
		if (InputComponent && InputAction)
		{
			// Handle for server or standalone game, clients need to bind OnGiveAbility
			const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromClass(Ability);
			if (AbilitySpec)
			{
				InputComponent->SetInputBinding(InputAction, GrantedAbility.TriggerEvent, AbilitySpec->Handle);
			}
			else
			{
				// Register a delegate triggered when ability is granted and available on clients
				FDelegateHandle DelegateHandle = OnGiveAbilityDelegate.AddUObject(this, &UGSCAbilitySystemComponent::HandleOnGiveAbility, InputComponent, InputAction, GrantedAbility.TriggerEvent, NewAbilitySpec);
				InputBindingDelegateHandles.Add(DelegateHandle);
			}
		}
	}

	// Startup attributes
	for (const FGSCAttributeSetDefinition& AttributeSetDefinition : GrantedAttributes)
	{
		if (AttributeSetDefinition.AttributeSet)
		{
			const bool bHasAttributeSet = GetAttributeSubobject(AttributeSetDefinition.AttributeSet) != nullptr;
			GSC_LOG(
				Verbose,
				TEXT("UGSCAbilitySystemComponent::GrantDefaultAbilitiesAndAttributes - HasAttributeSet: %s (%s)"),
				bHasAttributeSet ? TEXT("true") : TEXT("false"),
				*GetNameSafe(AttributeSetDefinition.AttributeSet)
			)

			// Prevent adding attribute set if already granted
			if (!bHasAttributeSet)
			{
				UAttributeSet* AttributeSet = NewObject<UAttributeSet>(InOwnerActor, AttributeSetDefinition.AttributeSet);
				if (AttributeSetDefinition.InitializationData)
				{
					AttributeSet->InitFromMetaDataTable(AttributeSetDefinition.InitializationData);
				}
				AddedAttributes.Add(AttributeSet);
				AddAttributeSetSubobject(AttributeSet);
			}
		}
	}
}

void UGSCAbilitySystemComponent::OnGiveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	Super::OnGiveAbility(AbilitySpec);
	GSC_LOG(Log, TEXT("UGSCAbilitySystemComponent::OnGiveAbility %s"), *AbilitySpec.Ability->GetName());
	OnGiveAbilityDelegate.Broadcast(AbilitySpec);
}

void UGSCAbilitySystemComponent::GrantStartupEffects()
{
	if (!IsOwnerActorAuthoritative())
	{
		return;
	}

	// Reset/Remove effects if we had already added them
	for (const FActiveGameplayEffectHandle AddedEffect : AddedEffects)
	{
		RemoveActiveGameplayEffect(AddedEffect);
	}

	FGameplayEffectContextHandle EffectContext = MakeEffectContext();
	EffectContext.AddSourceObject(this);

	AddedEffects.Empty(GrantedEffects.Num());

	for (const TSubclassOf<UGameplayEffect> GameplayEffect : GrantedEffects)
	{
		FGameplayEffectSpecHandle NewHandle = MakeOutgoingSpec(GameplayEffect, 1, EffectContext);
		if (NewHandle.IsValid())
		{
			FActiveGameplayEffectHandle EffectHandle = ApplyGameplayEffectSpecToTarget(*NewHandle.Data.Get(), this);
			AddedEffects.Add(EffectHandle);
		}
	}
}

// ReSharper disable CppParameterMayBeConstPtrOrRef
void UGSCAbilitySystemComponent::OnPawnControllerChanged(APawn* Pawn, AController* NewController)
{
	if (AbilityActorInfo && AbilityActorInfo->OwnerActor == Pawn && AbilityActorInfo->PlayerController != NewController)
	{
		if (!NewController)
		{
			// NewController null, prevent refresh actor info. Needed to ensure TargetActor EndPlay properly unbind from GenericLocalConfirmCallbacks/GenericLocalCancelCallbacks
			// and avoid an ensure error if ActorInfo PlayerController is invalid
			return;
		}

		AbilityActorInfo->InitFromActor(AbilityActorInfo->OwnerActor.Get(), AbilityActorInfo->AvatarActor.Get(), this);
	}
}

void UGSCAbilitySystemComponent::HandleOnGiveAbility(FGameplayAbilitySpec& AbilitySpec, UGSCAbilityInputBindingComponent* InputComponent, UInputAction* InputAction, EGSCAbilityTriggerEvent TriggerEvent, FGameplayAbilitySpec NewAbilitySpec)
{
	GSC_LOG(
		Log,
		TEXT("UGSCAbilitySystemComponent::HandleOnGiveAbility: %s, Ability: %s, Input: %s (TriggerEvent: %s) - (InputComponent: %s)"),
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
