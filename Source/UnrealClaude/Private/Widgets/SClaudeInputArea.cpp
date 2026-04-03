// Copyright Natali Caggiano. All Rights Reserved.

#include "SClaudeInputArea.h"
#include "ClipboardImageUtils.h"
#include "UnrealClaudeConstants.h"
#include "UnrealClaudeModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/FileHelper.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "UnrealClaude"

void SClaudeInputArea::Construct(const FArguments& InArgs)
{
	bIsWaiting = InArgs._bIsWaiting;
	OnSend = InArgs._OnSend;
	OnCancel = InArgs._OnCancel;
	OnTextChangedDelegate = InArgs._OnTextChanged;
	OnImagesChangedDelegate = InArgs._OnImagesChanged;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Image preview strip (starts collapsed, horizontal scroll)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SAssignNew(ImagePreviewStrip, SHorizontalBox)
			.Visibility(EVisibility::Collapsed)
		]

		// Input row
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)

			// Input text box with scroll support
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SBox)
				.MinDesiredHeight(60.0f)
				.MaxDesiredHeight(300.0f)
				[
					SNew(SScrollBox)
					.Orientation(Orient_Vertical)
					+ SScrollBox::Slot()
					[
						SAssignNew(InputTextBox, SMultiLineEditableTextBox)
						.HintText(LOCTEXT("InputHint", "Ask Claude about Unreal Engine 5.7... (Shift+Enter for newline)"))
						.AutoWrapText(true)
						.AllowMultiLine(true)
						.OnTextChanged(this, &SClaudeInputArea::HandleTextChanged)
						.OnTextCommitted(this, &SClaudeInputArea::HandleTextCommitted)
						.OnKeyDownHandler(this, &SClaudeInputArea::OnInputKeyDown)
						.IsEnabled_Lambda([this]() { return !bIsWaiting.Get(); })
					]
				]
			]

			// Buttons column
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			.VAlign(VAlign_Bottom)
			[
				SNew(SVerticalBox)

				// Browse image file button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Browse", "Browse"))
					.OnClicked(this, &SClaudeInputArea::HandleBrowseClicked)
					.ToolTipText(LOCTEXT("BrowseTip", "Attach an image file (PNG, JPG)"))
					.IsEnabled_Lambda([this]() { return !bIsWaiting.Get(); })
				]

				// Paste from clipboard button
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Paste", "Paste"))
					.OnClicked(this, &SClaudeInputArea::HandlePasteClicked)
					.ToolTipText(LOCTEXT("PasteTip", "Paste text or image from clipboard"))
					.IsEnabled_Lambda([this]() { return !bIsWaiting.Get(); })
				]

				// Send/Cancel button
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.Text_Lambda([this]() { return bIsWaiting.Get() ? LOCTEXT("Cancel", "Cancel") : LOCTEXT("Send", "Send"); })
					.OnClicked(this, &SClaudeInputArea::HandleSendCancelClicked)
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				]
			]
		]

		// Character count indicator
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]()
			{
				int32 CharCount = CurrentInputText.Len();
				if (CharCount > 0)
				{
					return FText::Format(LOCTEXT("CharCount", "{0} chars"), FText::AsNumber(CharCount));
				}
				return FText::GetEmpty();
			})
			.TextStyle(FAppStyle::Get(), "SmallText")
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		]
	];
}

void SClaudeInputArea::SetText(const FString& NewText)
{
	CurrentInputText = NewText;
	if (InputTextBox.IsValid())
	{
		InputTextBox->SetText(FText::FromString(NewText));
	}
}

FString SClaudeInputArea::GetText() const
{
	return CurrentInputText;
}

void SClaudeInputArea::ClearText()
{
	CurrentInputText.Empty();
	if (InputTextBox.IsValid())
	{
		InputTextBox->SetText(FText::GetEmpty());
	}
	ClearAttachedImages();
}

bool SClaudeInputArea::HasAttachedImages() const
{
	return AttachedImagePaths.Num() > 0;
}

int32 SClaudeInputArea::GetAttachedImageCount() const
{
	return AttachedImagePaths.Num();
}

TArray<FString> SClaudeInputArea::GetAttachedImagePaths() const
{
	return AttachedImagePaths;
}

void SClaudeInputArea::ClearAttachedImages()
{
	AttachedImagePaths.Empty();
	ThumbnailBrushes.Empty();
	RebuildImagePreviewStrip();
}

