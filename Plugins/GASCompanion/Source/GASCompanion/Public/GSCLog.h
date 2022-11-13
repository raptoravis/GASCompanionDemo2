// Copyright 2021 Mickael Daniel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Intended categories:
//	Log - This happened. What gameplay programmers may care about to debug
//	Verbose - This is why this happened. What you may turn on to debug the ability system code.
//

GASCOMPANION_API DECLARE_LOG_CATEGORY_EXTERN(LogAbilitySystemCompanion, Display, All);
GASCOMPANION_API DECLARE_LOG_CATEGORY_EXTERN(LogAbilitySystemCompanionUI, Display, All);

class FGSCScreenLogger
{
public:

	static FColor GetOnScreenVerbosityColor(const ELogVerbosity::Type Verbosity)
	{
		return
			Verbosity == ELogVerbosity::Fatal || Verbosity == ELogVerbosity::Error ? FColor::Red :
			Verbosity == ELogVerbosity::Warning ? FColor::Yellow :
			Verbosity == ELogVerbosity::Display || Verbosity == ELogVerbosity::Log ? FColor::Cyan :
			Verbosity == ELogVerbosity::Verbose || Verbosity == ELogVerbosity::VeryVerbose ? FColor::Orange :
			FColor::Cyan;
	}

	static void AddOnScreenDebugMessage(const ELogVerbosity::Type Verbosity, const FString Message)
	{
		if (GEngine)
		{
			const FColor Color = GetOnScreenVerbosityColor(Verbosity);
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, 5.f, Color, Message);
		}
	}
};

#define GSC_LOG(Verbosity, Format, ...) \
{ \
    UE_LOG(LogAbilitySystemCompanion, Verbosity, Format, ##__VA_ARGS__); \
}

#define GSC_UI_LOG(Verbosity, Format, ...) \
{ \
    UE_LOG(LogAbilitySystemCompanionUI, Verbosity, Format, ##__VA_ARGS__); \
}

#define GSC_SLOG(Verbosity, Format, ...) \
{ \
	FGSCScreenLogger::AddOnScreenDebugMessage(ELogVerbosity::Verbosity, FString::Printf(Format, ##__VA_ARGS__)); \
	UE_LOG(LogAbilitySystemCompanion, Verbosity, Format, ##__VA_ARGS__); \
}
