// Copyright Natali Caggiano. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "AssetThumbnail.h"

class SScrollBox;
class SHorizontalBox;
class SVerticalBox;

/** Single asset candidate returned by the retrieval pipeline */
struct FAssetCandidate
{
	int32  Rank         = 0;
	FString UID;
	float  Score        = 0.f;
	FString Name;
	FString Description;
	FString MeshPath;           // local GLTF path — empty = not yet downloaded
	FString ThumbnailPath;      // local cached thumbnail — empty = unavailable
	FString SketchfabUrl;
	FString ContentPath;        // UE Content Browser path — in-house assets only
	bool    bIsDownloadable = false;
	bool    bIsInhouse      = false;
};

/**
 * Horizontal scrollable panel showing asset retrieval candidates.
 * Each card: thumbnail + name + score bar + Place/Import button.
 *
 * In-house thumbnails are rendered via FAssetThumbnail (same as Content Browser).
 * Sketchfab thumbnails are loaded from disk (ThumbnailPath).
 *
 * Usage:
 *   Panel->SetCandidates(candidates);               // replace (no group label)
 *   Panel->SetGroup("bench", candidates);           // replace (with label)
 *   Panel->AppendGroup("street lamp", candidates);  // append
 *   Panel->ClearCandidates();
 */
class SAssetCandidatesPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssetCandidatesPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Populate the panel with candidates and make it visible (legacy, no group label) */
	void SetCandidates(const TArray<FAssetCandidate>& Candidates);

	/** Clear panel and show candidates with group label */
	void SetGroup(const FString& ObjectName, const TArray<FAssetCandidate>& Candidates);

	/** Append candidates without clearing */
	void AppendGroup(const FString& ObjectName, const TArray<FAssetCandidate>& Candidates);

	/** Clear all candidates and collapse panel */
	void ClearCandidates();

private:
	void RebuildCards();
	TSharedRef<SWidget> BuildCard(const FAssetCandidate& Candidate, int32 Index);

	TSharedPtr<FSlateDynamicImageBrush> LoadThumbnailBrush(const FString& FilePath, const FString& UID) const;

	FReply HandleImportClicked(const FAssetCandidate& Candidate);

	void ExecuteImportScript(const FString& MeshPath, const FString& AssetName, const FString& UID);
	void ExecutePlaceInhouseScript(const FString& ContentPath, const FString& AssetName);

private:
	TSharedPtr<SHorizontalBox>  CardStrip;
	TSharedPtr<SWidget>         RootBorder;

	TArray<FAssetCandidate>                        CurrentCandidates;

	// Disk-based thumbnails (Sketchfab)
	TArray<TSharedPtr<FSlateDynamicImageBrush>>    ThumbnailBrushes;

	// UE Content Browser thumbnails (in-house assets via FAssetThumbnail)
	TArray<TSharedPtr<FAssetThumbnail>>            AssetThumbnails;
	TSharedPtr<FAssetThumbnailPool>                ThumbnailPool;

	static constexpr float CardWidth   = 160.f;
	static constexpr float CardSpacing = 8.f;
	static constexpr float ThumbWidth  = 152.f;
	static constexpr float ThumbHeight = 86.f;
};
