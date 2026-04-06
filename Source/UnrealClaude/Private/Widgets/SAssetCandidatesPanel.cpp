// Copyright Natali Caggiano. All Rights Reserved.

#include "SAssetCandidatesPanel.h"
#include "UnrealClaudeModule.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Editor.h"       // GEditor

#define LOCTEXT_NAMESPACE "UnrealClaude"

// ─────────────────────────────────────────────────────────────────────────────
// Construct
// ─────────────────────────────────────────────────────────────────────────────

void SAssetCandidatesPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(RootBorder, SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.07f, 0.07f, 0.10f, 1.f))
		.Padding(FMargin(8.f, 6.f))
		.Visibility(EVisibility::Collapsed)
		[
			SNew(SVerticalBox)

			// ── Header row ──────────────────────────────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 6.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CandidatesTitle", "Asset Candidates"))
					.TextStyle(FAppStyle::Get(), "SmallText")
					.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.6f, 0.3f)))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.Text(LOCTEXT("ClosePanel", "✕"))
					.TextStyle(FAppStyle::Get(), "SmallText")
					.OnClicked_Lambda([this]()
					{
						ClearCandidates();
						return FReply::Handled();
					})
				]
			]

			// ── Card strip (horizontal scroll) ───────────────────────────
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)

				+ SScrollBox::Slot()
				[
					SAssignNew(CardStrip, SHorizontalBox)
				]
			]
		]
	];
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void SAssetCandidatesPanel::SetCandidates(const TArray<FAssetCandidate>& Candidates)
{
	CurrentCandidates = Candidates;
	RebuildCards();

	if (RootBorder.IsValid())
	{
		RootBorder->SetVisibility(
			Candidates.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed
		);
	}
}