void SClaudeInputArea::RemoveAttachedImage(int32 Index)
{
	if (AttachedImagePaths.IsValidIndex(Index))
	{
		AttachedImagePaths.RemoveAt(Index);
		ThumbnailBrushes.RemoveAt(Index);
		RebuildImagePreviewStrip();
		OnImagesChangedDelegate.ExecuteIfBound(AttachedImagePaths);
	}
}

FReply SClaudeInputArea::OnInputKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	// Ctrl+V: try image paste first, fall back to text paste
	if (InKeyEvent.GetKey() == EKeys::V && InKeyEvent.IsControlDown())
	{
		if (TryPasteImageFromClipboard())
		{
			return FReply::Handled();
		}
		// Return unhandled to let default text paste proceed
		return FReply::Unhandled();
	}

	// Enter (without Shift) to send
	// Shift+Enter allows newline
	if (InKeyEvent.GetKey() == EKeys::Enter)
	{
		if (!InKeyEvent.IsShiftDown())
		{
			OnSend.ExecuteIfBound();
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void SClaudeInputArea::HandleTextChanged(const FText& NewText)
{
	CurrentInputText = NewText.ToString();
	OnTextChangedDelegate.ExecuteIfBound(CurrentInputText);
}

void SClaudeInputArea::HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	// Don't send on commit - use explicit Enter key handling
}

FReply SClaudeInputArea::HandlePasteClicked()
{
	// Try image paste first
	if (TryPasteImageFromClipboard())
	{
		return FReply::Handled();
	}

	// Fall back to text paste
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);
	if (!ClipboardText.IsEmpty() && InputTextBox.IsValid())
	{
		// Append to existing text
		FString NewText = CurrentInputText + ClipboardText;
		SetText(NewText);
	}
	return FReply::Handled();
}

