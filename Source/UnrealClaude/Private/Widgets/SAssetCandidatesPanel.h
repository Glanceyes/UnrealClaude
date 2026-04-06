// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Brushes/SlateDynamicImageBrush.h"

class SScrollBox;
class SHorizontalBox;

/** Single asset candidate returned by the retrieval pipeline */
struct FAssetCandidate
{
	int32  Rank         = 0;
	FString UID;
	float  Score        = 0.f;
	FString Name;
	FString Description;
	FString MeshPath;       // local GLTF path — empty = not yet downloaded
	FString ThumbnailPath;  // local cached thumbnail — empty = unavailable
	FString SketchfabUrl;
};

/**
 * Horizontal scrollable panel showing asset retrieval candidates.
 * Each card: thumbnail + name + score bar + Import button.
 *
 * Usage:
 *   Panel->SetCandidates(CandidateArray);   // populates and makes visible
 *   Panel->ClearCandidates();               // collapses panel
 */
class SAssetCandidatesPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetCandidatesPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Populate the panel with candidates and make it visible */
	void SetCandidates(const TArray<FAssetCandidate>& Candidates);

	/** Clear candidates and collapse panel */
	void ClearCandidates();

private:
	/** Rebuild the card strip from CurrentCandidates */
	void RebuildCards();

	/** Build a single candidate card widget */
	TSharedRef<SWidget> BuildCard(const FAssetCandidate& Candidate, int32 Index);

	/** Load thumbnail image from disk and return Slate brush */
	TSharedPtr<FSlateDynamicImageBrush> LoadThumbnailBrush(const FString& FilePath, const FString& UID) const;

	/** Handle Import button click for a candidate */
	FReply HandleImportClicked(const FAssetCandidate& Candidate);

	/** Execute Python code inside the UE editor to import + place the asset */
	void ExecuteImportScript(const FString& MeshPath, const FString& AssetName);

private:
	TSharedPtr<SHorizontalBox>  CardStrip;
	TSharedPtr<SWidget>         RootBorder;

	TArray<FAssetCandidate>                        CurrentCandidates;
	TArray<TSharedPtr<FSlateDynamicImageBrush>>    ThumbnailBrushes;

	static constexpr float CardWidth     = 160.f;
	static constexpr float CardSpacing   = 8.f;
	static constexpr float ThumbWidth    = 152.f;
	static constexpr float ThumbHeight   = 86.f;
};