void SAssetCandidatesPanel::ClearCandidates()
{
	CurrentCandidates.Empty();
	ThumbnailBrushes.Empty();
	if (CardStrip.IsValid())
	{
		CardStrip->ClearChildren();
	}
	if (RootBorder.IsValid())
	{
		RootBorder->SetVisibility(EVisibility::Collapsed);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Private — card building
// ─────────────────────────────────────────────────────────────────────────────

void SAssetCandidatesPanel::RebuildCards()
{
	if (!CardStrip.IsValid()) return;
	CardStrip->ClearChildren();
	ThumbnailBrushes.Empty();

	for (int32 i = 0; i < CurrentCandidates.Num(); ++i)
	{
		// Pre-load thumbnail brush
		TSharedPtr<FSlateDynamicImageBrush> Brush;
		if (!CurrentCandidates[i].ThumbnailPath.IsEmpty())
		{
			Brush = LoadThumbnailBrush(CurrentCandidates[i].ThumbnailPath, CurrentCandidates[i].UID);
		}
		ThumbnailBrushes.Add(Brush);

		CardStrip->AddSlot()
		.AutoWidth()
		.Padding(i > 0 ? CardSpacing : 0.f, 0.f, 0.f, 0.f)
		[
			BuildCard(CurrentCandidates[i], i)
		];
	}
}

TSharedRef<SWidget> SAssetCandidatesPanel::BuildCard(const FAssetCandidate& Candidate, int32 Index)
{
	const bool  bHasLocal   = !Candidate.MeshPath.IsEmpty();
	const bool  bHasThumb   = ThumbnailBrushes.IsValidIndex(Index) && ThumbnailBrushes[Index].IsValid();
	const int32 ScorePct    = FMath::RoundToInt(Candidate.Score * 100.f);

	// Score color: green ≥ 55, orange ≥ 35, red < 35
	FLinearColor ScoreColor =
		ScorePct >= 55 ? FLinearColor(0.3f, 0.85f, 0.4f) :
		ScorePct >= 35 ? FLinearColor(1.0f, 0.65f, 0.1f) :
		                 FLinearColor(0.9f, 0.3f, 0.3f);

	// Truncate name to fit card width
	FString DisplayName = Candidate.Name;
	if (DisplayName.Len() > 20)
	{
		DisplayName = DisplayName.Left(18) + TEXT("…");
	}

	return SNew(SBox)
		.WidthOverride(CardWidth)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.16f, 1.f))
			.Padding(FMargin(4.f))
			[
				SNew(SVerticalBox)

				// ── Thumbnail with rank badge ──────────────────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SOverlay)

					// Thumbnail image (or placeholder)
					+ SOverlay::Slot()
					[
						SNew(SBox)
						.WidthOverride(ThumbWidth)
						.HeightOverride(ThumbHeight)
						[
							bHasThumb
							? StaticCastSharedRef<SWidget>(
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
								.HAlign(HAlign_Fill)
								.VAlign(VAlign_Fill)
								[
									SNew(SImage)
									.Image(ThumbnailBrushes[Index].Get())
								]
							)
							: StaticCastSharedRef<SWidget>(
								SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
								.BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f))
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								[
									SNew(STextBlock)
									.Text(LOCTEXT("NoThumb", "No Preview"))
									.TextStyle(FAppStyle::Get(), "SmallText")
									.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
								]
							)
						]
					]

					// Rank badge (top-left)
					+ SOverlay::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					.Padding(2.f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.7f))
						.Padding(FMargin(4.f, 2.f))
						[
							SNew(STextBlock)
							.Text(FText::AsNumber(Candidate.Rank))
							.TextStyle(FAppStyle::Get(), "SmallText")
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
						]
					]

					// "Local" badge (top-right, only when GLTF is on disk)
					+ SOverlay::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Top)
					.Padding(2.f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.1f, 0.45f, 0.1f, 0.85f))
						.Padding(FMargin(3.f, 1.f))
						.Visibility(bHasLocal ? EVisibility::Visible : EVisibility::Collapsed)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("LocalBadge", "✓"))
							.TextStyle(FAppStyle::Get(), "SmallText")
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
						]
					]
				]

				// ── Asset name ────────────────────────────────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 4.f, 0.f, 2.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(DisplayName))
					.TextStyle(FAppStyle::Get(), "SmallText")
					.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f)))
					.ToolTipText(FText::FromString(Candidate.Name))
				]

				// ── Score bar ─────────────────────────────────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(SHorizontalBox)

					// Filled portion
					+ SHorizontalBox::Slot()
					.FillWidth(Candidate.Score)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(ScoreColor)
						.Padding(FMargin(0.f, 2.f))
						[
							SNullWidget::NullWidget
						]
					]

					// Empty portion
					+ SHorizontalBox::Slot()
					.FillWidth(1.f - Candidate.Score)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f))
						.Padding(FMargin(0.f, 2.f))
						[
							SNullWidget::NullWidget
						]
					]

					// Score label
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f, 0.f, 0.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%d%%"), ScorePct)))
						.TextStyle(FAppStyle::Get(), "SmallText")
						.ColorAndOpacity(FSlateColor(ScoreColor))
					]
				]

				// ── Import button ─────────────────────────────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.IsEnabled(bHasLocal)
					.ToolTipText(bHasLocal
						? FText::FromString(FString::Printf(TEXT("Import: %s"), *Candidate.Name))
						: LOCTEXT("NoLocalFile", "GLTF not downloaded — run with --download flag"))
					.OnClicked_Lambda([this, Candidate]()
					{
						return HandleImportClicked(Candidate);
					})
					[
						SNew(STextBlock)
						.Text(bHasLocal
							? LOCTEXT("ImportBtn", "Import")
							: LOCTEXT("DownloadBtn", "No GLTF"))
						.TextStyle(FAppStyle::Get(), "SmallText")
						.ColorAndOpacity(FSlateColor(
							bHasLocal ? FLinearColor(0.3f, 0.85f, 0.4f) : FLinearColor(0.5f, 0.5f, 0.5f)
						))
					]
				]
			]
		];
}

