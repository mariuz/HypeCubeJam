#include "Assets/HCJAssetSettings.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HypeCubeJam.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Strong references that live until engine exit; null entries remember failed loads. */
	struct FHCJAssetCache
	{
		TMap<FSoftObjectPath, TStrongObjectPtr<UObject>> Assets;
		TStrongObjectPtr<USoundAttenuation> Attenuation;
		TStrongObjectPtr<USoundConcurrency> Concurrency[static_cast<int32>(EHCJSoundGroup::Count)];
		int32 NumPlayed[static_cast<int32>(EHCJSoundGroup::Count)] = {};
		/** Cosmetic variety only (which of several equivalent sounds, pitch wobble). */
		FRandomStream Random{ static_cast<int32>(FPlatformTime::Cycles64()) };

		static FHCJAssetCache& Get()
		{
			static FHCJAssetCache* Cache = nullptr;
			if (Cache == nullptr)
			{
				Cache = new FHCJAssetCache();
				// Strong pointers must be gone before the UObject system shuts down.
				FCoreDelegates::OnPreExit.AddLambda([]()
				{
					Cache->Assets.Empty();
					Cache->Attenuation.Reset();
					for (TStrongObjectPtr<USoundConcurrency>& Limit : Cache->Concurrency)
					{
						Limit.Reset();
					}
				});
			}
			return *Cache;
		}
	};
}

UHCJAssetSettings::UHCJAssetSettings()
{
}

UObject* UHCJAssetSettings::LoadAsset(const FSoftObjectPath& Path, const UClass* ExpectedClass, const TCHAR* Purpose)
{
	if (Path.IsNull() || IsEngineExitRequested())
	{
		return nullptr;
	}
#if !UE_BUILD_SHIPPING
	// -HCJNoAssets: run on the built-in fallbacks only (basic shapes, no effects, no sound), e.g. to compare frame rates.
	static const bool bNoAssets = FParse::Param(FCommandLine::Get(), TEXT("HCJNoAssets"));
	if (bNoAssets)
	{
		return nullptr;
	}
#endif
	check(IsInGameThread());

	FHCJAssetCache& Cache = FHCJAssetCache::Get();
	UObject* Asset = nullptr;
	if (const TStrongObjectPtr<UObject>* Cached = Cache.Assets.Find(Path))
	{
		Asset = Cached->Get();
	}
	else
	{
		Asset = Path.TryLoad();
		if (Asset == nullptr)
		{
			UE_LOG(LogHCJ, Warning, TEXT("Asset '%s' (%s) could not be loaded; using the built-in fallback"), *Path.ToString(), Purpose);
		}
		else
		{
			UE_LOG(LogHCJ, Verbose, TEXT("Loaded asset '%s' (%s)"), *Path.ToString(), Purpose);
		}
		Cache.Assets.Add(Path, TStrongObjectPtr<UObject>(Asset));
	}

	if (Asset != nullptr && ExpectedClass != nullptr && !Asset->IsA(ExpectedClass))
	{
		UE_LOG(LogHCJ, Warning, TEXT("Asset '%s' (%s) is a %s, not a %s; using the built-in fallback"),
			*Path.ToString(), Purpose, *Asset->GetClass()->GetName(), *ExpectedClass->GetName());
		// Report once: forget the object so the next lookup returns null silently.
		Cache.Assets.Add(Path, TStrongObjectPtr<UObject>());
		return nullptr;
	}
	return Asset;
}

void UHCJAssetSettings::PreloadAll()
{
	const UHCJAssetSettings& Settings = Get();
	const UScriptStruct* PathStruct = TBaseStructure<FSoftObjectPath>::Get();

	int32 NumRequested = 0;
	int32 NumLoaded = 0;
	const auto LoadOne = [&NumRequested, &NumLoaded](const FSoftObjectPath& Path, const FString& PropertyName)
	{
		if (!Path.IsNull())
		{
			++NumRequested;
			NumLoaded += LoadAsset(Path, nullptr, *PropertyName) != nullptr ? 1 : 0;
		}
	};

	for (TFieldIterator<FStructProperty> It(StaticClass(), EFieldIterationFlags::None); It; ++It)
	{
		if (It->Struct == PathStruct)
		{
			LoadOne(*It->ContainerPtrToValuePtr<FSoftObjectPath>(&Settings), It->GetName());
		}
	}
	// Plain lists of soft paths (ship meshes, sound variations).
	for (TFieldIterator<FArrayProperty> It(StaticClass(), EFieldIterationFlags::None); It; ++It)
	{
		const FStructProperty* Inner = CastField<FStructProperty>(It->Inner);
		if (Inner != nullptr && Inner->Struct == PathStruct)
		{
			for (const FSoftObjectPath& Path : *It->ContainerPtrToValuePtr<TArray<FSoftObjectPath>>(&Settings))
			{
				LoadOne(Path, It->GetName());
			}
		}
	}
	for (const FHCJAsteroidMeshVariant& Variant : Settings.AsteroidMeshes)
	{
		LoadOne(Variant.Mesh, TEXT("AsteroidMeshes"));
	}
	for (const FHCJMusicLayer& Layer : Settings.MusicLayers)
	{
		LoadOne(Layer.Sound, TEXT("MusicLayers"));
	}
	LoadOne(Settings.MenuTrack.Sound, TEXT("MenuTrack"));
	LoadOne(Settings.ExploreTrack.Sound, TEXT("ExploreTrack"));
	LoadOne(Settings.CombatTrack.Sound, TEXT("CombatTrack"));
	for (const FHCJShipSkin& Skin : Settings.ShipSkins)
	{
		LoadOne(Skin.Material, TEXT("ShipSkins"));
	}

	UE_LOG(LogHCJ, Log, TEXT("Assets: %d of %d configured assets loaded%s"), NumLoaded, NumRequested,
		NumLoaded == NumRequested ? TEXT("") : TEXT(" (the rest fall back to basic shapes / no effect)"));
}

