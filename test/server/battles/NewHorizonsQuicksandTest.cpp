/*
 * NewHorizonsQuicksandTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
class NewHorizonsQuicksandRuntimeTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
	}

	void prepareQuicksand()
	{
		prepareCommands(true);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 0, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, attackerSideHero->manaLimit());
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MAGIC_SCHOOL_SKILL, BonusSource::OTHER, 3, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
		attackerSideHero->addSpellToSpellbook(SpellID::QUICKSAND);
	}

	BattleAction quicksandAction(std::initializer_list<int> hexes) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::QUICKSAND;
		for(const auto hex : hexes)
			action.aimToHex(BattleHex(hex));
		return action;
	}
};
}

TEST(NewHorizonsQuicksandTest, SelectedActionVectorRequiresTheNewSerializationFeature)
{
	BattleAction action;
	action.side = BattleSide::ATTACKER;
	action.actionType = EActionType::HERO_SPELL;
	action.spell = SpellID::QUICKSAND;
	action.aimToHex(BattleHex(70));
	action.aimToHex(BattleHex(71));

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	ASSERT_EQ(restored.target.size(), 2u);
	EXPECT_EQ(restored.target[0].hexValue, BattleHex(70));
	EXPECT_EQ(restored.target[1].hexValue, BattleHex(71));
	auto oldReader = CMemorySerializer(current.extractBuffer());
	oldReader.iser.version = ESerializationVersion::NEW_HORIZONS_SIEGE_RATING;
	BattleAction oldRestored;
	EXPECT_THROW(oldReader.iser & oldRestored, std::runtime_error);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_SIEGE_RATING;
	EXPECT_THROW(old.oser & action, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST_F(NewHorizonsQuicksandRuntimeTest, ServerRejectsMalformedPlacementsBeforeManaOrActionSpend)
{
	prepareQuicksand();
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), quicksandAction({70})));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), quicksandAction({70, 70})));
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(70), 10);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), quicksandAction({70, 71})));

	battle()->battlefieldType = BattleField(BattleField::decode("core:ship_to_ship"));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), quicksandAction({6, 71})));

	EXPECT_TRUE(battle()->obstacles.empty());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
}

TEST_F(NewHorizonsQuicksandRuntimeTest, AcceptedPlacementKeepsOrderedEffectTargetsAndRedactsStartAction)
{
	prepareQuicksand();
	server.startedActions.clear();
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), quicksandAction({70, 71})));

	ASSERT_EQ(battle()->obstacles.size(), 2u);
	const std::array expected{BattleHex(70), BattleHex(71)};
	for(size_t index = 0; index < expected.size(); ++index)
	{
		const auto * patch = dynamic_cast<const SpellCreatedObstacle *>(battle()->obstacles[index].get());
		ASSERT_NE(patch, nullptr);
		EXPECT_EQ(patch->ID, SpellID::QUICKSAND);
		EXPECT_EQ(patch->pos, expected[index]);
		EXPECT_TRUE(patch->hidden);
		EXPECT_FALSE(patch->nativeVisible);
		EXPECT_TRUE(patch->visibleForSide(BattleSide::ATTACKER, true));
		EXPECT_FALSE(patch->visibleForSide(BattleSide::DEFENDER, true));
	}

	ASSERT_EQ(server.startedActions.size(), 1u);
	EXPECT_EQ(server.startedActions.back().ba.spell, SpellID::QUICKSAND);
	EXPECT_TRUE(server.startedActions.back().ba.target.empty());
	EXPECT_LT(attackerSideHero->getManaAvailable(), mana);
	EXPECT_NE(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
}