// ─────────────────────────────────────────────────────────────────────────────
// Private — thumbnail loading
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FSlateDynamicImageBrush> SAssetCandidatesPanel::LoadThumbnailBrush(
	const FString& FilePath, const FString& UID) const
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("SAssetCandidatesPanel: cannot load thumbnail: %s"), *FilePath);
		return nullptr;
	}

	IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	EImageFormat Fmt = Mod.DetectImageFormat(FileData.GetData(), FileData.Num());
	TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(Fmt);
	if (!Wrapper.IsValid()) return nullptr;

	if (!Wrapper->SetCompressed(FileData.GetData(), FileData.Num())) return nullptr;

	TArray<uint8> RawData;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawData)) return nullptr;

	const int32 W = Wrapper->GetWidth();
	const int32 H = Wrapper->GetHeight();
	if (W <= 0 || H <= 0) return nullptr;

	FName BrushName(*FString::Printf(TEXT("AssetThumb_%s"), *UID));
	return FSlateDynamicImageBrush::CreateWithImageData(
		BrushName,
		FVector2D(static_cast<float>(W), static_cast<float>(H)),
		RawData
	);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private — import
// ─────────────────────────────────────────────────────────────────────────────

FReply SAssetCandidatesPanel::HandleImportClicked(const FAssetCandidate& Candidate)
{
	ExecuteImportScript(Candidate.MeshPath, Candidate.Name);
	return FReply::Handled();
}

void SAssetCandidatesPanel::ExecuteImportScript(const FString& MeshPath, const FString& AssetName)
{
	if (MeshPath.IsEmpty())
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("SAssetCandidatesPanel: no mesh path for import"));
		return;
	}

	// Escape backslashes for Python raw string embedding
	FString SafePath = MeshPath.Replace(TEXT("\\"), TEXT("\\\\"));
	FString SafeName = AssetName.Replace(TEXT("'"), TEXT("\\'"));

	FString PythonCode = FString::Printf(
		TEXT(
			"# @UnrealClaude Script\n"
			"# @Name: ImportRetrievedAsset\n"
			"# @Description: Import retrieved asset '%s' into Content Browser and place in level\n"
			"import unreal\n"
			"mesh_path = '%s'\n"
			"ue_dest = '/Game/RetrievedAssets'\n"
			"task = unreal.AssetImportTask()\n"
			"task.set_editor_property('filename', mesh_path)\n"
			"task.set_editor_property('destination_path', ue_dest)\n"
			"task.set_editor_property('automated', True)\n"
			"task.set_editor_property('save', True)\n"
			"task.set_editor_property('replace_existing', True)\n"
			"unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])\n"
			"imported = task.get_editor_property('imported_object_paths')\n"
			"if imported:\n"
			"    asset = unreal.load_asset(imported[0])\n"
			"    loc = unreal.Vector(0, 0, 0)\n"
			"    actor = unreal.EditorLevelLibrary.spawn_actor_from_object(asset, loc, unreal.Rotator(0,0,0))\n"
			"    print(f'[UnrealClaude] Placed: {actor.get_actor_label()}' if actor else '[UnrealClaude] Spawn failed')\n"
			"else:\n"
			"    print('[UnrealClaude] Import may have succeeded — check Content Browser at /Game/RetrievedAssets')\n"
			"print('[UnrealClaude] Import done.')\n"
		),
		*SafeName,
		*SafePath
	);

	// Write to a temp script file
	FString ScriptDir  = FPaths::ProjectSavedDir() / TEXT("UnrealClaude/Scripts");
	FString ScriptPath = ScriptDir / TEXT("import_candidate.py");
	IPlatformFile& PF  = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*ScriptDir);

	if (!FFileHelper::SaveStringToFile(PythonCode, *ScriptPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogUnrealClaude, Error, TEXT("SAssetCandidatesPanel: failed to write import script to %s"), *ScriptPath);
		return;
	}

	// Execute via editor Python console
	if (GEditor)
	{
		FString Command = FString::Printf(TEXT("py \"%s\""), *ScriptPath);
		GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);
		UE_LOG(LogUnrealClaude, Log, TEXT("SAssetCandidatesPanel: import script submitted: %s"), *ScriptPath);
	}
}

#undef LOCTEXT_NAMESPACE