int32 UHCJAssetSettings::PickShipVariant(int32 PlayerId, int32 NumVariants)
{
	if (NumVariants <= 1)
	{
		return 0;
	}
	const uint32 Hash = static_cast<uint32>(HCJ::MixSeed(PlayerId, 0x5A12C0DE));
	return static_cast<int32>(Hash % static_cast<uint32>(NumVariants));
}

int32 UHCJAssetSettings::PickNearestSkin(const FLinearColor& Color, TConstArrayView<FLinearColor> SkinColors)
{
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < SkinColors.Num(); ++Index)
	{
		const FLinearColor& Skin = SkinColors[Index];
		const float Distance = FMath::Square(Skin.R - Color.R) + FMath::Square(Skin.G - Color.G) + FMath::Square(Skin.B - Color.B);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

bool UHCJAssetSettings::IsNearLocalView(const UObject* WorldContextObject, const FVector& Location, float MaxDistance)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World == nullptr)
	{
		return false;
	}
	const APlayerController* LocalController = GEngine->GetFirstLocalPlayerController(World);
	if (LocalController == nullptr)
	{
		return false;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	LocalController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	return FVector::DistSquared(ViewLocation, Location) <= FMath::Square(static_cast<double>(MaxDistance));
}

UNiagaraComponent* UHCJAssetSettings::SpawnEffect(const UObject* WorldContextObject, const FSoftObjectPath& System, const TCHAR* Purpose,
	const FVector& Location, const FRotator& Rotation, float Scale)
{
	if (System.IsNull() || !IsNearLocalView(WorldContextObject, Location, Get().EffectMaxDistance))
	{
		return nullptr;
	}
	UNiagaraSystem* Asset = Load<UNiagaraSystem>(System, Purpose);
	if (Asset == nullptr)
	{
		return nullptr;
	}
	return UNiagaraFunctionLibrary::SpawnSystemAtLocation(WorldContextObject, Asset, Location, Rotation, FVector(Scale),
		/*bAutoDestroy*/ true, /*bAutoActivate*/ true, ENCPoolMethod::AutoRelease, /*bPreCullCheck*/ true);
}

void UHCJAssetSettings::PlaySound(const UObject* WorldContextObject, const FSoftObjectPath& Sound, const TCHAR* Purpose, const FVector& Location, float Volume)
{
	const UHCJAssetSettings& Settings = Get();
	if (Sound.IsNull() || Volume <= 0.0f
		|| !IsNearLocalView(WorldContextObject, Location, Settings.SoundInnerRadius + Settings.SoundFalloffDistance))
	{
		return;
	}
	if (USoundBase* Asset = Load<USoundBase>(Sound, Purpose))
	{
		++FHCJAssetCache::Get().NumPlayed[static_cast<int32>(EHCJSoundGroup::Explosion)];
		UGameplayStatics::PlaySoundAtLocation(WorldContextObject, Asset, Location, FRotator::ZeroRotator, Volume, 1.0f, 0.0f, GetSoundAttenuation(),
			GetConcurrency(EHCJSoundGroup::Explosion));
	}
}

