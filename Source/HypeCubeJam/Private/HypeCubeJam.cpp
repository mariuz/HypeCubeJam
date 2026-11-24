#include "HypeCubeJam.h"
#include "Game/HCJPlayerController.h"

#include "AudioMixerBlueprintLibrary.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Game/HCJUiLogic.h"
#include "Misc/CoreDelegates.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RHIGlobals.h"
#include "Ship/HCJShipCharacter.h"
#include "UnrealClient.h"
#include "World/HCJSectorLayout.h"
#include "World/HCJSpaceBackdrop.h"
#include "World/HCJUniverse.h"

DEFINE_LOG_CATEGORY(LogHCJ);

#if !UE_BUILD_SHIPPING
namespace
{
	/**
	 * Unattended look test for rendered runs (see README, "Capture runs"):
	 *   -HCJCapture=<seconds>  every <seconds> saves Saved/Screenshots/HCJCapture/Shot_NN.png and logs the average
	 *                          and worst frame rate of that interval and how far the process is over its video
	 *                          memory budget (LogHCJ, "Capture"; 0 MB is what you want).
	 *   -HCJAutoFly            the local ship turns slowly, holds fire and alternates cruise / boost, so that
	 *                          bolts, muzzle flashes, trails and impacts are in the pictures.
	 *   -HCJAutoTorpedo        with -HCJAutoFly: it also fires a photon torpedo whenever one is loaded.
 *   -HCJAutoWarp           with -HCJAutoFly: it also holds the warp key for six seconds of every twenty.
	 *   -HCJAutoAim            with -HCJAutoFly: the nose follows the nearest other ship in sight (and locks it), so that
	 *                          the autopilot scores - the playthrough tests want a client with kills and deaths.
	 *   -HCJAutoAimError=<deg> with -HCJAutoAim: a poor shot - the nose wanders around the target by up to <deg> degrees
	 *                          to each side (yaw; pitch 0.7 of it, two slow sine waves), so that an aim assist has
	 *                          something to do (item 40).
	 *   -HCJOrbit              the chase camera looks at the local ship from another side after every picture
	 *                          (behind, right, front, left, ...), to check the facing of the ship mesh.
	 *   -HCJCaptureCmd="..."   console command run once, together with the third picture (e.g. "ListTextures").
	 *   -HCJCaptureEveryCmd="..." console command of the local player run with every picture (e.g. "HCJGive 2").
	 *   -HCJSkyCam             every second picture the chase camera looks at one of the bodies in the sky of the
	 *                          current sector (-HCJSkyIndex=<n> shifts which), to check the backdrop.
	 *   -HCJHoleTest=<f>       after the second picture runs the console command "HCJGotoHole <f>": the local ship is
	 *                          put at rest at <f> of the influence radius from a black hole (works on clients too,
	 *                          the server does it). -HCJHoleTestEvery=<n> repeats that every n pictures.
	 *   -HCJFrameDump=<n>      saves <n> consecutive frames to Saved/Screenshots/HCJFrames (for a video; use with
	 *                          -benchmark -fps=30); -HCJFrameDumpStart=<s> waits <s> seconds first. No interval
	 *                          pictures are taken while frames are dumped. The sound of exactly those frames is
	 *                          recorded to Saved/Screenshots/HCJFrames/Audio.wav; run with -NonRealtimeAudio so the
	 *                          audio device renders one fixed step per frame like the game (otherwise the recording
	 *                          is in wall-clock time and does not fit the frames).
	 *   -HCJDemoCmds="a;b;c"   with -HCJFrameDump: console commands of the local player, run one after the other at even
	 *                          intervals while the frames are dumped (e.g. "HCJGive 3;HCJGotoStation 6000"), so that a
	 *                          video shows more than the autopilot would find by itself.
	 *   -HCJCaptureCmds="a;b;;c" console commands of the local player, one per picture: the n-th is run in the frame of
	 *                          the n-th picture and already shows in it (empty entries skip a picture; an underscore stands for a
	 *                          space). With -HCJMenu (start in the title menu although this is a capture run) and the
	 *                          HCJMenuCmd console command this walks through the menus: e.g.
	 *                          -HCJMenu -HCJCapture=4 -HCJCaptureCmds="HCJMenuCmd_settings;HCJMenuCmd_main;HCJMenuCmd_play;;HCJMenuCmd_pause"
	 *   -HCJTour               listen server / standalone only: after every picture (every second one with
	 *                          -HCJSkyCam) the local ship is moved to a spawn point of the next sector, so one
	 *                          run shows the sky of many sectors. -HCJTourStart=<sector id> picks the first one.
	 *   -HCJQuitAfter=<s>      the process quits by itself after <s> seconds (also a dedicated server, also without -HCJCapture).
 * Does nothing without a local player (dedicated server, commandlets).
	 */
	struct FHCJCapture
	{
		float Interval = 0.0f;
		bool bAutoFly = false;
		bool bAutoTorpedo = false;
		bool bAutoWarp = false;
		bool bAutoAim = false;
		float AutoAimErrorDeg = 0.0f;
		double AutoAimClock = 0.0;
		double AutoFlyTime = 0.0;
		bool bOrbit = false;
		bool bTour = false;
		bool bSkyCam = false;
		int32 TourSector = -1;
		int32 SkyIndex = 0;
		int32 FrameDumpCount = 0;
		float FrameDumpStart = 0.0f;
		double TotalTime = 0.0;
		int32 NumFramesDumped = 0;
		bool bRecordingAudio = false;
		float HoleTestFraction = 0.0f;
		int32 HoleTestEvery = 1000;
		FString OnceCommand;
		FString EveryCommand;
		TArray<FString> ShotCommands;
		TArray<FString> DemoCommands;
		int32 NumDemoCommandsRun = 0;
		double IntervalTime = 0.0;
		double WorstFrame = 0.0;
		int32 IntervalFrames = 0;
		int32 NumShots = 0;