FReply SClaudeInputArea::HandleBrowseClicked()
{
	using namespace UnrealClaudeConstants::ClipboardImage;

	if (AttachedImagePaths.Num() >= MaxImagesPerMessage)
	{
		UE_LOG(LogUnrealClaude, Log, TEXT("Browse rejected: already at max (%d images)"), MaxImagesPerMessage);
		return FReply::Handled();
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TArray<FString> SelectedFiles;
	const void* ParentWindowHandle = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr);
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("Select Image"),
		FPaths::GetPath(FPaths::ProjectDir()),
		TEXT(""),
		TEXT("Image Files (*.png;*.jpg;*.jpeg)|*.png;*.jpg;*.jpeg"),
		EFileDialogFlags::None,
		SelectedFiles
	);

	if (!bOpened || SelectedFiles.Num() == 0)
	{
		return FReply::Handled();
	}

	const FString& SourcePath = SelectedFiles[0];
	const int64 FileSize = IFileManager::Get().FileSize(*SourcePath);
	if (FileSize <= 0)
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("Selected image is empty: %s"), *SourcePath);
		return FReply::Handled();
	}

	FClipboardImageUtils::CleanupOldScreenshots(
		FClipboardImageUtils::GetScreenshotDirectory(),
		UnrealClaudeConstants::ClipboardImage::MaxScreenshotAgeSeconds);

	FString DestPath;

	if (FileSize > MaxImageFileSize)
	{
		// Re-encode as JPEG to reduce size
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

		TArray<uint8> RawFileData;
		if (!FFileHelper::LoadFileToArray(RawFileData, *SourcePath))
		{
			UE_LOG(LogUnrealClaude, Warning, TEXT("Failed to load image: %s"), *SourcePath);
			return FReply::Handled();
		}

		EImageFormat DetectedFormat = ImageWrapperModule.DetectImageFormat(RawFileData.GetData(), RawFileData.Num());
		TSharedPtr<IImageWrapper> SrcWrapper = ImageWrapperModule.CreateImageWrapper(DetectedFormat);
		if (!SrcWrapper.IsValid() || !SrcWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
		{
			UE_LOG(LogUnrealClaude, Warning, TEXT("Failed to decode image: %s"), *SourcePath);
			return FReply::Handled();
		}

		TArray<uint8> RawPixels;
		if (!SrcWrapper->GetRaw(ERGBFormat::BGRA, 8, RawPixels))
		{
			UE_LOG(LogUnrealClaude, Warning, TEXT("Failed to get raw pixels: %s"), *SourcePath);
			return FReply::Handled();
		}

		int32 Width = SrcWrapper->GetWidth();
		int32 Height = SrcWrapper->GetHeight();

		// Scale down if still too large after JPEG compression estimate
		const float CompressionRatio = 0.08f; // JPEG ~8:1 for photos
		float ScaleFactor = FMath::Sqrt((float)MaxImageFileSize / (FileSize * CompressionRatio));
		if (ScaleFactor < 1.0f)
		{
			int32 NewWidth = FMath::Max(1, (int32)(Width * ScaleFactor));
			int32 NewHeight = FMath::Max(1, (int32)(Height * ScaleFactor));

			TArray<uint8> ScaledPixels;
			ScaledPixels.SetNumUninitialized(NewWidth * NewHeight * 4);
			for (int32 Y = 0; Y < NewHeight; Y++)
			{
				for (int32 X = 0; X < NewWidth; X++)
				{
					int32 SrcX = X * Width / NewWidth;
					int32 SrcY = Y * Height / NewHeight;
					int32 SrcIdx = (SrcY * Width + SrcX) * 4;
					int32 DstIdx = (Y * NewWidth + X) * 4;
					ScaledPixels[DstIdx + 0] = RawPixels[SrcIdx + 0];
					ScaledPixels[DstIdx + 1] = RawPixels[SrcIdx + 1];
					ScaledPixels[DstIdx + 2] = RawPixels[SrcIdx + 2];
					ScaledPixels[DstIdx + 3] = RawPixels[SrcIdx + 3];
				}
			}
			RawPixels = MoveTemp(ScaledPixels);
			Width = NewWidth;
			Height = NewHeight;
		}

		TSharedPtr<IImageWrapper> JpegWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
		JpegWrapper->SetRaw(RawPixels.GetData(), RawPixels.Num(), Width, Height, ERGBFormat::BGRA, 8);
		TArray64<uint8> Compressed = JpegWrapper->GetCompressed(85);

		FString DestFilename = FString::Printf(TEXT("attached_%lld.jpg"), FDateTime::UtcNow().GetTicks());
		DestPath = FPaths::Combine(FClipboardImageUtils::GetScreenshotDirectory(), DestFilename);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestPath), true);
		FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Compressed.GetData(), Compressed.Num()), *DestPath);

		UE_LOG(LogUnrealClaude, Log, TEXT("Image re-encoded as JPEG: %s (%lld -> %lld bytes)"), *SourcePath, FileSize, (int64)Compressed.Num());
	}
	else
	{
		FString Ext = FPaths::GetExtension(SourcePath).ToLower();
		FString DestFilename = FString::Printf(TEXT("attached_%lld.%s"), FDateTime::UtcNow().GetTicks(), *Ext);
		DestPath = FPaths::Combine(FClipboardImageUtils::GetScreenshotDirectory(), DestFilename);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(DestPath), true);

		if (!IFileManager::Get().Copy(*DestPath, *SourcePath))
		{
			UE_LOG(LogUnrealClaude, Warning, TEXT("Failed to copy image: %s"), *SourcePath);
			return FReply::Handled();
		}
	}

	AttachedImagePaths.Add(DestPath);
	ThumbnailBrushes.Add(CreateThumbnailBrush(DestPath));
	RebuildImagePreviewStrip();
	OnImagesChangedDelegate.ExecuteIfBound(AttachedImagePaths);

	return FReply::Handled();
}

FReply SClaudeInputArea::HandleSendCancelClicked()
{
	if (bIsWaiting.Get())
	{
		OnCancel.ExecuteIfBound();
	}
	else
	{
		OnSend.ExecuteIfBound();
	}
	return FReply::Handled();
}

bool SClaudeInputArea::TryPasteImageFromClipboard()
{
	using namespace UnrealClaudeConstants::ClipboardImage;

	if (!FClipboardImageUtils::ClipboardHasImage())
	{
		return false;
	}

	// Reject if at max image count
	if (AttachedImagePaths.Num() >= MaxImagesPerMessage)
	{
		UE_LOG(LogUnrealClaude, Log, TEXT("Image paste rejected: already at max (%d images)"), MaxImagesPerMessage);
		return false;
	}

	// Clean up old screenshots before saving a new one
	FString ScreenshotDir = FClipboardImageUtils::GetScreenshotDirectory();
	FClipboardImageUtils::CleanupOldScreenshots(
		ScreenshotDir,
		UnrealClaudeConstants::ClipboardImage::MaxScreenshotAgeSeconds);

	FString SavedPath;
	if (!FClipboardImageUtils::SaveClipboardImageToFile(SavedPath, ScreenshotDir))
	{
		return false;
	}

	AttachedImagePaths.Add(SavedPath);
	ThumbnailBrushes.Add(CreateThumbnailBrush(SavedPath));
	RebuildImagePreviewStrip();

	OnImagesChangedDelegate.ExecuteIfBound(AttachedImagePaths);
	return true;
}

