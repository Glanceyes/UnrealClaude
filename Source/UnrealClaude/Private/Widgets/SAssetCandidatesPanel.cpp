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
#include "AssetThumbnail.h"
#include "AssetRegistry/AssetRegistryModule.h"

#define LOCTEXT_NAMESPACE "UnrealClaude"

// ─────────────────────────────────────────────────────────────────────────────
// Construct
// ─────────────────────────────────────────────────────────────────────────────

void SAssetCandidatesPanel::Construct(const FArguments& InArgs)
{
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(32);

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

void SAssetCandidatesPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (ThumbnailPool.IsValid())
	{
		ThumbnailPool->Tick(InDeltaTime);
	}
}

void SAssetCandidatesPanel::SetGroup(const FString& /*ObjectName*/, const TArray<FAssetCandidate>& Candidates)
{
	// Group label UI not yet implemented — behaves like SetCandidates for now
	SetCandidates(Candidates);
}

void SAssetCandidatesPanel::AppendGroup(const FString& /*ObjectName*/, const TArray<FAssetCandidate>& Candidates)
{
	for (const FAssetCandidate& C : Candidates)
	{
		CurrentCandidates.Add(C);
	}
	RebuildCards();
	if (RootBorder.IsValid())
	{
		RootBorder->SetVisibility(EVisibility::Visible);
	}
}

void SAssetCandidatesPanel::ClearCandidates()
{
	CurrentCandidates.Empty();
	ThumbnailBrushes.Empty();
	AssetThumbnails.Empty();
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
	AssetThumbnails.Empty();

	// Lazy-init pool in case the widget was constructed from an older binary
	// (before ThumbnailPool was added to Construct) and then Live Coding patched it.
	if (!ThumbnailPool.IsValid())
	{
		ThumbnailPool = MakeShared<FAssetThumbnailPool>(32);
	}

	for (int32 i = 0; i < CurrentCandidates.Num(); ++i)
	{
		const FAssetCandidate& C = CurrentCandidates[i];

		// For in-house assets: use FAssetThumbnail (Content Browser system).
		// Use FindObject/LoadObject to bypass Asset Registry timing issues:
		// the background scanner may not have indexed all paths yet when the
		// panel opens, making GetAssetByObjectPath unreliable.
		TSharedPtr<FAssetThumbnail> InhouseThumb;
		if (C.bIsInhouse && !C.ContentPath.IsEmpty() && ThumbnailPool.IsValid())
		{
			FString AssetName    = FPaths::GetBaseFilename(C.ContentPath);
			FString FullObjPath  = C.ContentPath + TEXT(".") + AssetName;

			// Try in-memory first (instant), then load from disk
			UObject* Asset = FindObject<UObject>(nullptr, *FullObjPath);
			if (!Asset)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("SAssetCandidatesPanel: LoadObject for %s"), *FullObjPath);
				Asset = LoadObject<UObject>(nullptr, *FullObjPath);
			}
			if (Asset)
			{
				UE_LOG(LogUnrealClaude, Log, TEXT("SAssetCandidatesPanel: Got asset %s, making FAssetThumbnail"), *FullObjPath);
				FAssetData AssetData(Asset);
				InhouseThumb = MakeShared<FAssetThumbnail>(
					AssetData,
					static_cast<uint32>(ThumbWidth),
					static_cast<uint32>(ThumbHeight),
					ThumbnailPool
				);
			}
			else
			{
				UE_LOG(LogUnrealClaude, Warning, TEXT("SAssetCandidatesPanel: LoadObject FAILED for %s"), *FullObjPath);
			}
		}
		AssetThumbnails.Add(InhouseThumb);

		// For Sketchfab (or in-house fallback): disk thumbnail
		TSharedPtr<FSlateDynamicImageBrush> Brush;
		if (!C.ThumbnailPath.IsEmpty())
		{
			Brush = LoadThumbnailBrush(C.ThumbnailPath, C.UID);
		}
		ThumbnailBrushes.Add(Brush);

		CardStrip->AddSlot()
		.AutoWidth()
		.Padding(i > 0 ? CardSpacing : 0.f, 0.f, 0.f, 0.f)
		[
			BuildCard(C, i)
		];
	}
}

