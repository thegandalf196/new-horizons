/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../../lib/callback/BattleAISelection.h"
#include "../../lib/callback/CAdventureAI.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/BattleAction.h"
#include "../../AI/BattleAI/BattleSearchPolicy.h"
#include <stdexcept>

TEST(BattleAISelectionTest, HumanQuickCombatKeepsExistingReplayAndForceRules)
{
	using battleAISelection::automaticBattle;
	EXPECT_FALSE(automaticBattle(true, false, true, false, false));
	EXPECT_FALSE(automaticBattle(true, false, false, true, false));
	EXPECT_TRUE(automaticBattle(true, true, true, false, false));
	EXPECT_TRUE(automaticBattle(true, true, false, true, false));
	EXPECT_TRUE(automaticBattle(true, false, false, false, true));
	EXPECT_FALSE(automaticBattle(true, true, false, false, false));
}

TEST(BattleAISelectionTest, BattlesWithoutHumansUseAutomaticProfileWithoutQuickToggle)
{
	using namespace battleAISelection;
	// Covers both AI versus AI and AI versus a neutral guard: neither has a human participant.
	EXPECT_TRUE(automaticBattle(false, false, false, false, false));
	EXPECT_EQ(factoryName(automaticBattle(false, false, false, false, false), "BattleAI"), "BattleAIQuick");
	EXPECT_EQ(factoryName(automaticBattle(false, false, false, false, false), "BattleAI", "BattleAI"), "BattleAI");
}

TEST(BattleAISelectionTest, OldProfilesDefaultAutomaticCombatToQuickAndExplicitFullWins)
{
	using battleAISelection::factoryName;
	EXPECT_EQ(factoryName(true, "BattleAI"), "BattleAIQuick");
	EXPECT_EQ(factoryName(true, "StupidAI"), "BattleAIQuick");
	EXPECT_EQ(factoryName(true, "BattleAI", "BattleAIQuick"), "BattleAIQuick");
	EXPECT_EQ(factoryName(true, "BattleAIQuick", "BattleAI"), "BattleAI");
	EXPECT_THROW(factoryName(true, "BattleAI", ""), std::invalid_argument);
	EXPECT_THROW(factoryName(true, "BattleAI", "BattleAIBeamOne"), std::invalid_argument);
}

TEST(BattleAISelectionTest, ManualOpponentAndVisibleAutofightKeepOrdinaryChoice)
{
	using battleAISelection::factoryName;
	EXPECT_EQ(factoryName(false, "BattleAI"), "BattleAI");
	EXPECT_EQ(factoryName(false, "BattleAIBeamOne", "BattleAIQuick"), "BattleAIBeamOne");
	EXPECT_EQ(factoryName(false, "StupidAI", "BattleAI"), "StupidAI");
	// An automatic-only setting is not read/validated on the ordinary path.
	EXPECT_EQ(factoryName(false, "MMAI", "unrelated"), "MMAI");
}

namespace
{
class SelectionAdventureAI final : public CAdventureAI
{
public:
	mutable unsigned ordinaryReads = 0;
	std::string getBattleAIName() const override { ++ordinaryReads; return "EmptyAI"; }
	void heroGotLevel(const CGHeroInstance *, PrimarySkill, std::vector<SecondarySkill> &,
		const std::vector<newHorizonsHeroes::PerkOfferCandidate> &, QueryID) override {}
	void commanderGotLevel(const CCommanderInstance *, std::vector<ui32>, QueryID) override {}
	void showBlockingDialog(const std::string &, const std::vector<Component> &, QueryID,
		int, bool, bool, bool) override {}
	void showGarrisonDialog(const CArmedInstance *, const CGHeroInstance *, bool, QueryID,
		const MetaString &) override {}
	void showTeleportDialog(const CGHeroInstance *, TeleportChannelID, TTeleportExitsList, bool, QueryID) override {}
	void showMapObjectSelectDialog(QueryID, const Component &, const MetaString &, const MetaString &,
		const std::vector<ObjectInstanceID> &) override {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
};

void startEmptyBattle(SelectionAdventureAI & ai, BattleID id)
{
	ai.battleStart(id, nullptr, nullptr, {}, nullptr, nullptr, BattleSide::ATTACKER, false);
}
}

TEST(BattleAISelectionTest, AdventureContextIsBoundToBattleAndConsumedOnFailure)
{
	SelectionAdventureAI ai;
	ai.cbc = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	ai.prepareBattleAISelection(BattleID(1), true);
	// A stale automatic selection must neither affect another battle nor persist after rejection.
	EXPECT_THROW(startEmptyBattle(ai, BattleID(2)), std::logic_error);
	EXPECT_FALSE(ai.battleAI);
	EXPECT_EQ(ai.ordinaryReads, 0u);
	startEmptyBattle(ai, BattleID(2));
	EXPECT_NE(ai.battleAI, nullptr);
	EXPECT_EQ(ai.ordinaryReads, 1u);
	EXPECT_THROW(ai.prepareBattleAISelection(BattleID(3), true), std::logic_error);
	ai.battleEnd(BattleID(2), nullptr, QueryID(-1));
	EXPECT_FALSE(ai.battleAI);
}

TEST(BattleAISelectionTest, ManualAdventureContextClearsAnUnstartedAutomaticSelection)
{
	SelectionAdventureAI ai;
	ai.cbc = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	ai.prepareBattleAISelection(BattleID(1), true, "BattleAI");
	ai.prepareBattleAISelection(BattleID(2), false);
	startEmptyBattle(ai, BattleID(2));
	EXPECT_NE(ai.battleAI, nullptr);
	EXPECT_EQ(ai.ordinaryReads, 1u);
	ai.battleEnd(BattleID(2), nullptr, QueryID(-1));
	startEmptyBattle(ai, BattleID(3));
	EXPECT_EQ(ai.ordinaryReads, 2u);
	ai.battleEnd(BattleID(3), nullptr, QueryID(-1));
}

TEST(BattleAISelectionTest, InvalidPreparedSettingCannotReuseThePreviousBattleChoice)
{
	SelectionAdventureAI ai;
	ai.cbc = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	ai.prepareBattleAISelection(BattleID(1), true);
	EXPECT_THROW(ai.prepareBattleAISelection(BattleID(1), true, "unsupported"), std::invalid_argument);
	startEmptyBattle(ai, BattleID(1));
	EXPECT_EQ(ai.ordinaryReads, 1u);
	ai.battleEnd(BattleID(1), nullptr, QueryID(-1));
}

TEST(BattleAISelectionTest, AutomaticSelectionReusesExistingProfilesWithoutArmingSearch)
{
	using battleSearch::Profile;
	EXPECT_EQ(battleSearch::profileForAIName(battleAISelection::factoryName(true, "BattleAI")), Profile::BeamOneTwoActors);
	EXPECT_EQ(battleSearch::profileForAIName(battleAISelection::factoryName(true, "BattleAI", "BattleAI")), Profile::Full);
	EXPECT_FALSE(battleSearch::forecastBeamOne());
	EXPECT_EQ(battleSearch::forecastFutureActors(), 0u);
}