FReply SClaudeInputArea::HandleRemoveImageClicked(int32 Index)
{
	RemoveAttachedImage(Index);
	return FReply::Handled();
}

void SClaudeInputArea::RebuildImagePreviewStrip()
{
	using namespace UnrealClaudeConstants::ClipboardImage;

	if (!ImagePreviewStrip.IsValid())
	{
		return;
	}

	ImagePreviewStrip->ClearChildren();

	if (AttachedImagePaths.Num() == 0)
	{
		ImagePreviewStrip->SetVisibility(EVisibility::Collapsed);
		return;
	}

	ImagePreviewStrip->SetVisibility(EVisibility::Visible);

	// Add a thumbnail slot for each attached image
	for (int32 i = 0; i < AttachedImagePaths.Num(); ++i)
	{
		const FString& ImagePath = AttachedImagePaths[i];
		FString FileName = FPaths::GetCleanFilename(ImagePath);

		ImagePreviewStrip->AddSlot()
		.AutoWidth()
		.Padding(i > 0 ? ThumbnailSpacing : 0.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(ThumbnailSize)
			.HeightOverride(ThumbnailSize)
			.ToolTipText(FText::FromString(FileName))
			[
				SNew(SOverlay)

				// Layer 0: thumbnail image
				+ SOverlay::Slot()
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SNew(SImage)
						.Image(ThumbnailBrushes.IsValidIndex(i) && ThumbnailBrushes[i].IsValid()
							? ThumbnailBrushes[i].Get() : nullptr)
					]
				]

				// Layer 1: remove button (top-right)
				+ SOverlay::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Top)
				[
					SNew(SButton)
					.Text(LOCTEXT("RemoveImageX", "X"))
					.OnClicked_Lambda([this, Index = i]()
					{
						return HandleRemoveImageClicked(Index);
					})
					.ToolTipText(LOCTEXT("RemoveImageTip", "Remove this image"))
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				]
			]
		];
	}

	// Add count label after thumbnails
	ImagePreviewStrip->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(ThumbnailSpacing, 0.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("ImageCount", "{0}/{1}"),
			FText::AsNumber(AttachedImagePaths.Num()),
			FText::AsNumber(MaxImagesPerMessage)))
		.TextStyle(FAppStyle::Get(), "SmallText")
		.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
	];
}

TSharedPtr<FSlateDynamicImageBrush> SClaudeInputArea::CreateThumbnailBrush(const FString& FilePath) const
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("Failed to load image for thumbnail: %s"), *FilePath);
		return nullptr;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	EImageFormat DetectedFormat = ImageWrapperModule.DetectImageFormat(FileData.GetData(), FileData.Num());
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(DetectedFormat);

	if (!ImageWrapper.IsValid())
	{
		return nullptr;
	}

	if (!ImageWrapper->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("Failed to decompress image for thumbnail: %s"), *FilePath);
		return nullptr;
	}

	TArray<uint8> RawData;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawData))
	{
		UE_LOG(LogUnrealClaude, Warning, TEXT("Failed to get raw pixel data for thumbnail"));
		return nullptr;
	}

	const int32 Width = ImageWrapper->GetWidth();
	const int32 Height = ImageWrapper->GetHeight();

	if (Width <= 0 || Height <= 0)
	{
		return nullptr;
	}

	// Create a dynamic brush from the raw pixel data
	FName BrushName = FName(*FString::Printf(TEXT("ClipboardThumb_%s"), *FPaths::GetBaseFilename(FilePath)));
	TSharedPtr<FSlateDynamicImageBrush> Brush = FSlateDynamicImageBrush::CreateWithImageData(
		BrushName,
		FVector2D(Width, Height),
		TArray<uint8>(RawData));

	return Brush;
}

#undef LOCTEXT_NAMESPACE