TSharedRef<SWidget> SAssetCandidatesPanel::BuildCard(const FAssetCandidate& Candidate, int32 Index)
{
	const bool  bIsInhouse       = Candidate.bIsInhouse;
	const bool  bHasLocal        = !Candidate.MeshPath.IsEmpty();
	const bool  bCanDownload     = Candidate.bIsDownloadable;
	const bool  bHasInhouseThumb = AssetThumbnails.IsValidIndex(Index) && AssetThumbnails[Index].IsValid();
	const bool  bHasDiskThumb    = ThumbnailBrushes.IsValidIndex(Index) && ThumbnailBrushes[Index].IsValid();
	const bool  bHasThumb        = bHasInhouseThumb || bHasDiskThumb;
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
							bHasInhouseThumb
							? StaticCastSharedRef<SWidget>(
								// In-house: use FAssetThumbnail (same as Content Browser)
								AssetThumbnails[Index]->MakeThumbnailWidget()
							)
							: bHasDiskThumb
							? StaticCastSharedRef<SWidget>(
								// Sketchfab: load from disk
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

				// ── Place / Import / Download button ─────────────────
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.IsEnabled(bIsInhouse || bHasLocal || bCanDownload)
					.ToolTipText(
						bIsInhouse
							? FText::FromString(FString::Printf(TEXT("Place in level: %s"), *Candidate.Name))
							: bHasLocal
								? FText::FromString(FString::Printf(TEXT("Import into Content Browser: %s"), *Candidate.Name))
								: bCanDownload
									? FText::FromString(FString::Printf(TEXT("Download from Sketchfab & import: %s"), *Candidate.Name))
									: LOCTEXT("NotDownloadable", "Not downloadable via API"))
					.OnClicked_Lambda([this, Candidate]()
					{
						return HandleImportClicked(Candidate);
					})
					[
						SNew(STextBlock)
						.Text(bIsInhouse
							? LOCTEXT("PlaceBtn",    "Place")
							: bHasLocal
								? LOCTEXT("ImportBtn",   "Import")
								: bCanDownload
									? LOCTEXT("DownloadBtn", "\u2193 Download & Import")
									: LOCTEXT("NoDownload",  "Not Available"))
						.TextStyle(FAppStyle::Get(), "SmallText")
						.ColorAndOpacity(FSlateColor(
							bIsInhouse   ? FLinearColor(0.9f, 0.75f, 0.2f) :   // yellow — already in project
							bHasLocal    ? FLinearColor(0.3f, 0.85f, 0.4f) :   // green
							bCanDownload ? FLinearColor(0.3f, 0.6f,  1.0f) :   // blue
							               FLinearColor(0.4f, 0.4f,  0.4f)     // grey
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
	if (Candidate.bIsInhouse)
	{
		ExecutePlaceInhouseScript(Candidate.ContentPath, Candidate.Name);
	}
	else
	{
		ExecuteImportScript(Candidate.MeshPath, Candidate.Name, Candidate.UID);
	}
	return FReply::Handled();
}

void SAssetCandidatesPanel::ExecuteImportScript(const FString& MeshPath, const FString& AssetName, const FString& UID)
{
	// Escape single quotes for Python string embedding
	FString SafePath = MeshPath.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'"));
	FString SafeName = AssetName.Replace(TEXT("'"), TEXT("\\'"));
	FString SafeUID  = UID.Replace(TEXT("'"), TEXT("\\'"));

	// Python script: optionally download GLTF first, then import and place in level
	FString PythonCode = FString::Printf(
		TEXT(
			"# @UnrealClaude Script\n"
			"# @Name: ImportRetrievedAsset\n"
			"# @Description: Import retrieved asset '%s' into Content Browser and place in level\n"
			"import unreal, subprocess, sys, json, os\n"
			"\n"
			"mesh_path = r'%s'\n"
			"uid       = '%s'\n"
			"ue_dest   = '/Game/RetrievedAssets'\n"
			"\n"
			"# ── Step 1: Download GLTF if not already on disk ─────────────────\n"
			"if not mesh_path or not os.path.exists(mesh_path):\n"
			"    print(f'[UnrealClaude] Downloading GLTF for uid={uid} ...')\n"
			"    py_exe  = r'C:/Users/glanceyes/AppData/Local/Programs/Python/Python312/python.exe'\n"
			"    script  = r'C:/Users/glanceyes/Projects/AAAScene/glanceyes_260403/Python/retrieve_assets.py'\n"
			"    result  = subprocess.run(\n"
			"        [py_exe, script, '--download-uid', uid],\n"
			"        capture_output=True, text=True, encoding='utf-8',\n"
			"        creationflags=0x08000000  # CREATE_NO_WINDOW\n"
			"    )\n"
			"    if result.returncode != 0:\n"
			"        err_text = result.stderr\n"
			"        if '403' in err_text:\n"
			"            sketchfab_url = 'https://sketchfab.com/models/' + uid\n"
			"            print(f'[UnrealClaude] This model is not downloadable via Sketchfab API (403 Forbidden).')\n"
			"            print(f'[UnrealClaude] Download it manually at: {sketchfab_url}')\n"
			"        else:\n"
			"            print('[UnrealClaude] Download failed:', err_text[:300])\n"
			"    else:\n"
			"        try:\n"
			"            data = json.loads(result.stdout)\n"
			"            mesh_path = data.get('mesh_path', '')\n"
			"            print(f'[UnrealClaude] Downloaded to: {mesh_path}')\n"
			"        except Exception as e:\n"
			"            print(f'[UnrealClaude] Failed to parse download result: {e}')\n"
			"\n"
			"if not mesh_path or not os.path.exists(mesh_path):\n"
			"    print('[UnrealClaude] Cannot import: GLTF file unavailable. See log above.')\n"
			"else:\n"
			"    # ── Step 2: Import into Content Browser ──────────────────────────\n"
			"    task = unreal.AssetImportTask()\n"
			"    task.set_editor_property('filename', mesh_path)\n"
			"    task.set_editor_property('destination_path', ue_dest)\n"
			"    task.set_editor_property('automated', True)\n"
			"    task.set_editor_property('save', True)\n"
			"    task.set_editor_property('replace_existing', True)\n"
			"    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])\n"
			"\n"
			"    # ── Step 3: Collect only StaticMeshes from THIS import ───────────\n"
			"    imported = task.get_editor_property('imported_object_paths')\n"
			"    print(f'[UnrealClaude] Import task returned {len(imported)} object(s)')\n"
			"    mesh_assets = []\n"
			"    for path in imported:\n"
			"        try:\n"
			"            a = unreal.load_asset(path)\n"
			"            if a and a.get_class().get_name() == 'StaticMesh':\n"
			"                mesh_assets.append(a)\n"
			"        except Exception as e:\n"
			"            print(f'[UnrealClaude] Could not load {path}: {e}')\n"
			"    print(f'[UnrealClaude] Found {len(mesh_assets)} StaticMesh(es) in this import')\n"
			"\n"
			"    # ── Step 4: Spawn position in front of viewport camera ────────────\n"
			"    try:\n"
			"        ue_sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)\n"
			"        cam_loc, cam_rot = ue_sub.get_level_viewport_camera_info()\n"
			"        fwd = cam_rot.get_forward_vector()\n"
			"        base_loc = unreal.Vector(\n"
			"            cam_loc.x + fwd.x * 500.0,\n"
			"            cam_loc.y + fwd.y * 500.0,\n"
			"            cam_loc.z\n"
			"        )\n"
			"        print(f'[UnrealClaude] Spawn at: {base_loc}')\n"
			"    except Exception as e:\n"
			"        print(f'[UnrealClaude] Camera info failed ({e}), using world origin')\n"
			"        base_loc = unreal.Vector(0.0, 0.0, 0.0)\n"
			"\n"
			"    # ── Step 5: Place all parts at the SAME location ─────────────────\n"
			"    # Parts of the same GLTF (trunk, leaves, etc.) must share the base location\n"
			"    # so their geometry assembles correctly.\n"
			"    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n"
			"    placed = 0\n"
			"    for sm in mesh_assets:\n"
			"        actor = actor_sub.spawn_actor_from_class(\n"
			"            unreal.StaticMeshActor, base_loc, unreal.Rotator(0, 0, 0)\n"
			"        )\n"
			"        if actor:\n"
			"            actor.static_mesh_component.set_static_mesh(sm)\n"
			"            print(f'[UnrealClaude] Placed: {actor.get_actor_label()}')\n"
			"            placed += 1\n"
			"    if placed == 0:\n"
			"        print('[UnrealClaude] No StaticMesh placed — check /Game/RetrievedAssets in Content Browser')\n"
			"\n"
			"print('[UnrealClaude] Import done.')\n"
		),
		*SafeName,
		*SafePath,
		*SafeUID
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

void SAssetCandidatesPanel::ExecutePlaceInhouseScript(const FString& ContentPath, const FString& AssetName)
{
	if (ContentPath.IsEmpty())
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("SAssetCandidatesPanel: no content_path for in-house asset"));
		return;
	}

	FString SafePath = ContentPath.Replace(TEXT("'"), TEXT("\\'"));
	FString SafeName = AssetName.Replace(TEXT("'"), TEXT("\\'"));

	FString PythonCode = FString::Printf(
		TEXT(
			"# @UnrealClaude Script\n"
			"# @Name: PlaceInhouseAsset\n"
			"# @Description: Place in-house asset '%s' from Content Browser into level\n"
			"import unreal\n"
			"\n"
			"# /BlueClient2/ is already mounted by UnrealClaude plugin at startup\n"
			"content_path = '%s'\n"
			"print(f'[UnrealClaude] Loading in-house asset: {content_path}')\n"
			"\n"
			"sm = unreal.load_asset(content_path)\n"
			"if not sm:\n"
			"    print(f'[UnrealClaude] ERROR: Could not load asset at {content_path}')\n"
			"    print('[UnrealClaude] /BlueClient2/ is mounted by the plugin at startup')\n"
			"else:\n"
			"    try:\n"
			"        ue_sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)\n"
			"        cam_loc, cam_rot = ue_sub.get_level_viewport_camera_info()\n"
			"        fwd = cam_rot.get_forward_vector()\n"
			"        loc = unreal.Vector(\n"
			"            cam_loc.x + fwd.x * 500.0,\n"
			"            cam_loc.y + fwd.y * 500.0,\n"
			"            cam_loc.z\n"
			"        )\n"
			"    except Exception as e:\n"
			"        print(f'[UnrealClaude] Camera info failed ({e}), using world origin')\n"
			"        loc = unreal.Vector(0.0, 0.0, 0.0)\n"
			"\n"
			"    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)\n"
			"    actor = actor_sub.spawn_actor_from_class(\n"
			"        unreal.StaticMeshActor, loc, unreal.Rotator(0, 0, 0)\n"
			"    )\n"
			"    if actor:\n"
			"        actor.static_mesh_component.set_static_mesh(sm)\n"
			"        print(f'[UnrealClaude] Placed: {actor.get_actor_label()} at {loc}')\n"
			"    else:\n"
			"        print('[UnrealClaude] Spawn failed')\n"
			"\n"
			"print('[UnrealClaude] Done.')\n"
		),
		*SafeName,
		*SafePath
	);

	FString ScriptDir  = FPaths::ProjectSavedDir() / TEXT("UnrealClaude/Scripts");
	FString ScriptPath = ScriptDir / TEXT("place_inhouse.py");
	IPlatformFile& PF  = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*ScriptDir);

	if (!FFileHelper::SaveStringToFile(PythonCode, *ScriptPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogUnrealClaude, Error, TEXT("SAssetCandidatesPanel: failed to write place script"));
		return;
	}

	if (GEditor)
	{
		FString Command = FString::Printf(TEXT("py \"%s\""), *ScriptPath);
		GEditor->Exec(GEditor->GetEditorWorldContext().World(), *Command);
		UE_LOG(LogUnrealClaude, Log, TEXT("SAssetCandidatesPanel: place script submitted: %s"), *ScriptPath);
	}
}

#undef LOCTEXT_NAMESPACE
