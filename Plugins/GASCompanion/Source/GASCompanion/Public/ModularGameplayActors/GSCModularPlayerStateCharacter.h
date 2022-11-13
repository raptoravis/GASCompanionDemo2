// Copyright 2021 Mickael Daniel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "GSCModularPlayerStateCharacter.generated.h"

class UGSCAbilitySystemComponent;

/**
 * Minimal class that supports extension by game feature plugins.
 *
 * Intended to be used for ACharacters using AbilitySystemComponent living on PlayerState.
 */
UCLASS(Blueprintable)
class GASCOMPANION_API AGSCModularPlayerStateCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AGSCModularPlayerStateCharacter(const FObjectInitializer& ObjectInitializer);

	// Cached AbilitySystemComponent. Real owner is PlayerState, but pointer gets updated to use PlayerState's here in PossessedBy / OnRep_PlayerState
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	//~ Begin IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~ End IAbilitySystemInterface

	//~ Begin AActor Interface
	virtual void PreInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor Interface

	//~ Begin APawn Interface
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	//~ End APawn Interface
};
