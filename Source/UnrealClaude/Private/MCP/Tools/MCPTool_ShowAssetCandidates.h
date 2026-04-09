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
 *   show_asset_candidates({
 *     "results":     <retrieve_assets JSON results array>,
 *     "object_name": "bench",        // optional group label
 *     "append":      false           // true = add group, false = replace panel
 *   })
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
			"and Import/Place buttons for each candidate.\n\n"
			"For multi-object workflows (image with multiple objects), call this once per object:\n"
			"  - First object: omit 'append' (or set false) — clears the panel and starts a new group\n"
			"  - Each subsequent object: set 'append': true — adds a new group row without clearing\n\n"
			"The 'object_name' label appears as the group header (e.g. 'bench', 'street lamp').\n"
			"Users select one card per group and click 'Place All Selected' to place everything at once.\n\n"
			"Pass the full 'results' array from retrieve_assets.py JSON output."
		);
		Info.Parameters = {
			FMCPToolParameter(
				TEXT("results"),
				TEXT("string"),
				TEXT("JSON array of asset results from retrieve_assets.py (stringify the results field)"),
				true
			),
			FMCPToolParameter(
				TEXT("object_name"),
				TEXT("string"),
				TEXT("Label for this group of candidates (e.g. 'bench', 'street lamp'). Shown as the group header in the panel."),
				false
			),
			FMCPToolParameter(
				TEXT("append"),
				TEXT("boolean"),
				TEXT("If true, append as a new group to the existing panel instead of replacing it. Use false (default) for the first object, true for subsequent objects."),
				false
			)
		};
		Info.Annotations = FMCPToolAnnotations::ReadOnly();
		return Info;
	}

	virtual FMCPToolResult Execute(const TSharedRef<FJsonObject>& Params) override;
};
