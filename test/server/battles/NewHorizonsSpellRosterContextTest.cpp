/*
 * NewHorizonsSpellRosterContextTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../spells/NewHorizonsMagicProfileFixture.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/callback/CGameInfoCallback.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include <limits>

namespace
{
SpellID arrow()
{
	return SpellID(SpellID::decode("core:magicArrow"));
}

class RosterWorld final : public CGameInfoCallback
{
	CGameState & state;
public:
	JsonNode snapshot;
	RosterWorld(CGameState & state, JsonNode snapshot) : state(state), snapshot(std::move(snapshot)) {}
	CGameState & gameState() override { return state; }
	const CGameState & gameState() const override { return state; }
	const JsonNode & getMagicRules() const override { return snapshot; }
};

}

class NewHorizonsSpellRosterContextTest : public HeroCommandFixture
{
protected:
	bool legacyMagic = false;
	std::unique_ptr<newHorizonsTest::MagicV1Baseline> baseline;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		baseline = std::make_unique<newHorizonsTest::MagicV1Baseline>();
	}
	void TearDown() override
	{
		HeroCommandFixture::TearDown();
		baseline.reset();
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			legacyMagic ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
};

TEST_F(NewHorizonsSpellRosterContextTest, ActualWorldAndBattleQueriesDoNotReplaceMapBans)
{
	startGame();
	ASSERT_NE(arrow(), SpellID::NONE);
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), arrow()));
	gameState()->getMap().allowedSpells.erase(arrow());
	EXPECT_FALSE(gameState()->isAllowed(arrow()));
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), arrow()));
	startBattle();
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), arrow()));
}

TEST_F(NewHorizonsSpellRosterContextTest, LegacySnapshotKeepsOrdinarySpellsButMissingBattleIsNotWorldFallback)
{
	legacyMagic = true;
	startGame();
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), arrow()));
	CPlayerBattleCallback missing(nullptr, PlayerColor(0));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByBattleRoster(missing, arrow()));
	startBattle();
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), arrow()));
	EXPECT_FALSE(newHorizonsMagic::spellBelongsToRules(gameState()->getMagicRules(), "new-horizons:magicMissile", true));
	// Managed identity admission here is string-level only. No new spell imported.
}

TEST_F(NewHorizonsSpellRosterContextTest, InvalidAndNullRegistryEntriesFailBeforeDefinitionDereference)
{
	startGame();
	startBattle();
	for(const auto id : {SpellID(SpellID::NONE), SpellID(-2), SpellID(std::numeric_limits<int32_t>::max())})
	{
		EXPECT_FALSE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), id));
		EXPECT_FALSE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), id));
	}
	struct NullRegistrySlot
	{
		SpellID id;
		NullRegistrySlot() : id(static_cast<int32_t>(LIBRARY->spellh->objects.size()))
		{ LIBRARY->spellh->objects.emplace_back(nullptr); }
		~NullRegistrySlot() { LIBRARY->spellh->objects.pop_back(); }
	} empty;
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), empty.id));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByBattleRoster(*battle(), empty.id));
}

TEST_F(NewHorizonsSpellRosterContextTest, CopiedAdmissionAndCapturedBattleDoNotFollowLaterWorldRowChanges)
{
	startGame();
	RosterWorld other(*gameState(), gameState()->getMagicRules());
	BattleInfo captured(&other);
	const bool admitted = newHorizonsMagic::spellAllowedByWorldRoster(other, arrow());
	ASSERT_TRUE(admitted);
	other.snapshot["spells"].Struct().erase("core:magicArrow");
	// Adversarial query fixture only: this table is intentionally not admissible
	// to the full loader, which still requires non-NH common-spell coverage.
	EXPECT_THROW(newHorizonsMagic::validateRules(other.snapshot), std::runtime_error);
	EXPECT_FALSE(newHorizonsMagic::spellAllowedByWorldRoster(other, arrow()));
	EXPECT_TRUE(newHorizonsMagic::spellAllowedByBattleRoster(captured, arrow()));
	EXPECT_TRUE(admitted);
}