		bool Tick(float DeltaSeconds)
		{
			const UWorld* World = nullptr;
			if (GEngine != nullptr)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::Game && Context.World() != nullptr)
					{
						World = Context.World();
						break;
					}
				}
			}
			APlayerController* Controller = World != nullptr ? GEngine->GetFirstLocalPlayerController(World) : nullptr;
			if (Controller == nullptr)
			{
				return true;
			}

			// Frame dump for a video: one picture per frame (run with -benchmark -fps=30 for even pacing).
			if (FrameDumpCount > 0)
			{
				TotalTime += DeltaSeconds;
				if (TotalTime >= FrameDumpStart && NumFramesDumped < FrameDumpCount)
				{
					if (DemoCommands.Num() > 0 && NumDemoCommandsRun < DemoCommands.Num()
						&& NumFramesDumped >= (NumDemoCommandsRun * FrameDumpCount) / DemoCommands.Num() + 45)
					{
						Controller->ConsoleCommand(DemoCommands[NumDemoCommandsRun++]);
					}
					if (!bRecordingAudio)
					{
						bRecordingAudio = true;
						UAudioMixerBlueprintLibrary::StartRecordingOutput(World, FrameDumpCount / 30.0f + 5.0f);
					}
					const FString FrameName = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("HCJFrames") / FString::Printf(TEXT("Frame_%05d.png"), NumFramesDumped);
					FScreenshotRequest::RequestScreenshot(FrameName, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
					if (++NumFramesDumped == FrameDumpCount)
					{
						UAudioMixerBlueprintLibrary::StopRecordingOutputToAnyPath(World, EAudioRecordingExportType::WavFile, TEXT("Audio"),
							FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("HCJFrames")));
						UE_LOG(LogHCJ, Log, TEXT("Capture: %d frames dumped to Saved/Screenshots/HCJFrames (sound: Audio.wav)"), NumFramesDumped);
					}
				}
			}

			IntervalTime += DeltaSeconds;
			const bool bSkyLook = bSkyCam && (NumShots % 2) == 1;
			WorstFrame = FMath::Max(WorstFrame, double(DeltaSeconds));
			++IntervalFrames;

			if (bAutoFly && !bSkyLook)
			{
				if (AHCJShipCharacter* Ship = Cast<AHCJShipCharacter>(Controller->GetPawn()))
				{
					const bool bBoost = (NumShots % 2) == 1;
					Ship->SetFiring(true);
					if (bAutoTorpedo)
					{
						// Whenever one is loaded: the ship itself (owning client) and the server decide whether it goes off.
						Ship->FireTorpedo();
					}
					Ship->SetBoosting(bBoost);
					if (bAutoWarp)
					{
						// The warp key for six seconds of every twenty: a charge, a full jump, then the cooldown.
						AutoFlyTime += DeltaSeconds;
						Ship->SetWarping(FMath::Fmod(AutoFlyTime, 20.0) < 6.0);
					}
					Ship->AddThrustInput(FVector(bBoost ? 1.0 : 0.3, 0.0, 0.0));
					// Circle inside the asteroid field: turn steadily, and head back for the middle when far out.
					const AHCJUniverse* Universe = AHCJUniverse::Get(World);
					const int32 Sector = Universe != nullptr ? Universe->GetSectorAtLocation(Ship->GetActorLocation()) : INDEX_NONE;
					FRotator Aim = Controller->GetControlRotation();
					Aim.Yaw += 20.0 * DeltaSeconds;
					Aim.Pitch = 0.0;
					if (Sector != INDEX_NONE)
					{
						const FVector ToMiddle = Universe->GetSectorOrigin(Sector) - Ship->GetActorLocation();
						if (ToMiddle.SizeSquared() > FMath::Square(0.5 * HCJ::SectorFieldRadius))
						{
							Aim = FMath::RInterpTo(Aim, ToMiddle.Rotation(), DeltaSeconds, 2.0f);
						}
					}
					if (bAutoAim)
					{
						// The nearest other ship this machine knows of, within 120 m and not behind a rock: nose on it.
						AHCJShipCharacter* Nearest = nullptr;
						double NearestDistSq = FMath::Square(12000.0);
						for (TActorIterator<AHCJShipCharacter> It(Ship->GetWorld()); It; ++It)
						{
							AHCJShipCharacter* Other = *It;
							const double DistSq = FVector::DistSquared(Other->GetActorLocation(), Ship->GetActorLocation());
							if (Other != Ship && !Other->IsDead() && !Other->IsCloakHidden() && DistSq < NearestDistSq
								&& !Ship->GetWorld()->LineTraceTestByObjectType(Ship->GetActorLocation(), Other->GetActorLocation(), FCollisionObjectQueryParams(ECC_WorldStatic)))
							{
								NearestDistSq = DistSq;
								Nearest = Other;
							}
						}
						if (Nearest != nullptr)
						{
							FRotator Wanted = (Nearest->GetActorLocation() - Ship->GetActorLocation()).Rotation();
							if (AutoAimErrorDeg > 0.0f)
							{
								AutoAimClock += DeltaSeconds;
								Wanted.Yaw += AutoAimErrorDeg * FMath::Sin(AutoAimClock * 0.9);
								Wanted.Pitch += 0.7 * AutoAimErrorDeg * FMath::Sin(AutoAimClock * 1.3 + 1.0);
							}
							Aim = FMath::RInterpTo(Controller->GetControlRotation(), Wanted, DeltaSeconds, 5.0f);
							Ship->SetLockTarget(Nearest);
							// (The player controller hands the ship ITS target every frame, and that target is the HUD's
							// auto target, which a headless run never draws: without this the lock would always be empty.)
							if (AHCJPlayerController* Player = Cast<AHCJPlayerController>(Controller))
							{
								Player->SetAutoTarget(Nearest);
							}
						}
					}
					Controller->SetControlRotation(Aim);
				}
			}

			if (bOrbit && !bSkyLook)
			{
				const APawn* Pawn = Controller->GetPawn();
				if (USpringArmComponent* Boom = Pawn != nullptr ? Pawn->FindComponentByClass<USpringArmComponent>() : nullptr)
				{
					Boom->bUsePawnControlRotation = false;
					Boom->SetUsingAbsoluteRotation(true);
					Boom->SetWorldRotation(FRotator(-12.0, Pawn->GetActorRotation().Yaw + 90.0 * (NumShots % 4), 0.0));
				}
			}

			if (bSkyLook)
			{
				// Point the chase camera at one of the bodies in the sky of this sector.
				const APawn* Pawn = Controller->GetPawn();
				const AHCJUniverse* Universe = AHCJUniverse::Get(World);
				const AHCJSpaceBackdrop* Backdrop = Universe != nullptr ? Universe->GetBackdrop() : nullptr;
				USpringArmComponent* Boom = Pawn != nullptr ? Pawn->FindComponentByClass<USpringArmComponent>() : nullptr;
				if (Boom != nullptr && Backdrop != nullptr && Universe->IsBuilt())
				{
					const FHCJSectorSky Sky = Backdrop->MakeSky(Universe->GetSectorAtLocation(Pawn->GetActorLocation()));
					if (Sky.Bodies.Num() > 0)
					{
						Boom->bUsePawnControlRotation = false;
						Boom->SetUsingAbsoluteRotation(true);
						Boom->SetWorldRotation(Sky.Bodies[(NumShots / 2 + SkyIndex) % Sky.Bodies.Num()].Direction.Rotation());
					}
				}
			}

			if (IntervalTime >= Interval && !(FrameDumpCount > 0 && TotalTime >= FrameDumpStart && NumFramesDumped < FrameDumpCount))
			{
				const FString Filename = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("HCJCapture") / FString::Printf(TEXT("Shot_%02d.png"), NumShots);
				FScreenshotRequest::RequestScreenshot(Filename, /*bInShowUI*/ true, /*bAddFilenameSuffix*/ false);
				UE_LOG(LogHCJ, Log, TEXT("Capture %d: %.1f fps average, %.1f fps worst frame over %.1f s, video memory %.0f MB over budget -> %s"),
					NumShots, IntervalFrames / IntervalTime, WorstFrame > 0.0 ? 1.0 / WorstFrame : 0.0, IntervalTime,
					double(GDemotedLocalMemorySize) / (1024.0 * 1024.0), *Filename);
				if (NumShots == 2 && !OnceCommand.IsEmpty())
				{
					GEngine->Exec(Controller->GetWorld(), *OnceCommand);
				}
				if (!EveryCommand.IsEmpty())
				{
					Controller->ConsoleCommand(EveryCommand);
				}
				if (ShotCommands.IsValidIndex(NumShots) && !ShotCommands[NumShots].IsEmpty())
				{
					Controller->ConsoleCommand(ShotCommands[NumShots]);
				}
				if (HoleTestFraction > 0.0f && NumShots >= 1 && (NumShots % HoleTestEvery) == 1)
				{
					Controller->ConsoleCommand(FString::Printf(TEXT("HCJGotoHole %f"), HoleTestFraction));
				}
				++NumShots;
				if (bTour && (!bSkyCam || (NumShots % 2) == 0))
				{
					APawn* Pawn = Controller->GetPawn();
					const AHCJUniverse* Universe = AHCJUniverse::Get(World);
					if (Pawn != nullptr && Pawn->HasAuthority() && Universe != nullptr && Universe->IsBuilt())
					{
						const int32 Sector = (TourSector >= 0 ? TourSector : Universe->GetSectorAtLocation(Pawn->GetActorLocation()) + 1) % Universe->NumSectors();
						TourSector = Sector + 1;
						FRandomStream Stream(NumShots);
						const FTransform Spawn = Universe->GetRandomSpawnTransform(Sector, Stream);
						Pawn->TeleportTo(Spawn.GetLocation(), Spawn.Rotator(), false, true);
						Controller->SetControlRotation(Spawn.Rotator());
					}
				}
				IntervalTime = 0.0;
				WorstFrame = 0.0;
				IntervalFrames = 0;
			}
			return true;
		}
	};
}
#endif

class FHypeCubeJamModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		// A join that fails, or a server that goes away, ends in the title menu with a line saying why.
		FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
		{
			if (GEngine != nullptr)
			{
				GEngine->OnNetworkFailure().AddLambda([](UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
				{
					if (NetDriver == nullptr || NetDriver->ServerConnection != nullptr)
					{
						UE_LOG(LogHCJ, Log, TEXT("Menu: network failure on the client side (%s): %s"), ENetworkFailure::ToString(FailureType), *ErrorString);
						HCJMenuFlow::NotifyFailure(FailureType == ENetworkFailure::ConnectionLost || FailureType == ENetworkFailure::ConnectionTimeout
							? TEXT("Connection to the host lost.") : TEXT("Could not connect to that host."));
					}
				});
				GEngine->OnTravelFailure().AddLambda([](UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString)
				{
					UE_LOG(LogHCJ, Log, TEXT("Menu: travel failure (%s): %s"), ETravelFailure::ToString(FailureType), *ErrorString);
					HCJMenuFlow::NotifyFailure(TEXT("Could not connect to that host."));
				});
			}
		});

#if !UE_BUILD_SHIPPING
		// -HCJQuitAfter=<seconds>: an unattended run (bot match, capture, host + client test) ends by itself, so nothing has to stop it from outside.
		float QuitAfter = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("HCJQuitAfter="), QuitAfter) && QuitAfter > 0.0f)
		{
			QuitTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Left = QuitAfter](float DeltaSeconds) mutable
			{
				Left -= DeltaSeconds;
				if (Left > 0.0f)
				{
					return true;
				}
				UE_LOG(LogHCJ, Log, TEXT("Quitting: the time given with -HCJQuitAfter is up"));
				FPlatformMisc::RequestExit(false);
				return false;
			}));
		}

		float Interval = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("HCJCapture="), Interval) && Interval > 0.0f)
		{
			TSharedRef<FHCJCapture> Capture = MakeShared<FHCJCapture>();
			Capture->Interval = Interval;
			Capture->bAutoFly = FParse::Param(FCommandLine::Get(), TEXT("HCJAutoFly"));
			Capture->bAutoTorpedo = FParse::Param(FCommandLine::Get(), TEXT("HCJAutoTorpedo"));
			Capture->bAutoWarp = FParse::Param(FCommandLine::Get(), TEXT("HCJAutoWarp"));
			Capture->bAutoAim = FParse::Param(FCommandLine::Get(), TEXT("HCJAutoAim"));
			FParse::Value(FCommandLine::Get(), TEXT("HCJAutoAimError="), Capture->AutoAimErrorDeg);
			Capture->bOrbit = FParse::Param(FCommandLine::Get(), TEXT("HCJOrbit"));
			Capture->bTour = FParse::Param(FCommandLine::Get(), TEXT("HCJTour"));
			Capture->bSkyCam = FParse::Param(FCommandLine::Get(), TEXT("HCJSkyCam"));
			FParse::Value(FCommandLine::Get(), TEXT("HCJTourStart="), Capture->TourSector);
			FParse::Value(FCommandLine::Get(), TEXT("HCJSkyIndex="), Capture->SkyIndex);
			FParse::Value(FCommandLine::Get(), TEXT("HCJFrameDump="), Capture->FrameDumpCount);
			FParse::Value(FCommandLine::Get(), TEXT("HCJFrameDumpStart="), Capture->FrameDumpStart);
			FParse::Value(FCommandLine::Get(), TEXT("HCJHoleTest="), Capture->HoleTestFraction);
			FParse::Value(FCommandLine::Get(), TEXT("HCJHoleTestEvery="), Capture->HoleTestEvery);
			Capture->HoleTestEvery = FMath::Max(Capture->HoleTestEvery, 2);
			FParse::Value(FCommandLine::Get(), TEXT("HCJCaptureCmd="), Capture->OnceCommand);
			FParse::Value(FCommandLine::Get(), TEXT("HCJCaptureEveryCmd="), Capture->EveryCommand);
			FString ShotCommands;
			if (FParse::Value(FCommandLine::Get(), TEXT("HCJCaptureCmds="), ShotCommands, /*bShouldStopOnSeparator*/ false))
			{
				ShotCommands.ReplaceInline(TEXT("_"), TEXT(" "));
				ShotCommands.ParseIntoArray(Capture->ShotCommands, TEXT(";"), /*CullEmpty*/ false);
			}
			FString DemoCommands;
			if (FParse::Value(FCommandLine::Get(), TEXT("HCJDemoCmds="), DemoCommands, /*bShouldStopOnSeparator*/ false))
			{
				// Spaces cannot be passed everywhere: an underscore stands for one.
				DemoCommands.ReplaceInline(TEXT("_"), TEXT(" "));
				DemoCommands.ParseIntoArray(Capture->DemoCommands, TEXT(";"), /*CullEmpty*/ true);
			}
			CaptureTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Capture](float DeltaSeconds) { return Capture->Tick(DeltaSeconds); }));
		}
#endif
	}

	virtual void ShutdownModule() override
	{
		if (CaptureTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(CaptureTicker);
			CaptureTicker.Reset();
		}
		if (QuitTicker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(QuitTicker);
			QuitTicker.Reset();
		}
	}

private:
	FTSTicker::FDelegateHandle CaptureTicker;
	FTSTicker::FDelegateHandle QuitTicker;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FHypeCubeJamModule, HypeCubeJam, "HypeCubeJam");