void UHCJAssetSettings::PlaySoundAt(const UObject* WorldContextObject, TConstArrayView<FSoftObjectPath> Sounds, const TCHAR* Purpose,
	const FVector& Location, float Volume, float PitchVariation, EHCJSoundGroup Group, float PitchScale)
{
	const UHCJAssetSettings& Settings = Get();
	if (Sounds.Num() == 0 || Volume <= 0.0f
		|| !IsNearLocalView(WorldContextObject, Location, Settings.SoundInnerRadius + Settings.SoundFalloffDistance))
	{
		return;
	}
	FHCJAssetCache& Cache = FHCJAssetCache::Get();
	const FSoftObjectPath& Pick = Sounds[Cache.Random.RandHelper(Sounds.Num())];
	if (USoundBase* Asset = Load<USoundBase>(Pick, Purpose))
	{
		++Cache.NumPlayed[static_cast<int32>(Group)];
		const float Pitch = FMath::Max(PitchScale, 0.1f) * (1.0f + PitchVariation * (2.0f * Cache.Random.GetFraction() - 1.0f));
		UGameplayStatics::PlaySoundAtLocation(WorldContextObject, Asset, Location, FRotator::ZeroRotator, Volume, Pitch, 0.0f, GetSoundAttenuation(),
			GetConcurrency(Group));
	}
}

void UHCJAssetSettings::PlaySound2D(const UObject* WorldContextObject, TConstArrayView<FSoftObjectPath> Sounds, const TCHAR* Purpose,
	float Volume, EHCJSoundGroup Group)
{
	if (Sounds.Num() == 0 || Volume <= 0.0f)
	{
		return;
	}
	FHCJAssetCache& Cache = FHCJAssetCache::Get();
	const FSoftObjectPath& Pick = Sounds[Cache.Random.RandHelper(Sounds.Num())];
	if (USoundBase* Asset = Load<USoundBase>(Pick, Purpose))
	{
		++Cache.NumPlayed[static_cast<int32>(Group)];
		UGameplayStatics::PlaySound2D(WorldContextObject, Asset, Volume, 1.0f, 0.0f, GetConcurrency(Group));
	}
}

USoundConcurrency* UHCJAssetSettings::GetConcurrency(EHCJSoundGroup Group)
{
	if (Group == EHCJSoundGroup::Music || Group >= EHCJSoundGroup::Count || IsEngineExitRequested())
	{
		return nullptr;
	}
	FHCJAssetCache& Cache = FHCJAssetCache::Get();
	TStrongObjectPtr<USoundConcurrency>& Limit = Cache.Concurrency[static_cast<int32>(Group)];
	if (!Limit.IsValid())
	{
		const UHCJAssetSettings& Settings = Get();
		int32 MaxVoices = Settings.MaxWorldVoices;
		switch (Group)
		{
		case EHCJSoundGroup::Fire: MaxVoices = Settings.MaxFireVoices; break;
		case EHCJSoundGroup::Impact: MaxVoices = Settings.MaxImpactVoices; break;
		case EHCJSoundGroup::Explosion: MaxVoices = Settings.MaxExplosionVoices; break;
		case EHCJSoundGroup::UI: MaxVoices = Settings.MaxUiVoices; break;
		default: break;
		}
		USoundConcurrency* Object = NewObject<USoundConcurrency>(GetTransientPackage());
		Object->Concurrency.MaxCount = FMath::Max(MaxVoices, 1);
		Object->Concurrency.bLimitToOwner = false;
		Object->Concurrency.ResolutionRule = Group == EHCJSoundGroup::UI
			? EMaxConcurrentResolutionRule::StopOldest : EMaxConcurrentResolutionRule::StopFarthestThenOldest;
		Limit.Reset(Object);
	}
	return Limit.Get();
}

int32 UHCJAssetSettings::GetNumSoundsPlayed(EHCJSoundGroup Group)
{
	return Group < EHCJSoundGroup::Count ? FHCJAssetCache::Get().NumPlayed[static_cast<int32>(Group)] : 0;
}

float UHCJAssetSettings::CosmeticRandom()
{
	return FHCJAssetCache::Get().Random.GetFraction();
}

USoundAttenuation* UHCJAssetSettings::GetSoundAttenuation()
{
	FHCJAssetCache& Cache = FHCJAssetCache::Get();
	if (!Cache.Attenuation.IsValid() && !IsEngineExitRequested())
	{
		const UHCJAssetSettings& Settings = Get();
		USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(GetTransientPackage(), TEXT("HCJSoundAttenuation"));
		Attenuation->Attenuation.bAttenuate = true;
		Attenuation->Attenuation.bSpatialize = true;
		// Natural falloff over the long distances of a sector: -6 dB about a quarter of the way out, -40 dB at the edge.
		Attenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Attenuation->Attenuation.dBAttenuationAtMax = -40.0f;
		// Sounds on top of the camera (the own ship) are not panned hard to one side.
		Attenuation->Attenuation.NonSpatializedRadiusStart = 900.0f;
		Attenuation->Attenuation.NonSpatializedRadiusEnd = 300.0f;
		Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
		Attenuation->Attenuation.AttenuationShapeExtents = FVector(Settings.SoundInnerRadius, 0.0, 0.0);
		Attenuation->Attenuation.FalloffDistance = Settings.SoundFalloffDistance;
		Cache.Attenuation.Reset(Attenuation);
	}
	return Cache.Attenuation.Get();
}
