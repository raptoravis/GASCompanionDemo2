// Copyright 2021 Mickael Daniel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFeatureAction.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GSCTypes.h"
#include "GSCGameFeatureAction_AddAbilities.generated.h"

class UGSCAbilitySystemComponent;
class UGSCAbilityInputBindingComponent;
struct FComponentRequestHandle;
class UInputAction;
class UDataTable;

USTRUCT(BlueprintType)
struct FGSCGameFeatureAbilityMapping
{
	GENERATED_BODY()

	/** Type of ability to grant */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Ability)
	TSoftClassPtr<UGameplayAbility> AbilityType;

	/** Input action to bind the ability to, if any (can be left unset) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Ability)
	TSoftObjectPtr<UInputAction> InputAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Ability, meta=(EditCondition = "InputAction != nullptr", EditConditionHides))
	EGSCAbilityTriggerEvent TriggerEvent = EGSCAbilityTriggerEvent::Started;
};

USTRUCT(BlueprintType)
struct FGSCGameFeatureAttributeSetMapping
{
	GENERATED_BODY()

	/** Attribute Set to grant */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Attributes)
	TSoftClassPtr<UAttributeSet> AttributeSet;

	/** Data table referent to initialize the attributes with, if any (can be left unset) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Attributes)
	TSoftObjectPtr<UDataTable> InitializationData;
};

USTRUCT(BlueprintType)
struct FGSCGameFeatureGameplayEffectMapping
{
	GENERATED_BODY()

	/** Gameplay Effect to apply */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gameplay Effect")
	TSoftClassPtr<UGameplayEffect> EffectType;

	/** Level for the Gameplay Effect to apply */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Gameplay Effect")
	float Level = 1.f;
};

USTRUCT()
struct FGSCGameFeatureAbilitiesEntry
{
	GENERATED_BODY()

	/** The base actor class to add to */
	UPROPERTY(EditAnywhere, Category="Abilities")
	TSoftClassPtr<AActor> ActorClass;

	/** List of abilities to grant to actors of the specified class */
	UPROPERTY(EditAnywhere, Category="Abilities")
	TArray<FGSCGameFeatureAbilityMapping> GrantedAbilities;

	/** List of attribute sets to grant to actors of the specified class */
	UPROPERTY(EditAnywhere, Category="Attributes")
	TArray<FGSCGameFeatureAttributeSetMapping> GrantedAttributes;
	
	/** List of gameplay effects to grant to actors of the specified class */
	UPROPERTY(EditAnywhere, Category="Attributes")
	TArray<FGSCGameFeatureGameplayEffectMapping> GrantedEffects;
};

/**
 * GameFeatureAction responsible for granting abilities (and attributes) to actors of a specified type.
 */
UCLASS(MinimalAPI, meta = (DisplayName = "Add Abilities (GAS Companion)"))
class UGSCGameFeatureAction_AddAbilities : public UGameFeatureAction
{
	GENERATED_BODY()

public:
	//~ Begin UGameFeatureAction interface
	virtual void OnGameFeatureActivating() override;
	virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;
#if WITH_EDITORONLY_DATA
	virtual void AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData) override;
#endif
	//~ End UGameFeatureAction interface

	//~ Begin UObject interface
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(TArray<FText>& ValidationErrors) override;
#endif
	//~ End UObject interface

	/** List of Ability to grant to actors of the specified class */
	UPROPERTY(EditAnywhere, Category="Abilities", meta=(TitleProperty="ActorClass", ShowOnlyInnerProperties))
	TArray<FGSCGameFeatureAbilitiesEntry> AbilitiesList;

	void Reset();
	void HandleActorExtension(AActor* Actor, FName EventName, int32 EntryIndex);

	void AddActorAbilities(AActor* Actor, const FGSCGameFeatureAbilitiesEntry& AbilitiesEntry);
	void RemoveActorAbilities(const AActor* Actor);

	template<class ComponentType>
	ComponentType* FindOrAddComponentForActor(AActor* Actor, const FGSCGameFeatureAbilitiesEntry& AbilitiesEntry)
	{
		return Cast<ComponentType>(FindOrAddComponentForActor(ComponentType::StaticClass(), Actor, AbilitiesEntry));
	}
	UActorComponent* FindOrAddComponentForActor(UClass* ComponentType, const AActor* Actor, const FGSCGameFeatureAbilitiesEntry& AbilitiesEntry);

private:
	struct FActorExtensions
	{
		TArray<FGameplayAbilitySpecHandle> Abilities;
		TArray<UAttributeSet*> Attributes;
		TArray<FDelegateHandle> InputBindingDelegateHandles;
		TArray<FActiveGameplayEffectHandle> EffectHandles;
	};

	FDelegateHandle GameInstanceStartHandle;

	// ReSharper disable once CppUE4ProbableMemoryIssuesWithUObjectsInContainer
	TMap<AActor*, FActorExtensions> ActiveExtensions;

	TArray<TSharedPtr<FComponentRequestHandle>> ComponentRequests;

	virtual void AddToWorld(const FWorldContext& WorldContext);
	void HandleGameInstanceStart(UGameInstance* GameInstance);

	static void TryGrantAbility(UGSCAbilitySystemComponent* AbilitySystemComponent, TSubclassOf<UGameplayAbility> AbilityType, OUT FGameplayAbilitySpecHandle& AbilityHandle, OUT FGameplayAbilitySpec& AbilitySpec);
	void TryBindAbilityInput(UGSCAbilitySystemComponent* AbilitySystemComponent, const FGSCGameFeatureAbilityMapping& AbilityMapping, const FGSCGameFeatureAbilitiesEntry& AbilitiesEntry, FGameplayAbilitySpecHandle AbilityHandle, FGameplayAbilitySpec AbilitySpec, OUT FActorExtensions& AddedExtensions);
	static void TryGrantAttributes(UAbilitySystemComponent* AbilitySystemComponent, const FGSCGameFeatureAttributeSetMapping& AttributeSetMapping, OUT FActorExtensions& AddedExtensions);
	static void TryGrantGameplayEffect(UAbilitySystemComponent* AbilitySystemComponent, TSubclassOf<UGameplayEffect> EffectType, float Level, OUT FActorExtensions& AddedExtensions);

	/** Handler for AbilitySystem OnGiveAbility delegate. Sets up input binding for clients (not authority) when GameFeatures are activated during Play. */
	void HandleOnGiveAbility(FGameplayAbilitySpec& AbilitySpec, UGSCAbilityInputBindingComponent* InputComponent, UInputAction* InputAction, EGSCAbilityTriggerEvent TriggerEvent, FGameplayAbilitySpec NewAbilitySpec);

	/** Does the passed in ability system component have this attribute set? */
	static bool HasAttributeSet(UAbilitySystemComponent* AbilitySystemComponent, const TSubclassOf<UAttributeSet> Set);
	
	/** Does the passed in ability system component have ability already granted? */
	static bool HasAbility(UAbilitySystemComponent* AbilitySystemComponent, const TSubclassOf<UGameplayAbility> Ability);
};
