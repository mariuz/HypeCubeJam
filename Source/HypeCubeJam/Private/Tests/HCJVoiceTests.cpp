#include "Tests/HCJTestHelpers.h"

#include "Assets/HCJAssetSettings.h"
#include "Audio/HCJVoiceLogic.h"
#include "Sound/SoundWave.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags HCJVoiceTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float LineSeconds = 1.3f;
}

/**
 * The ship computer's rate limiter: off means silent, nothing right after a spawn, per-line and per-group cooldowns,
 * one voice at a time (an urgent line cuts a lower one off, nothing else does), the gap between lines, dropped
 * requests are not queued, the welcome line plays once, and a busy minute stays under the cap.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJVoiceLimiterTest, "HypeCubeJam.Audio.VoiceLimiter", HCJVoiceTestFlags)

bool FHCJVoiceLimiterTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const float Grace = FHCJVoiceLimiter::SpawnGraceSeconds;
	const float Gap = FHCJVoiceLimiter::GapSeconds;

	// The table is sane: every line has a wave, urgent lines exist, the four shield lines share a group.
	for (int32 Index = 1; Index < static_cast<int32>(EHCJVoiceLine::Count); ++Index)
	{
		const FHCJVoiceRule& Rule = FHCJVoiceLimiter::GetRule(static_cast<EHCJVoiceLine>(Index));
		HCJ_TEST_CHECK(Rule.Asset[0] != 0 && Rule.Priority > 0 && Rule.Cooldown >= 8.0f, TEXT("line %d has no wave, no priority or a cooldown under 8 s"), Index);
	}
	TestTrue(TEXT("facing 0 is the fore shield"), FHCJVoiceLimiter::ShieldLine(0) == EHCJVoiceLine::ShieldFore);
	TestTrue(TEXT("facing 3 is the starboard shield"), FHCJVoiceLimiter::ShieldLine(3) == EHCJVoiceLine::ShieldStarboard);
	TestTrue(TEXT("no fifth facing"), FHCJVoiceLimiter::ShieldLine(4) == EHCJVoiceLine::None);
	TestTrue(TEXT("a torpedo is urgent"), FHCJVoiceLimiter::GetRule(EHCJVoiceLine::TorpedoIncoming).Priority >= FHCJVoiceLimiter::UrgentPriority);
	TestTrue(TEXT("a clear sector is not"), FHCJVoiceLimiter::GetRule(EHCJVoiceLine::SectorClear).Priority < FHCJVoiceLimiter::UrgentPriority);

	// Switched off: silent, and nothing is remembered against the line.
	{
		FHCJVoiceLimiter Limiter;
		Limiter.NotifySpawn(0.0f);
		TestTrue(TEXT("off: dropped"), Limiter.Request(EHCJVoiceLine::TorpedoIncoming, 10.0f, LineSeconds, false) == EHCJVoiceDecision::Drop);
		TestTrue(TEXT("on again: spoken at once"), Limiter.Request(EHCJVoiceLine::TorpedoIncoming, 10.1f, LineSeconds, true) == EHCJVoiceDecision::Play);
		TestEqual(TEXT("one dropped"), Limiter.GetNumDropped(), 1);
	}

	// Right after a spawn nothing is said, not even the most urgent line; and it is not said later either.
	{
		FHCJVoiceLimiter Limiter;
		Limiter.NotifySpawn(100.0f);
		TestTrue(TEXT("in the grace period: dropped"), Limiter.Request(EHCJVoiceLine::TorpedoIncoming, 100.0f + Grace - 0.1f, LineSeconds, true) == EHCJVoiceDecision::Drop);
		TestTrue(TEXT("nothing is being spoken afterwards (not queued)"), Limiter.GetCurrent(100.0f + Grace + 1.0f) == EHCJVoiceLine::None);
		TestTrue(TEXT("after the grace period: spoken"), Limiter.Request(EHCJVoiceLine::TorpedoIncoming, 100.0f + Grace, LineSeconds, true) == EHCJVoiceDecision::Play);
		TestTrue(TEXT("it is the current line"), Limiter.GetCurrent(100.0f + Grace + 0.5f) == EHCJVoiceLine::TorpedoIncoming);
		TestTrue(TEXT("and over when its wave is"), Limiter.GetCurrent(100.0f + Grace + LineSeconds + 0.01f) == EHCJVoiceLine::None);
	}

	// One voice at a time, the gap, the cooldown of a line and of a group.
	{
		FHCJVoiceLimiter Limiter;
		Limiter.NotifySpawn(0.0f);
		float Now = 10.0f;
		TestTrue(TEXT("fore shield: spoken"), Limiter.Request(EHCJVoiceLine::ShieldFore, Now, LineSeconds, true) == EHCJVoiceDecision::Play);
		TestTrue(TEXT("enemy behind while it speaks: dropped"), Limiter.Request(EHCJVoiceLine::EnemyBehind, Now + 0.5f, LineSeconds, true) == EHCJVoiceDecision::Drop);
		TestTrue(TEXT("enemy behind inside the gap: dropped"), Limiter.Request(EHCJVoiceLine::EnemyBehind, Now + LineSeconds + Gap - 0.1f, LineSeconds, true) == EHCJVoiceDecision::Drop);
		Now += LineSeconds + Gap;
		TestTrue(TEXT("enemy behind after the gap: spoken (a drop does not start its cooldown)"), Limiter.Request(EHCJVoiceLine::EnemyBehind, Now, LineSeconds, true) == EHCJVoiceDecision::Play);
		Now += LineSeconds + Gap;
		// 10 + 2 * (1.3 + 5) = 22.6: the shield group's 30 s are not over (started at 10).
		TestTrue(TEXT("another shield facing inside the group's cooldown: dropped"), Limiter.Request(EHCJVoiceLine::ShieldAft, Now, LineSeconds, true) == EHCJVoiceDecision::Drop);
		TestTrue(TEXT("the same facing: dropped as well"), Limiter.Request(EHCJVoiceLine::ShieldFore, Now, LineSeconds, true) == EHCJVoiceDecision::Drop);
		const float ShieldCooldown = FHCJVoiceLimiter::GetRule(EHCJVoiceLine::ShieldFore).Cooldown;
		TestTrue(TEXT("another facing after the cooldown: spoken"), Limiter.Request(EHCJVoiceLine::ShieldAft, 10.0f + ShieldCooldown + 0.1f, LineSeconds, true) == EHCJVoiceDecision::Play);
		TestEqual(TEXT("three lines so far"), Limiter.GetNumPlayed(), 3);
	}

	// Urgent lines: cut a lower line off, need only the short gap, never cut each other off downwards.
	{
		FHCJVoiceLimiter Limiter;
		Limiter.NotifySpawn(0.0f);
		TestTrue(TEXT("sector clear: spoken"), Limiter.Request(EHCJVoiceLine::SectorClear, 10.0f, LineSeconds, true) == EHCJVoiceDecision::Play);
		TestTrue(TEXT("a shield line does not cut it off"), Limiter.Request(EHCJVoiceLine::ShieldPort, 10.3f, LineSeconds, true) == EHCJVoiceDecision::Drop);
		TestTrue(TEXT("a torpedo does"), Limiter.Request(EHCJVoiceLine::TorpedoIncoming, 10.5f, LineSeconds, true) == EHCJVoiceDecision::Interrupt);
		TestTrue(TEXT("the torpedo line is the current one"), Limiter.GetCurrent(10.6f) == EHCJVoiceLine::TorpedoIncoming);
		TestTrue(TEXT("hull critical does not cut the torpedo line off"), Limiter.Request(EHCJVoiceLine::HullCritical, 10.8f, LineSeconds, true) == EHCJVoiceDecision::Drop);
		const float End = 10.5f + LineSeconds;
		TestTrue(TEXT("urgent, inside the short gap: dropped"), Limiter.Request(EHCJVoiceLine::HullCritical, End + FHCJVoiceLimiter::UrgentGapSeconds - 0.1f, LineSeconds, true) == EHCJVoiceDecision::Drop);
		TestTrue(TEXT("urgent, after the short gap: spoken"), Limiter.Request(EHCJVoiceLine::HullCritical, End + FHCJVoiceLimiter::UrgentGapSeconds, LineSeconds, true) == EHCJVoiceDecision::Play);
		const float TorpedoCooldown = FHCJVoiceLimiter::GetRule(EHCJVoiceLine::TorpedoIncoming).Cooldown;
		TestTrue(TEXT("a second torpedo inside the line's cooldown: dropped"), Limiter.Request(EHCJVoiceLine::TorpedoIncoming, 10.5f + TorpedoCooldown - 0.1f, LineSeconds, true) == EHCJVoiceDecision::Drop);
		TestTrue(TEXT("after it: spoken"), Limiter.Request(EHCJVoiceLine::TorpedoIncoming, 10.5f + TorpedoCooldown + 0.1f, LineSeconds, true) == EHCJVoiceDecision::Play);
	}

	// The welcome line: once, whatever happens later; a respawn ends whatever was being said.
	{
		FHCJVoiceLimiter Limiter;
		Limiter.NotifySpawn(0.0f);
		TestTrue(TEXT("welcome: spoken"), Limiter.Request(EHCJVoiceLine::Welcome, Grace, LineSeconds, true) == EHCJVoiceDecision::Play);
		Limiter.NotifySpawn(Grace + 0.5f);
		TestTrue(TEXT("a new ship: silent"), Limiter.GetCurrent(Grace + 0.6f) == EHCJVoiceLine::None);
		TestTrue(TEXT("welcome on the second ship: dropped"), Limiter.Request(EHCJVoiceLine::Welcome, 500.0f, LineSeconds, true) == EHCJVoiceDecision::Drop);
		TestTrue(TEXT("welcome an hour later: dropped"), Limiter.Request(EHCJVoiceLine::Welcome, 3600.0f, LineSeconds, true) == EHCJVoiceDecision::Drop);
		TestEqual(TEXT("spoken once"), Limiter.GetNumPlayed(EHCJVoiceLine::Welcome), 1);
	}

	// A fight in which everything happens all the time, asked every tenth of a second for ten minutes: never two
	// voices at once, never less than the gap between two lines (the short gap before an urgent one), never more
	// than the cap of non-urgent lines in a minute, and no line twice within its cooldown.
	{
		FHCJVoiceLimiter Limiter;
		Limiter.NotifySpawn(0.0f);
		FRandomStream Random(7);
		float LastEnd = -1000.0f;
		float LastStartOf[static_cast<int32>(EHCJVoiceLine::Count)];
		for (float& Time : LastStartOf)
		{
			Time = -1.0e9f;
		}
		TArray<float> Starts;
		int32 NumUrgent = 0;
		for (int32 Step = 0; Step < 6000; ++Step)
		{
			const float Now = 5.0f + 0.1f * Step;
			const EHCJVoiceLine Line = static_cast<EHCJVoiceLine>(Random.RandRange(2, static_cast<int32>(EHCJVoiceLine::Count) - 1));
			const FHCJVoiceRule& Rule = FHCJVoiceLimiter::GetRule(Line);
			const bool bUrgent = Rule.Priority >= FHCJVoiceLimiter::UrgentPriority;
			const EHCJVoiceDecision Decision = Limiter.Request(Line, Now, LineSeconds, true);
			if (Decision == EHCJVoiceDecision::Drop)
			{
				continue;
			}
			if (Decision == EHCJVoiceDecision::Play)
			{
				HCJ_TEST_CHECK(Now - LastEnd >= (bUrgent ? FHCJVoiceLimiter::UrgentGapSeconds : Gap) - 0.001f, TEXT("line %d started %.2f s after the last one ended"), static_cast<int32>(Line), Now - LastEnd);
			}
			else
			{
				HCJ_TEST_CHECK(bUrgent && Now < LastEnd, TEXT("line %d cut a line off at %.1f without being urgent, or cut silence off"), static_cast<int32>(Line), Now);
			}
			HCJ_TEST_CHECK(Now - LastStartOf[static_cast<int32>(Line)] >= Rule.Cooldown - 0.001f, TEXT("line %d again after %.1f s"), static_cast<int32>(Line), Now - LastStartOf[static_cast<int32>(Line)]);
			LastStartOf[static_cast<int32>(Line)] = Now;
			LastEnd = Now + LineSeconds;
			NumUrgent += bUrgent ? 1 : 0;
			if (!bUrgent)
			{
				int32 InLastMinute = 0;
				for (const float Start : Starts)
				{
					InLastMinute += Now - Start < 60.0f ? 1 : 0;
				}
				HCJ_TEST_CHECK(InLastMinute < FHCJVoiceLimiter::MaxPerMinute, TEXT("a non-urgent line at %.1f s although %d lines started in the minute before"), Now, InLastMinute);
			}
			{
				// The hard cap holds for every line, urgent or not.
				int32 InLastMinute = 0;
				for (const float Start : Starts)
				{
					InLastMinute += Now - Start < 60.0f ? 1 : 0;
				}
				HCJ_TEST_CHECK(InLastMinute < FHCJVoiceLimiter::HardMaxPerMinute, TEXT("a line at %.1f s although %d lines started in the minute before"), Now, InLastMinute);
			}
			Starts.Add(Now);
		}
		// Ten minutes of the worst case: nothing gets past the hard cap, so at most HardMaxPerMinute lines per started minute.
		HCJ_TEST_CHECK(Starts.Num() >= 30 && Starts.Num() <= 10 * FHCJVoiceLimiter::HardMaxPerMinute, TEXT("%d lines in ten minutes of constant requests"), Starts.Num());
		HCJ_TEST_CHECK(NumUrgent > 0, TEXT("%d urgent lines of %d"), NumUrgent, Starts.Num());
		AddInfo(FString::Printf(TEXT("worst case: %d lines in 600 s (%d urgent), %d requests dropped"), Starts.Num(), NumUrgent, Limiter.GetNumDropped()));
	}

	// The match nobody enjoys: a ship every 20 s (17 s alive), an enemy behind it all the time (the HUD's cue asks every
	// 4 s), two shield facings and the hull going down in every life, two missiles and a torpedo per life. The voice
	// stays under the hard cap in every minute and no line is heard more than twice in any minute.
	{
		FHCJVoiceLimiter Limiter;
		TArray<float> Starts;
		TArray<EHCJVoiceLine> Lines;
		const auto Ask = [&Limiter, &Starts, &Lines](EHCJVoiceLine Line, float Now)
		{
			if (Limiter.Request(Line, Now, LineSeconds, true) != EHCJVoiceDecision::Drop)
			{
				Starts.Add(Now);
				Lines.Add(Line);
			}
		};
		for (int32 Life = 0; Life < 30; ++Life)
		{
			const float Spawn = 20.0f * Life;
			Limiter.NotifySpawn(Spawn);
			// (In the order of time within a life; tenths of a second.)
			for (int32 Tenth = 0; Tenth < 170; ++Tenth)
			{
				const float Now = Spawn + 0.1f * Tenth;
				if (Tenth >= 30 && (Tenth - 30) % 40 == 0) { Ask(EHCJVoiceLine::EnemyBehind, Now); }
				if (Tenth == 45) { Ask(EHCJVoiceLine::MissileLock, Now); }
				if (Tenth == 60) { Ask(EHCJVoiceLine::ShieldAft, Now); }
				if (Tenth == 85) { Ask(EHCJVoiceLine::TorpedoIncoming, Now); }
				if (Tenth == 95) { Ask(EHCJVoiceLine::ShieldPort, Now); }
				if (Tenth == 125) { Ask(EHCJVoiceLine::MissileLock, Now); }
				if (Tenth == 140) { Ask(EHCJVoiceLine::HullCritical, Now); }
			}
		}
		for (int32 Index = 0; Index < Starts.Num(); ++Index)
		{
			int32 InMinute = 0;
			int32 SameInMinute = 0;
			for (int32 Other = Index; Other < Starts.Num() && Starts[Other] - Starts[Index] < 60.0f; ++Other)
			{
				++InMinute;
				SameInMinute += Lines[Other] == Lines[Index] ? 1 : 0;
			}
			HCJ_TEST_CHECK(InMinute <= FHCJVoiceLimiter::HardMaxPerMinute, TEXT("%d lines in the minute from %.1f s"), InMinute, Starts[Index]);
			HCJ_TEST_CHECK(SameInMinute <= 2, TEXT("line %d was spoken %d times in the minute from %.1f s"), static_cast<int32>(Lines[Index]), SameInMinute, Starts[Index]);
		}
		// Ten minutes: between one line every 20 s and the hard cap.
		HCJ_TEST_CHECK(Starts.Num() >= 30 && Starts.Num() <= 10 * FHCJVoiceLimiter::HardMaxPerMinute, TEXT("%d lines in ten minutes of dying every 20 s"), Starts.Num());
		AddInfo(FString::Printf(TEXT("dying every 20 s: %d lines in 600 s, %d requests dropped"), Starts.Num(), Limiter.GetNumDropped()));
	}

	// Refusals carry their reason (the "Voice: ... declined" log line).
	{
		FHCJVoiceLimiter Limiter;
		Limiter.NotifySpawn(0.0f);
		Limiter.Request(EHCJVoiceLine::HullCritical, 10.0f, LineSeconds, false);
		TestTrue(TEXT("off: reason is the setting"), Limiter.GetLastRefusal() == EHCJVoiceRefusal::Disabled);
		Limiter.Request(EHCJVoiceLine::HullCritical, 1.0f, LineSeconds, true);
		TestTrue(TEXT("just spawned: reason is the grace"), Limiter.GetLastRefusal() == EHCJVoiceRefusal::SpawnGrace);
		TestTrue(TEXT("spoken"), Limiter.Request(EHCJVoiceLine::HullCritical, 10.0f, LineSeconds, true) == EHCJVoiceDecision::Play && Limiter.GetLastRefusal() == EHCJVoiceRefusal::None);
		Limiter.Request(EHCJVoiceLine::EnemyBehind, 10.5f, LineSeconds, true);
		TestTrue(TEXT("while a line is spoken: busy"), Limiter.GetLastRefusal() == EHCJVoiceRefusal::Busy);
		Limiter.Request(EHCJVoiceLine::EnemyBehind, 12.0f, LineSeconds, true);
		TestTrue(TEXT("right after a line: gap"), Limiter.GetLastRefusal() == EHCJVoiceRefusal::Gap);
		Limiter.Request(EHCJVoiceLine::HullCritical, 30.0f, LineSeconds, true);
		TestTrue(TEXT("the same line again: cooldown"), Limiter.GetLastRefusal() == EHCJVoiceRefusal::Cooldown);
		for (int32 Reason = 1; Reason <= static_cast<int32>(EHCJVoiceRefusal::HardMinuteCap); ++Reason)
		{
			HCJ_TEST_CHECK(FHCJVoiceLimiter::DescribeRefusal(static_cast<EHCJVoiceRefusal>(Reason))[0] != 0, TEXT("refusal %d has no text"), Reason);
		}
	}

	HCJ_TEST_FINISH();
}

/** The configured waves are the twelve lines of the table, in its order (UHCJAssetSettings::VoiceLines is indexed by the line). */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHCJVoiceLinesConfigTest, "HypeCubeJam.Audio.VoiceLinesConfig", HCJVoiceTestFlags)

