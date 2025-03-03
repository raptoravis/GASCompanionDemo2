// Copyright 2021 Mickael Daniel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFeatureAction.h"
#include "GSCGameFeatureAction_AddInputMappingContext.generated.h"

class UInputMappingContext;
struct FComponentRequestHandle;

/**
* Adds InputMappingContext to local players' EnhancedInput system.
*
* Expects that local players are set up to use the EnhancedInput system and a Player Controller with Game Feature
* extensions (such as AGSCModularPlayerController)
*
* If you don't see Mapping Context added when the Game Feature is activated by default when starting PIE, make sure
* Game Mode is setup to use an AGSCModularPlayerController or a child of it. Or at the very least a Player Controller
* that sends a ModularGameplay ExtensionEvent with the "GameActorReady" value when it receives player.
*
* @see AGSCModularPlayerController::ReceivedPlayer
*/
UCLASS(MinimalAPI, meta = (DisplayName = "Add Input Mapping (GAS Companion)"))
class UGSCGameFeatureAction_AddInputMappingContext : public UGameFeatureAction
{
	GENERATED_BODY()

public:

	// Input Mapping Context to add to local players EnhancedInput system.
	UPROPERTY(EditAnywhere, Category="Input")
	TSoftObjectPtr<UInputMappingContext> InputMapping;

	// Higher priority input mappings will be prioritized over mappings with a lower priority.
	UPROPERTY(EditAnywhere, Category="Input")
	int32 Priority = 0;

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


private:
	TArray<TSharedPtr<FComponentRequestHandle>> ExtensionRequestHandles;
	TArray<TWeakObjectPtr<APlayerController>> ControllersAddedTo;

	FDelegateHandle GameInstanceStartHandle;

	virtual void AddToWorld(const FWorldContext& WorldContext);

	void Reset();
	void HandleControllerExtension(AActor* Actor, FName EventName);
	void AddInputMappingForPlayer(UPlayer* Player);
	void RemoveInputMapping(APlayerController* PlayerController);
	void HandleGameInstanceStart(UGameInstance* GameInstance);
};
