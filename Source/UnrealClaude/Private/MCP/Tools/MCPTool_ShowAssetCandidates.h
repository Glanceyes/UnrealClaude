// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCP/MCPToolBase.h"

/**
 * MCP Tool: show_asset_candidates
 *
 * Called by Claude after running retrieve_assets.py.
 * Parses the JSON results array and pushes them into
 * SAssetCandidatesPanel inside the active ClaudeEditorWidget.
 *
 * Claude usage:
 *   show_asset_candidates({ "results": <retrieve_assets JSON results array> })
 */
class FMCPTool_ShowAssetCandidates : public FMCPToolBase
{
public:
	virtual FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("show_asset_candidates");
		Info.Description = TEXT(
			"Display asset retrieval candidates directly inside the UnrealClaude panel.\n\n"
			"Call this after running retrieve_assets.py to show thumbnails, names, scores, "
			"and Import buttons for each candidate without opening an external browser.\n\n"
			"Pass the full 'results' array from retrieve_assets.py JSON output.\n\n"
			"The panel will appear above the input field. The user can click Import on any "
			"candidate to import the GLTF into /Game/RetrievedAssets and place it in the level."
		);
		Info.Parameters = {
			FMCPToolParameter(
				TEXT("results"),
				TEXT("string"),
				TEXT("JSON array of asset results from retrieve_assets.py (stringify the results field)"),
				true
			)
		};
		Info.Annotations = FMCPToolAnnotations::ReadOnly();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;
};
