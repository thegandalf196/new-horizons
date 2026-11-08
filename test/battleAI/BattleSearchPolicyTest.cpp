/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../../AI/BattleAI/BattleSearchPolicy.h"
#include <stdexcept>
#include <thread>

TEST(BattleSearchPolicyTest, ExistingFactoryNameRemainsFullAndOptInNamesAreExplicit)
{
	using namespace battleSearch;
	EXPECT_EQ(profileForAIName("BattleAI"), Profile::Full);
	EXPECT_EQ(profileForAIName("BattleAIBeamOne"), Profile::BeamOne);
	EXPECT_EQ(profileForAIName("BattleAIQuick"), Profile::BeamOneTwoActors);
	EXPECT_FALSE(profileForAIName("StupidAI"));
	EXPECT_FALSE(profileForAIName("battleaiquick"));
	EXPECT_FALSE(forecastBeamOne());
	EXPECT_EQ(forecastFutureActors(), 0u);
}

TEST(BattleSearchPolicyTest, QuickAndUnlimitedProfilesWorkWithoutDiagnosticStartup)
{
	using namespace battleSearch;
	{
		const Scope quick(Profile::BeamOneTwoActors);
		EXPECT_TRUE(forecastBeamOne());
		EXPECT_EQ(forecastFutureActors(), 2u);
		{
			const Scope beam(Profile::BeamOne);
			EXPECT_TRUE(forecastBeamOne());
			EXPECT_EQ(forecastFutureActors(), 0u);
		}
		EXPECT_EQ(forecastFutureActors(), 2u);
		{
			const Scope reference(Profile::Full);
			EXPECT_FALSE(forecastBeamOne());
			EXPECT_EQ(forecastFutureActors(), 0u);
		}
		EXPECT_EQ(forecastFutureActors(), 2u);
	}
	EXPECT_FALSE(forecastBeamOne());
}

TEST(BattleSearchPolicyTest, InvalidAndThrowingNestedScopesCannotLeakToTheNextDecision)
{
	using namespace battleSearch;
	{
		const Scope quick(Profile::BeamOneTwoActors);
		EXPECT_THROW(Scope(static_cast<Profile>(99)), std::invalid_argument);
		EXPECT_EQ(forecastFutureActors(), 2u);
		try
		{
			const Scope reference(Profile::Full);
			throw std::runtime_error("test decision exit");
		}
		catch(const std::runtime_error &) {}
		EXPECT_EQ(forecastFutureActors(), 2u);
	}
	EXPECT_FALSE(forecastBeamOne());
	EXPECT_EQ(forecastFutureActors(), 0u);
}

TEST(BattleSearchPolicyTest, WorkerStartsFullAndMustEstablishItsOwnDecisionProfile)
{
	using namespace battleSearch;
	const Scope parent(Profile::BeamOneTwoActors);
	bool workerStartedFull = false, workerOptIn = false, workerRestoredFull = false;
	std::thread worker([&]
	{
		workerStartedFull = !forecastBeamOne() && forecastFutureActors() == 0;
		{
			const Scope ownDecision(Profile::BeamOneTwoActors);
			workerOptIn = forecastBeamOne() && forecastFutureActors() == 2;
		}
		workerRestoredFull = !forecastBeamOne() && forecastFutureActors() == 0;
	});
	worker.join();
	EXPECT_TRUE(workerStartedFull);
	EXPECT_TRUE(workerOptIn);
	EXPECT_TRUE(workerRestoredFull);
	EXPECT_EQ(forecastFutureActors(), 2u);
}
