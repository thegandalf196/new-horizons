/*
 * NewHorizonsSpellRosterConsumerTest.cpp, part of VCMI engine
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
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/callback/CGameInfoCallback.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include <limits>

namespace
{
SpellID spellNamed(const std::string & name) { return SpellID(SpellID::decode(name)); }

// Intentionally adversarial read context, not a valid partial-core save. Actual
// managed-identity import and valid old/new saved-roster tests are a later gate.
class ExcludingWorld final : public CGameInfoCallback
{
	CGameState & state;
	JsonNode rules;
	GameSettings settings;
public:
	ExcludingWorld(CGameState & state, const std::string & excluded) : state(state), rules(state.getMagicRules())
	{
		rules["spells"].Struct().erase(excluded);
		settings.loadBase(state.getSettings().getFullConfig());
		settings.addOverride(EGameSettings::SPELLS_TOMES_GRANT_BANNED_SPELLS, JsonNode(true));
	}
	CGameState & gameState() override { return state; }
	const CGameState & gameState() const override { return state; }
	const JsonNode & getMagicRules() const override { return rules; }
	const IGameSettings & getSettings() const override { return settings; }
};

class ScopedHeroCallback
{
	CGHeroInstance & hero;
	IGameInfoCallback * prior;
public:
	ScopedHeroCallback(CGHeroInstance & hero, IGameInfoCallback * replacement)
		: hero(hero), prior(hero.cb) { hero.cb = replacement; }
	~ScopedHeroCallback() { hero.cb = prior; }
};

class ExcludingBattle final : public BattleInfo
{
	JsonNode rules;
public:
	ExcludingBattle(IGameInfoCallback * world, JsonNode rules) : BattleInfo(world), rules(std::move(rules)) {}
	const JsonNode & getMagicRules() const override { return rules; }
};

class ScopedArmyBattleLinks
{
	CGHeroInstance & left;
	CGHeroInstance & right;
	BattleInfo * leftBattle;
	BattleInfo * rightBattle;
public:
	ScopedArmyBattleLinks(CGHeroInstance & left, CGHeroInstance & right)
		: left(left), right(right), leftBattle(left.battle), rightBattle(right.battle) {}
	~ScopedArmyBattleLinks() { left.battle = leftBattle; right.battle = rightBattle; }
};
}

class NewHorizonsSpellRosterConsumerTest : public HeroCommandFixture
{
protected:
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
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}
	void prepareHero(bool book = true)
	{
		startGame();
		attackerSideHero->removeAllSpells();
		if(book)
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	}
};

TEST_F(NewHorizonsSpellRosterConsumerTest, ExcludedKnownBookRemainsStoredButCannotGrant)
{
	prepareHero();
	const auto arrow = spellNamed("core:magicArrow");
	attackerSideHero->addSpellToSpellbook(arrow);
	ASSERT_FALSE(attackerSideHero->getSourcesForSpell(arrow).empty());
	ExcludingWorld excluded(*gameState(), "core:magicArrow");
	EXPECT_THROW(newHorizonsMagic::validateRules(excluded.getMagicRules()), std::runtime_error);
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	EXPECT_TRUE(attackerSideHero->getSpellsInSpellbook().count(arrow));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(arrow).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(arrow.toSpell()));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SpecificSpellBonusCannotBypassRoster)
{
	prepareHero(false);
	const auto arrow = spellNamed("core:magicArrow");
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(arrow)));
	ASSERT_FALSE(attackerSideHero->getSourcesForSpell(arrow).empty());
	ExcludingWorld excluded(*gameState(), "core:magicArrow");
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(arrow).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(arrow.toSpell()));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, SchoolGrantAndTomesGrantBannedSettingCannotBypassRoster)
{
	prepareHero();
	const auto arrow = spellNamed("core:magicArrow");
	ASSERT_TRUE(arrow.toSpell()->schools.count(SpellSchool::AIR));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLS_OF_SCHOOL,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(SpellSchool::AIR)));
	ASSERT_FALSE(attackerSideHero->getSourcesForSpell(arrow).empty());
	ExcludingWorld excluded(*gameState(), "core:magicArrow");
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	ASSERT_TRUE(excluded.getSettings().getBoolean(EGameSettings::SPELLS_TOMES_GRANT_BANNED_SPELLS));
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(arrow).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(arrow.toSpell()));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, LevelGrantCannotBypassRoster)
{
	prepareHero();
	const auto implosion = spellNamed("core:implosion");
	const auto level = attackerSideHero->getSpellLevel(implosion.toSpell());
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELLS_OF_LEVEL,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype::spellLevel(level))));
	ASSERT_FALSE(attackerSideHero->getSourcesForSpell(implosion).empty());
	ExcludingWorld excluded(*gameState(), "core:implosion");
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	EXPECT_TRUE(attackerSideHero->getSourcesForSpell(implosion).empty());
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(implosion.toSpell()));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, AllowBannedCannotLearnAnExcludedUnknownSpell)
{
	prepareHero();
	const auto arrow = spellNamed("core:magicArrow");
	ASSERT_TRUE(attackerSideHero->canLearnSpell(arrow.toSpell(), true));
	ExcludingWorld excluded(*gameState(), "core:magicArrow");
	ScopedHeroCallback context(*attackerSideHero, &excluded);
	EXPECT_FALSE(attackerSideHero->canLearnSpell(arrow.toSpell(), false));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(arrow.toSpell(), true));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, BothAllowedSpellEnumeratorsFilterBeforeLevelLookup)
{
	prepareHero();
	const auto arrow = spellNamed("core:magicArrow");
	ASSERT_TRUE(gameState()->getMap().allowedSpells.count(arrow));
	ExcludingWorld excluded(*gameState(), "core:magicArrow");
	EXPECT_FALSE(excluded.isAllowed(arrow));
	std::vector<SpellID> worldResult;
	excluded.getAllowedSpells(worldResult, std::nullopt);
	EXPECT_FALSE(vstd::contains(worldResult, arrow));
	std::vector<SpellID> mapResult;
	static_cast<MapInfoCallback &>(excluded).getAllowedSpells(mapResult, 1);
	EXPECT_FALSE(vstd::contains(mapResult, arrow));
	EXPECT_TRUE(excluded.getSpellSchools(arrow).empty());
	EXPECT_EQ(excluded.getSpellLevel(arrow), 0);
}

TEST_F(NewHorizonsSpellRosterConsumerTest, InvalidIdsFailBeforeSchoolLevelCostAndGrantDereference)
{
	prepareHero();
	for(const auto id : {SpellID(SpellID::NONE), SpellID(-2), SpellID(std::numeric_limits<int32_t>::max())})
	{
		EXPECT_TRUE(newHorizonsMagic::spellSchools(gameState()->getMagicRules(), id).empty());
		EXPECT_EQ(newHorizonsMagic::spellLevel(gameState()->getMagicRules(), id), 0);
		EXPECT_THROW(newHorizonsMagic::spellCost(gameState()->getMagicRules(), id, 0), std::runtime_error);
		EXPECT_TRUE(attackerSideHero->getSourcesForSpell(id).empty());
	}
	EXPECT_FALSE(attackerSideHero->canCastThisSpell(nullptr));
	EXPECT_FALSE(attackerSideHero->canLearnSpell(nullptr, true));
}

TEST_F(NewHorizonsSpellRosterConsumerTest, CommonCastGateRejectsExcludedBattleDespiteAdmittingWorld)
{
	prepareHero();
	const auto arrow = spellNamed("core:magicArrow");
	attackerSideHero->addSpellToSpellbook(arrow);
	attackerSideHero->mana = 100;
	startBattle();
	beginCombat();
	spells::BattleCast original(battle(), attackerSideHero, spells::Mode::HERO, arrow.toSpell());
	spells::detail::ProblemImpl originalProblem;
	ASSERT_TRUE(arrow.toSpell()->battleMechanics(&original)->canBeCast(originalProblem));
	auto excluded = battle()->getMagicRules();
	excluded["spells"].Struct().erase("core:magicArrow");
	CMemorySerializer wire;
	wire.oser & *battle();
	wire.iser.cb = gameState().get();
	{
		ScopedArmyBattleLinks restore(*attackerSideHero, *defenderSideHero);
		ExcludingBattle incoming(gameState().get(), excluded);
		wire.iser & static_cast<BattleInfo &>(incoming);
		incoming.localInit();
		ASSERT_TRUE(newHorizonsMagic::spellAllowedByWorldRoster(*gameState(), arrow));
		ASSERT_FALSE(newHorizonsMagic::spellAllowedByBattleRoster(incoming, arrow));
		for(const auto mode : {spells::Mode::HERO, spells::Mode::PASSIVE})
		{
			spells::BattleCast cast(&incoming, attackerSideHero, mode, arrow.toSpell());
			spells::detail::ProblemImpl problem;
			EXPECT_FALSE(arrow.toSpell()->battleMechanics(&cast)->canBeCast(problem));
		}
	}
	EXPECT_EQ(attackerSideHero->battle, battle());
	EXPECT_EQ(defenderSideHero->battle, battle());
	// Delegated exclusion is synthetic and invalid as a full saved core roster;
	// actual managed-identity old-save and authoritative rejection gates remain.
}
