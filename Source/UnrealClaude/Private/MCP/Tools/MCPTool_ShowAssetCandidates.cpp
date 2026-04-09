// Copyright Natali Caggiano. All Rights Reserved.

#include "MCPTool_ShowAssetCandidates.h"
#include "ClaudeEditorWidget.h"
#include "MCP/MCPToolBase.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"

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
		C.Rank            = (int32)Obj->GetNumberField(TEXT("rank"));
		C.UID             = Obj->GetStringField(TEXT("uid"));
		C.Score           = (float)Obj->GetNumberField(TEXT("score"));
		C.Name            = Obj->GetStringField(TEXT("name"));
		C.Description     = Obj->GetStringField(TEXT("description"));
		C.MeshPath        = Obj->GetStringField(TEXT("mesh_path"));
		C.ThumbnailPath   = Obj->GetStringField(TEXT("thumbnail_path"));
		C.SketchfabUrl    = Obj->GetStringField(TEXT("sketchfab_url"));
		C.ContentPath     = Obj->GetStringField(TEXT("content_path"));
		C.bIsDownloadable = Obj->GetBoolField(TEXT("is_downloadable"));
		{
			FString Source = Obj->GetStringField(TEXT("source"));
			C.bIsInhouse = (Source == TEXT("inhouse"));
		}
		Candidates.Add(MoveTemp(C));
	}

	if (Candidates.IsEmpty())
	{
		return FMCPToolResult::Error(TEXT("No valid candidates found in results array"));
	}

	// ── 4. Sketchfab-only: populate ThumbnailPath from local cache ────────────
	//
	//   In-house thumbnails are rendered directly inside SAssetCandidatesPanel
	//   via FAssetThumbnail + FAssetThumbnailPool — the same system used by the
	//   UE Content Browser. No Python, no SceneCapture2D, no disk I/O here.
	//
	//   For Sketchfab candidates, we only check whether a cached .jpg already
	//   exists from a prior fetch; actual fetching happens in retrieve_assets.py.
	{
		FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
		FString ThumbDir    = UserProfile / TEXT(".unrealclaude/thumbs");
		for (FAssetCandidate& C : Candidates)
		{
			if (C.bIsInhouse || !C.ThumbnailPath.IsEmpty()) continue;
			FString CachedJpg = ThumbDir / FString::Printf(TEXT("%s.jpg"), *C.UID);
			if (FPaths::FileExists(CachedJpg))
			{
				C.ThumbnailPath = CachedJpg;
			}
		}
	}

	// ── 5. Extract optional object_name and append parameters ────────────────
	FString ObjectName;
	Params->TryGetStringField(TEXT("object_name"), ObjectName);

	bool bAppend = false;
	Params->TryGetBoolField(TEXT("append"), bAppend);

	// ── 6. Push to the active widget on the game thread ──────────────────────
	TSharedPtr<SClaudeEditorWidget> Widget = SClaudeEditorWidget::GetActive();
	if (!Widget.IsValid())
	{
		return FMCPToolResult::Error(TEXT("UnrealClaude panel is not open"));
	}

	if (bAppend)
	{
		Widget->AppendAssetGroup(ObjectName, Candidates);
	}
	else if (!ObjectName.IsEmpty())
	{
		Widget->ShowAssetGroup(ObjectName, Candidates);
	}
	else
	{
		Widget->ShowAssetCandidates(Candidates);
	}

	FString ModeStr  = bAppend ? TEXT("appended group") : TEXT("new panel");
	FString LabelStr = ObjectName.IsEmpty() ? TEXT("") : FString::Printf(TEXT(" ('%s')"), *ObjectName);
	return FMCPToolResult::Success(
		FString::Printf(TEXT("Showing %d asset candidates%s in the UnrealClaude panel (%s)"),
			Candidates.Num(), *LabelStr, *ModeStr)
	);
}
