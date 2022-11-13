// Copyright 2020 Mickael Daniel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GSCDeprecatedGameplayEffectTemplate.generated.h"

/**
 * This class is introduced just to handle deprecation of various Gameplay Effect Template that
 * were used in GAS Companion to generate Context Menu Gameplay Effects (defined in Project Settings)
 *
 * Those template Blueprints are not meant to be used directly, and GE Blueprints created via Context Menu before
 * were child of it. Creation Menu has been reworked to instead create a direct child of UGameplayEffect
 * (not the BP template) by copying over Class Default Object properties in the newly created Blueprint, from
 * the Template Blueprint.
 *
 * Distinction between GE Template Blueprint and real GE Blueprints is important to make to hide templates in various dropdown
 * (Subclass of GameplayEffect properties, or nodes like ApplyGameplayEffect) and not clutter project with all pre-defined templates.
 */
UCLASS(Abstract, Deprecated)
class UDEPRECATED_GSCDeprecatedGameplayEffectTemplate : public UGameplayEffect
{
	GENERATED_BODY()
	
};