bool FHCJVoiceLinesConfigTest::RunTest(const FString& Parameters)
{
	int32 NumFailures = 0;
	const UHCJAssetSettings& Settings = UHCJAssetSettings::Get();
	const int32 NumLines = static_cast<int32>(EHCJVoiceLine::Count) - 1;
	TestEqual(TEXT("one wave per line"), Settings.VoiceLines.Num(), NumLines);
	for (int32 Index = 0; Index < Settings.VoiceLines.Num() && Index < NumLines; ++Index)
	{
		const FHCJVoiceRule& Rule = FHCJVoiceLimiter::GetRule(static_cast<EHCJVoiceLine>(Index + 1));
		HCJ_TEST_CHECK(Settings.VoiceLines[Index].GetAssetName() == Rule.Asset, TEXT("VoiceLines[%d] is %s, the table says %s"), Index, *Settings.VoiceLines[Index].ToString(), Rule.Asset);
		HCJ_TEST_CHECK(Settings.VoiceLines[Index].GetLongPackageName().StartsWith(TEXT("/Game/HCJ/Audio/Voice/")), TEXT("VoiceLines[%d] is not under /Game/HCJ/Audio/Voice"), Index);
		const USoundWave* Wave = Cast<USoundWave>(Settings.VoiceLines[Index].TryLoad());
		HCJ_TEST_CHECK(Wave != nullptr && Wave->GetDuration() > 0.8f && Wave->GetDuration() < 2.5f && !Wave->bLooping, TEXT("VoiceLines[%d] (%s) does not load as a short one-shot wave"), Index, Rule.Asset);
	}
	TestTrue(TEXT("the voice is not muted by the config"), Settings.VoiceVolume > 0.0f);

	HCJ_TEST_FINISH();
}

#endif // WITH_DEV_AUTOMATION_TESTS
