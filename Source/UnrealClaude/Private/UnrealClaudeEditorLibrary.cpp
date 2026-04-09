// Copyright Natali Caggiano. All Rights Reserved.

#include "UnrealClaudeEditorLibrary.h"
#include "UnrealClaudeModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetThumbnail.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "RHICommandList.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Slate/SlateTextures.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"

namespace
{
	struct FInhouseThumbnailEntry
	{
		FString UID;
		FString ContentPath;
	};

	FString GetDefaultThumbDir()
	{
		FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
		if (UserProfile.IsEmpty())
		{
			UserProfile = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		}
		return FPaths::Combine(UserProfile, TEXT(".unrealclaude"), TEXT("thumbs"));
	}

	bool ParseJsonlLine(const FString& Line, FInhouseThumbnailEntry& OutEntry)
	{
		TSharedPtr<FJsonObject> Json;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		{
			return false;
		}

		return Json->TryGetStringField(TEXT("uid"), OutEntry.UID)
			&& !OutEntry.UID.IsEmpty()
			&& Json->TryGetStringField(TEXT("content_path"), OutEntry.ContentPath)
			&& !OutEntry.ContentPath.IsEmpty();
	}

	UObject* ResolveAsset(const FString& ContentPath)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ContentPath));
		if (AssetData.IsValid())
		{
			if (UObject* Asset = AssetData.FastGetAsset())
			{
				return Asset;
			}
		}

		return LoadObject<UObject>(nullptr, *ContentPath);
	}

	bool CompressAndSaveThumbnail(const FObjectThumbnail& Thumbnail, const FString& DestPath, FString& OutError)
	{
		const TArray<uint8>& RawImage = Thumbnail.GetUncompressedImageData();
		const int32 Width = Thumbnail.GetImageWidth();
		const int32 Height = Thumbnail.GetImageHeight();
		if (RawImage.Num() <= 0 || Width <= 0 || Height <= 0)
		{
			OutError = TEXT("Thumbnail image is empty");
			return false;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!ImageWrapper.IsValid())
		{
			OutError = TEXT("Failed to create PNG image wrapper");
			return false;
		}

		if (!ImageWrapper->SetRaw(RawImage.GetData(), RawImage.Num(), Width, Height, ERGBFormat::BGRA, 8))
		{
			OutError = TEXT("Failed to prepare thumbnail BGRA data for PNG");
			return false;
		}

		const TArray64<uint8>& CompressedPng = ImageWrapper->GetCompressed();
		if (CompressedPng.Num() == 0)
		{
			OutError = TEXT("Failed to compress thumbnail to PNG");
			return false;
		}

		if (!FFileHelper::SaveArrayToFile(CompressedPng, *DestPath))
		{
			OutError = TEXT("Failed to write PNG file");
			return false;
		}

		return true;
	}

	bool SaveStoredThumbnailFromPackage(const FAssetData& AssetData, const FString& DestPath, FString& OutError)
	{
		if (!AssetData.IsValid())
		{
			OutError = TEXT("Asset data is invalid");
			return false;
		}

		FString PackageFilename;
		if (!FPackageName::DoesPackageExist(AssetData.PackageName.ToString(), &PackageFilename))
		{
			OutError = TEXT("Package file does not exist");
			return false;
		}

		const FName AssetFullName(*AssetData.GetFullName());
		TSet<FName> AssetFullNames;
		AssetFullNames.Add(AssetFullName);

		FThumbnailMap ThumbnailMap;
		ThumbnailTools::LoadThumbnailsFromPackage(PackageFilename, AssetFullNames, ThumbnailMap);
		FObjectThumbnail* StoredThumbnail = ThumbnailMap.Find(AssetFullName);
		if (StoredThumbnail == nullptr)
		{
			OutError = TEXT("Stored package thumbnail not found");
			return false;
		}

		return CompressAndSaveThumbnail(*StoredThumbnail, DestPath, OutError);
	}

	bool SaveLiveThumbnailFromPool(const FAssetData& AssetData, const FString& DestPath, FString& OutError)
	{
		constexpr uint32 ThumbnailSize = ThumbnailTools::DefaultThumbnailSize;

		TSharedRef<FAssetThumbnailPool> ThumbnailPool = MakeShared<FAssetThumbnailPool>(1, 0.02, 1);
		TSharedPtr<FAssetThumbnail> AssetThumbnail = MakeShared<FAssetThumbnail>(AssetData, ThumbnailSize, ThumbnailSize, ThumbnailPool);

		AssetThumbnail->GetViewportRenderTargetTexture();
		ThumbnailPool->PrioritizeThumbnails({ AssetThumbnail }, ThumbnailSize, ThumbnailSize);

		for (int32 Attempt = 0; Attempt < 30; ++Attempt)
		{
			ThumbnailPool->Tick(1.0f / 60.0f);
			FlushRenderingCommands();
			FPlatformProcess::Sleep(0.01f);
		}

		FSlateTexture2DRHIRef* SlateTexture = static_cast<FSlateTexture2DRHIRef*>(AssetThumbnail->GetViewportRenderTargetTexture());
		if (SlateTexture == nullptr || !SlateTexture->IsValid())
		{
			OutError = TEXT("Live thumbnail texture is invalid");
			return false;
		}

		TArray<FColor> Pixels;
		FTextureRHIRef TextureRHI = SlateTexture->GetRHIRef();
		const int32 Width = static_cast<int32>(SlateTexture->GetWidth());
		const int32 Height = static_cast<int32>(SlateTexture->GetHeight());

		ENQUEUE_RENDER_COMMAND(ReadLiveThumbnailSurface)(
			[TextureRHI, Width, Height, &Pixels](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.ReadSurfaceData(
					TextureRHI,
					FIntRect(0, 0, Width, Height),
					Pixels,
					FReadSurfaceDataFlags()
				);
			});
		FlushRenderingCommands();

		if (Pixels.Num() != Width * Height)
		{
			OutError = TEXT("Failed to read live thumbnail pixels");
			return false;
		}

		TArray64<uint8> CompressedPng;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), CompressedPng);
		if (CompressedPng.Num() == 0)
		{
			OutError = TEXT("Failed to compress live thumbnail to PNG");
			return false;
		}

		if (!FFileHelper::SaveArrayToFile(CompressedPng, *DestPath))
		{
			OutError = TEXT("Failed to write PNG file");
			return false;
		}

		return true;
	}

	bool SaveFreshRenderedThumbnail(UObject* AssetObject, const FString& DestPath, FString& OutError)
	{
		if (AssetObject == nullptr)
		{
			OutError = TEXT("Asset object is null");
			return false;
		}

		constexpr uint32 ThumbnailSize = ThumbnailTools::DefaultThumbnailSize;

		TUniquePtr<FSlateTextureRenderTarget2DResource> RenderTarget =
			MakeUnique<FSlateTextureRenderTarget2DResource>(
				FLinearColor::Black,
				ThumbnailSize,
				ThumbnailSize,
				PF_B8G8R8A8,
				SF_Point,
				TA_Wrap,
				TA_Wrap,
				0.0f);

		BeginInitResource(RenderTarget.Get());
		FRenderCommandFence InitFence;
		InitFence.BeginFence();
		InitFence.Wait();

		ThumbnailTools::RenderThumbnail(
			AssetObject,
			ThumbnailSize,
			ThumbnailSize,
			ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush,
			RenderTarget.Get());

		FTextureRHIRef TextureRHI = RenderTarget->GetTextureRHI();
		if (!TextureRHI.IsValid())
		{
			BeginReleaseResource(RenderTarget.Get());
			FlushRenderingCommands();
			OutError = TEXT("Fresh thumbnail render target texture is invalid");
			return false;
		}

		TArray<FColor> Pixels;
		ENQUEUE_RENDER_COMMAND(ReadFreshThumbnailSurface)(
			[TextureRHI, &Pixels, ThumbnailSize](FRHICommandListImmediate& RHICmdList)
			{
				RHICmdList.ReadSurfaceData(
					TextureRHI,
					FIntRect(0, 0, static_cast<int32>(ThumbnailSize), static_cast<int32>(ThumbnailSize)),
					Pixels,
					FReadSurfaceDataFlags());
			});
		FlushRenderingCommands();

		BeginReleaseResource(RenderTarget.Get());
		FlushRenderingCommands();

		if (Pixels.Num() != static_cast<int32>(ThumbnailSize * ThumbnailSize))
		{
			OutError = TEXT("Failed to read fresh thumbnail render target pixels");
			return false;
		}

		TArray64<uint8> CompressedPng;
		FImageUtils::PNGCompressImageArray(
			static_cast<int32>(ThumbnailSize),
			static_cast<int32>(ThumbnailSize),
			TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()),
			CompressedPng);
		if (CompressedPng.Num() == 0)
		{
			OutError = TEXT("Failed to compress fresh thumbnail render target to PNG");
			return false;
		}

		if (!FFileHelper::SaveArrayToFile(CompressedPng, *DestPath))
		{
			OutError = TEXT("Failed to write PNG file");
			return false;
		}

		return true;
	}

	bool SaveWidgetRenderedThumbnail(UObject* AssetObject, const FString& DestPath, FString& OutError)
	{
		if (AssetObject == nullptr)
		{
			OutError = TEXT("Asset object is null");
			return false;
		}

		constexpr uint32 ThumbnailSize = ThumbnailTools::DefaultThumbnailSize;
		const FVector2D DrawSize(static_cast<float>(ThumbnailSize), static_cast<float>(ThumbnailSize));
		const FAssetData AssetData(AssetObject);
		if (!AssetData.IsValid())
		{
			OutError = TEXT("Asset data is invalid");
			return false;
		}

		TSharedPtr<FAssetThumbnailPool> ThumbnailPool = UThumbnailManager::Get().GetSharedThumbnailPool();
		if (!ThumbnailPool.IsValid())
		{
			ThumbnailPool = MakeShared<FAssetThumbnailPool>(16, 0.02, 3);
		}
		TSharedPtr<FAssetThumbnail> AssetThumbnail = MakeShared<FAssetThumbnail>(AssetData, ThumbnailSize, ThumbnailSize, ThumbnailPool);
		TSharedRef<SWidget> ThumbnailWidget = AssetThumbnail->MakeThumbnailWidget();
		TSharedRef<SWidget> SizedWidget =
			SNew(SBox)
			.WidthOverride(DrawSize.X)
			.HeightOverride(DrawSize.Y)
			[
				ThumbnailWidget
			];

		TSharedRef<SWindow> ThumbnailWindow =
			SNew(SWindow)
			.AutoCenter(EAutoCenter::None)
			.SizingRule(ESizingRule::FixedSize)
			.ClientSize(DrawSize)
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.FocusWhenFirstShown(false)
			.ActivationPolicy(EWindowActivationPolicy::Never)
			.IsTopmostWindow(false);
		ThumbnailWindow->SetContent(SizedWidget);

		if (!FSlateApplication::IsInitialized())
		{
			OutError = TEXT("Slate application is not initialized");
			return false;
		}

		FSlateApplication& SlateApp = FSlateApplication::Get();
		SlateApp.AddWindow(ThumbnailWindow, false);
		ThumbnailWindow->ShowWindow();
		ThumbnailWindow->SetOpacity(0.0f);
		ThumbnailWindow->Resize(DrawSize);
		ThumbnailWindow->SlatePrepass();

		// Kick the pool once so the underlying live thumbnail resource is requested.
		bool bThumbnailRendered = false;
		ThumbnailPool->OnThumbnailRendered().AddLambda(
			[&AssetData, &bThumbnailRendered](const FAssetData& RenderedAssetData)
			{
				if (RenderedAssetData.GetSoftObjectPath() == AssetData.GetSoftObjectPath())
				{
					bThumbnailRendered = true;
				}
			});
		AssetThumbnail->GetViewportRenderTargetTexture();
		ThumbnailPool->PrioritizeThumbnails({ AssetThumbnail }, ThumbnailSize, ThumbnailSize);
		for (int32 Attempt = 0; Attempt < 60; ++Attempt)
		{
			ThumbnailPool->Tick(1.0f / 60.0f);
			SlateApp.PumpMessages();
			SlateApp.Tick();
			FlushRenderingCommands();
			FPlatformProcess::Sleep(0.01f);
			if (bThumbnailRendered)
			{
				break;
			}
		}

		if (!bThumbnailRendered)
		{
			SlateApp.RequestDestroyWindow(ThumbnailWindow);
			OutError = TEXT("Thumbnail pool did not finish rendering widget thumbnail");
			return false;
		}

		FWidgetRenderer WidgetRenderer(/*bUseGammaCorrection*/ true, /*bInClearTarget*/ true);
		TStrongObjectPtr<UTextureRenderTarget2D> RenderTarget(FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, true));
		if (!RenderTarget.IsValid())
		{
			OutError = TEXT("Failed to create widget render target");
			return false;
		}

		WidgetRenderer.DrawWidget(RenderTarget.Get(), SizedWidget, DrawSize, 0.0f);
		FlushRenderingCommands();

		FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
		if (Resource == nullptr)
		{
			OutError = TEXT("Widget render target resource is null");
			return false;
		}

		TArray<FColor> Pixels;
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(false);
		if (!Resource->ReadPixels(Pixels, ReadFlags))
		{
			OutError = TEXT("Failed to read widget render target pixels");
			return false;
		}

		if (Pixels.Num() != static_cast<int32>(ThumbnailSize * ThumbnailSize))
		{
			OutError = TEXT("Widget render target returned unexpected pixel count");
			return false;
		}

		TArray64<uint8> CompressedPng;
		FImageUtils::PNGCompressImageArray(
			static_cast<int32>(ThumbnailSize),
			static_cast<int32>(ThumbnailSize),
			TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()),
			CompressedPng);
		if (CompressedPng.Num() == 0)
		{
			OutError = TEXT("Failed to compress widget-rendered thumbnail to PNG");
			return false;
		}

		if (!FFileHelper::SaveArrayToFile(CompressedPng, *DestPath))
		{
			SlateApp.RequestDestroyWindow(ThumbnailWindow);
			OutError = TEXT("Failed to write PNG file");
			return false;
		}

		SlateApp.RequestDestroyWindow(ThumbnailWindow);
		return true;
	}

	bool SaveThumbnailPng(UObject* AssetObject, const FString& DestPath, FString& OutError)
	{
		if (AssetObject == nullptr)
		{
			OutError = TEXT("Asset object is null");
			return false;
		}

		if (FAssetData AssetData = FAssetData(AssetObject); AssetData.IsValid())
		{
			if (SaveWidgetRenderedThumbnail(AssetObject, DestPath, OutError))
			{
				return true;
			}

			if (SaveFreshRenderedThumbnail(AssetObject, DestPath, OutError))
			{
				return true;
			}

			if (FObjectThumbnail* GeneratedThumbnail = ThumbnailTools::GenerateThumbnailForObjectToSaveToDisk(AssetObject))
			{
				if (CompressAndSaveThumbnail(*GeneratedThumbnail, DestPath, OutError))
				{
					return true;
				}
			}

			if (SaveLiveThumbnailFromPool(AssetData, DestPath, OutError))
			{
				return true;
			}

			if (SaveStoredThumbnailFromPackage(AssetData, DestPath, OutError))
			{
				return true;
			}
		}

		if (FObjectThumbnail* CachedThumbnail = ThumbnailTools::GetThumbnailForObject(AssetObject))
		{
			if (CompressAndSaveThumbnail(*CachedThumbnail, DestPath, OutError))
			{
				return true;
			}
		}

		FObjectThumbnail Thumbnail;
		ThumbnailTools::RenderThumbnail(
			AssetObject,
			ThumbnailTools::DefaultThumbnailSize,
			ThumbnailTools::DefaultThumbnailSize,
			ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush,
			nullptr,
			&Thumbnail
		);

		return CompressAndSaveThumbnail(Thumbnail, DestPath, OutError);
	}

	FString SerializeResult(const FString& OutputDir, int32 Exported, int32 Skipped, int32 Failed,
		const TArray<TSharedPtr<FJsonValue>>& Entries)
	{
		TSharedRef<FJsonObject> ResultObject = MakeShared<FJsonObject>();
		ResultObject->SetStringField(TEXT("output_dir"), OutputDir);
		ResultObject->SetNumberField(TEXT("exported"), Exported);
		ResultObject->SetNumberField(TEXT("skipped"), Skipped);
		ResultObject->SetNumberField(TEXT("failed"), Failed);
		ResultObject->SetArrayField(TEXT("entries"), Entries);

		FString Serialized;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
		FJsonSerializer::Serialize(ResultObject, Writer);
		return Serialized;
	}

	FString SerializeFatalError(const FString& Message)
	{
		TArray<TSharedPtr<FJsonValue>> EmptyEntries;
		TSharedPtr<FJsonObject> ErrorEntry = MakeShared<FJsonObject>();
		ErrorEntry->SetStringField(TEXT("status"), TEXT("failed"));
		ErrorEntry->SetStringField(TEXT("error"), Message);
		EmptyEntries.Add(MakeShared<FJsonValueObject>(ErrorEntry));
		return SerializeResult(TEXT(""), 0, 0, 1, EmptyEntries);
	}
}

