// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_ShowAssetCandidates.h"
#include "ClaudeEditorWidget.h"
#include "MCP/MCPToolBase.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FMCPToolResult FMCPTool_ShowAssetCandidates::Execute(const TSharedRef<FJsonObject>& Params)
{
	// ── 1. Extract 'results' parameter ──────────────────────────────────────
	FString ResultsJson;
	TOptional<FMCPToolResult> Error;
	if (!ExtractRequiredString(Params, TEXT("results"), ResultsJson, Error))
	{
		return Error.GetValue();
	}

	// ── 2. Parse JSON array ──────────────────────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> ResultsArray;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultsJson);
	if (!FJsonSerializer::Deserialize(Reader, ResultsArray))
	{
		// Maybe the caller passed a full output object — try extracting "results" key
		TSharedPtr<FJsonObject> OuterObj;
		TSharedRef<TJsonReader<>> ObjReader = TJsonReaderFactory<>::Create(ResultsJson);
		if (FJsonSerializer::Deserialize(ObjReader, OuterObj) && OuterObj.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* NestedArr = nullptr;
			if (OuterObj->TryGetArrayField(TEXT("results"), NestedArr))
			{
				ResultsArray = *NestedArr;
			}
		}
		if (ResultsArray.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("'results' must be a JSON array (or object with 'results' array)"));
		}
	}

	if (ResultsArray.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("Results array is empty"));
	}

	// ── 3. Convert to FAssetCandidate array ──────────────────────────────────
	TArray<FAssetCandidate> Candidates;
	for (const TSharedPtr<FJsonValue>& Val : ResultsArray)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val.IsValid() || !Val->TryGetObject(ObjPtr) || !ObjPtr) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FAssetCandidate C;
		C.Rank          = (int32)Obj->GetNumberField(TEXT("rank"));
		C.UID           = Obj->GetStringField(TEXT("uid"));
		C.Score         = (float)Obj->GetNumberField(TEXT("score"));
		C.Name          = Obj->GetStringField(TEXT("name"));
		C.Description   = Obj->GetStringField(TEXT("description"));
		C.MeshPath      = Obj->GetStringField(TEXT("mesh_path"));
		C.ThumbnailPath = Obj->GetStringField(TEXT("thumbnail_path"));
		C.SketchfabUrl  = Obj->GetStringField(TEXT("sketchfab_url"));
		Candidates.Add(MoveTemp(C));
	}

	if (Candidates.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("No valid candidates found in results array"));
	}

	// ── 4. Push to the active widget on the game thread ──────────────────────
	TSharedPtr<SClaudeEditorWidget> Widget = SClaudeEditorWidget::GetActive();
	if (!Widget.IsValid())
	{
		return FMCPToolResult::Error(TEXT("UnrealClaude panel is not open"));
	}

	// Slate must be updated on the game thread; MCP tools already run there via task queue
	Widget->ShowAssetCandidates(Candidates);

	return FMCPToolResult::Success(
		FString::Printf(TEXT("Showing %d asset candidates in the UnrealClaude panel"), Candidates.Num())
	);
}
