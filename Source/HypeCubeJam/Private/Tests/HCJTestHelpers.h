#pragma once

#include "CoreMinimal.h"
#include "Hypercube/HCJHypercubeGraph.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Records a failed expectation inside an automation test's RunTest. The loops in these tests cover thousands
 * of cases, so only the first few failures are reported in full; HCJ_TEST_FINISH adds the total.
 * Requires a local `int32 NumFailures = 0;`. The format string must be a TEXT() literal with at least one argument.
 */
#define HCJ_TEST_CHECK(Condition, Format, ...) \
	do \
	{ \
		if (!(Condition)) \
		{ \
			if (++NumFailures <= HCJTest::MaxReportedFailures) \
			{ \
				AddError(FString::Printf(Format, __VA_ARGS__)); \
			} \
		} \
	} while (false)

/** Ends RunTest: summarises suppressed failures and returns the test result. */
#define HCJ_TEST_FINISH() \
	if (NumFailures > HCJTest::MaxReportedFailures) \
	{ \
		AddError(FString::Printf(TEXT("%d failed checks in total (only the first %d are listed)"), NumFailures, HCJTest::MaxReportedFailures)); \
	} \
	return NumFailures == 0

namespace HCJTest
{
	inline constexpr int32 MaxReportedFailures = 12;

	/** Tolerance in uu for distance comparisons (generation keeps 1 uu of slack inside every constraint). */
	inline constexpr double DistanceTolerance = 0.5;

	/** A spread of seeds: the awkward values first, then well-mixed ones. */
	inline int32 MakeSeed(int32 Index)
	{
		switch (Index)
		{
		case 0: return 0;
		case 1: return 1;
		case 2: return -1;
		case 3: return MAX_int32;
		case 4: return MIN_int32;
		default: return HCJ::MixSeed(0x7E57C0DE, Index);
		}
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