FString UUnrealClaudeEditorLibrary::ExportInhouseThumbnails(
	const FString& IndexJsonlPath,
	const FString& OutputDir,
	bool bOverwrite,
	int32 Limit,
	const FString& TargetUID)
{
	const FString ResolvedOutputDir = OutputDir.IsEmpty() ? GetDefaultThumbDir() : OutputDir;

	if (!FPaths::FileExists(IndexJsonlPath))
	{
		return SerializeFatalError(FString::Printf(TEXT("Index JSONL file not found: %s"), *IndexJsonlPath));
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*ResolvedOutputDir);

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *IndexJsonlPath))
	{
		return SerializeFatalError(FString::Printf(TEXT("Failed to read index JSONL file: %s"), *IndexJsonlPath));
	}

	int32 Exported = 0;
	int32 Skipped = 0;
	int32 Failed = 0;
	int32 Processed = 0;
	TArray<TSharedPtr<FJsonValue>> ResultEntries;

	for (const FString& Line : Lines)
	{
		if (Line.TrimStartAndEnd().IsEmpty())
		{
			continue;
		}

		FInhouseThumbnailEntry Entry;
		if (!ParseJsonlLine(Line, Entry))
		{
			Failed++;
			continue;
		}

		if (!TargetUID.IsEmpty() && Entry.UID != TargetUID)
		{
			continue;
		}

		if (Limit > 0 && Processed >= Limit)
		{
			break;
		}

		Processed++;

		const FString DestPath = FPaths::Combine(ResolvedOutputDir, FString::Printf(TEXT("inhouse_%s.png"), *Entry.UID));
		TSharedPtr<FJsonObject> ResultItem = MakeShared<FJsonObject>();
		ResultItem->SetStringField(TEXT("uid"), Entry.UID);
		ResultItem->SetStringField(TEXT("content_path"), Entry.ContentPath);
		ResultItem->SetStringField(TEXT("thumbnail_path"), DestPath);

		if (!bOverwrite && FPaths::FileExists(DestPath))
		{
			Skipped++;
			ResultItem->SetStringField(TEXT("status"), TEXT("skipped"));
			ResultEntries.Add(MakeShared<FJsonValueObject>(ResultItem));
			continue;
		}

		UObject* AssetObject = ResolveAsset(Entry.ContentPath);
		if (AssetObject == nullptr)
		{
			Failed++;
			ResultItem->SetStringField(TEXT("status"), TEXT("failed"));
			ResultItem->SetStringField(TEXT("error"), TEXT("Failed to resolve asset from content_path"));
			ResultEntries.Add(MakeShared<FJsonValueObject>(ResultItem));
			continue;
		}

		FString ExportError;
		if (!SaveThumbnailPng(AssetObject, DestPath, ExportError))
		{
			Failed++;
			ResultItem->SetStringField(TEXT("status"), TEXT("failed"));
			ResultItem->SetStringField(TEXT("error"), ExportError);
			ResultEntries.Add(MakeShared<FJsonValueObject>(ResultItem));
			UE_LOG(LogUnrealClaude, Warning, TEXT("ExportInhouseThumbnails failed for %s: %s"), *Entry.UID, *ExportError);
			continue;
		}

		Exported++;
		ResultItem->SetStringField(TEXT("status"), TEXT("exported"));
		ResultEntries.Add(MakeShared<FJsonValueObject>(ResultItem));
	}

	return SerializeResult(ResolvedOutputDir, Exported, Skipped, Failed, ResultEntries);
}
