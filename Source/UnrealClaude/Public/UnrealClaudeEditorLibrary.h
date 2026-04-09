// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealClaudeEditorLibrary.generated.h"

UCLASS()
class UNREALCLAUDE_API UUnrealClaudeEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Export native UE thumbnails for in-house assets listed in the JSONL index.
	 * Exposed to Python/Blueprint so indexing can reuse the same thumbnail path
	 * as the candidate panel without adding a new MCP tool.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealClaude|Retrieval")
	static FString ExportInhouseThumbnails(
		const FString& IndexJsonlPath,
		const FString& OutputDir = TEXT(""),
		bool bOverwrite = false,
		int32 Limit = 0,
		const FString& TargetUID = TEXT("")
	);
};
