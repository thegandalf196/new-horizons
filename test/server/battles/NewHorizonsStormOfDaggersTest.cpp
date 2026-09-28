/*
 * NewHorizonsStormOfDaggersTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto stormOfDaggersKey = "new-horizons:stormOfDaggers";

SpellID stormOfDaggersSpell()
{
	return SpellID(SpellID::decode(stormOfDaggersKey));
}
}

class NewHorizonsStormOfDaggersTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	std::vector<CStack *> enemies;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		rules["warcasting"] = JsonNode(false);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int spellPower = 100, int enemyCount = 6)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		const auto spell = stormOfDaggersSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
		for(int index = 0; index < enemyCount; ++index)
			enemies.push_back(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
				BattleHex(12, 2 + index), 1000));
		beginCombat();
	}

	BattleAction action(const std::vector<CStack *> & targets) const
	{
		BattleAction result;
		result.actionType = EActionType::HERO_SPELL;
		result.side = BattleSide::ATTACKER;
		result.spell = stormOfDaggersSpell();
		for(const auto * target : targets)
			result.aimToUnit(target);
		return result;
	}
};

TEST_F(NewHorizonsStormOfDaggersTest, SharedForecastMatchesCanonicalPoolAndEqualSplitTable)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spellId = stormOfDaggersSpell();
	const auto * spell = spellId.toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spellId));
	EXPECT_EQ(newHorizonsMagic::spellCost(attackerSideHero->getMagicRules(), spellId, MasteryLevel::NONE), 8);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), 10);

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	auto mechanics = spell->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->isNewHorizonsStormOfDaggers());
	EXPECT_EQ(mechanics->getEffectValue(), 195);

	const std::array<int64_t, 5> expectedTotals{195, 224, 254, 283, 312};
	const std::array<int64_t, 5> expectedPerTarget{195, 112, 85, 71, 62};
	for(int32_t count = 1; count <= 5; ++count)
	{
		EXPECT_EQ(mechanics->getStormOfDaggersTotalDamage(count), expectedTotals[count - 1]) << count;
		EXPECT_EQ(mechanics->getStormOfDaggersDamagePerTarget(count), expectedPerTarget[count - 1]) << count;
	}

	// The authored table rounds independently: at N=3, each target receives 85
	// even though the displayed rounded total is 254. The half values round up.
	EXPECT_EQ(mechanics->getStormOfDaggersTotalDamage(3), 254);
	EXPECT_EQ(mechanics->getStormOfDaggersDamagePerTarget(3), 85);
	EXPECT_FALSE(mechanics->setStormOfDaggersTargetCount(0));
	EXPECT_FALSE(mechanics->setStormOfDaggersTargetCount(6));
	ASSERT_TRUE(mechanics->setStormOfDaggersTargetCount(3));
	EXPECT_EQ(mechanics->getEffectValue(), 85);
}

TEST_F(NewHorizonsStormOfDaggersTest, SavedSorceryRankScalesOnlySpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = stormOfDaggersSpell();
	const auto sorcery = SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic"));
	ASSERT_TRUE(sorcery.hasValue());

	const std::array<int64_t, 4> expectedPool{195, 218, 240, 263};
	const std::array<int64_t, 4> expectedPowerTerm{150, 173, 195, 218};
	for(size_t rank = 0; rank < expectedPool.size(); ++rank)
	{
		attackerSideHero->setSecSkillLevel(sorcery, static_cast<MasteryLevel::Type>(rank), ChangeValueMode::ABSOLUTE);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		const auto mechanics = spell.toSpell()->battleMechanics(&cast);
		EXPECT_EQ(mechanics->getEffectValue(), expectedPool[rank]);
		EXPECT_EQ(mechanics->getEffectValue() - 45, expectedPowerTerm[rank]);
	}

	attackerSideHero->setSecSkillLevel(sorcery, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1, ChangeValueMode::ABSOLUTE);
	spells::BattleCast half(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	EXPECT_EQ(spell.toSpell()->battleMechanics(&half)->getStormOfDaggersDamagePerTarget(1), 47);
}

TEST_F(NewHorizonsStormOfDaggersTest, AuthoritativeCastDamagesEveryChosenEnemyWithPerTargetResistance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const std::vector<CStack *> selected{enemies[0], enemies[1], enemies[2]};
	selected[1]->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	const std::array<int64_t, 3> beforeHealth{
		selected[0]->getAvailableHealth(), selected[1]->getAvailableHealth(), selected[2]->getAvailableHealth()};
	const auto beforeMana = attackerSideHero->getManaAvailable();

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, stormOfDaggersSpell().toSpell());
	auto mechanics = stormOfDaggersSpell().toSpell()->battleMechanics(&cast);
	ASSERT_TRUE(mechanics->setStormOfDaggersTargetCount(3));
	const auto resistedForecast = mechanics->adjustEffectValue(selected[1]);
	ASSERT_LT(resistedForecast, mechanics->getStormOfDaggersDamagePerTarget(3));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(selected)));
	EXPECT_EQ(beforeHealth[0] - selected[0]->getAvailableHealth(), 85);
	EXPECT_EQ(beforeHealth[1] - selected[1]->getAvailableHealth(), resistedForecast);
	EXPECT_EQ(beforeHealth[2] - selected[2]->getAvailableHealth(), 85);
	EXPECT_EQ(beforeMana - attackerSideHero->getManaAvailable(), 8);
	ASSERT_EQ(server.castsOf(stormOfDaggersSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(stormOfDaggersSpell()).front().damage,
		85 + resistedForecast + 85);
}

TEST_F(NewHorizonsStormOfDaggersTest, InvalidFullTargetVectorRejectsAtomicallyBeforeActionOrMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const std::array<int64_t, 2> beforeHealth{
		enemies[0]->getAvailableHealth(), enemies[1]->getAvailableHealth()};
	const auto beforeMana = attackerSideHero->getManaAvailable();
	const auto beforeActions = server.startedActions.size();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		action({enemies[0], enemies[0]})));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		action({enemies[0], friendly})));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		action(enemies)));

	EXPECT_EQ(beforeHealth[0], enemies[0]->getAvailableHealth());
	EXPECT_EQ(beforeHealth[1], enemies[1]->getAvailableHealth());
	EXPECT_EQ(beforeMana, attackerSideHero->getManaAvailable());
	EXPECT_EQ(beforeActions, server.startedActions.size());
	EXPECT_TRUE(server.castsOf(stormOfDaggersSpell()).empty());
}
