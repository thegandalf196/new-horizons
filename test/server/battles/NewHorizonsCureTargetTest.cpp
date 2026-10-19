/*
 * NewHorizonsCureTargetTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
class NewHorizonsCureTargetTest : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare()
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 100);
		beginCombat();
		ASSERT_NE(target, nullptr);
		ASSERT_NE(enemy, nullptr);
	}

	void freeze(CStack * recipient)
	{
		SetStackEffect effects;
		effects.battleID = BattleID(0);
		effects.toAdd.emplace_back(recipient->unitId(), std::vector<Bonus>{
			newHorizonsFrozen::marker(BonusSourceID(creatureByName("core:iceElemental")),
				battle()->getBattle()->getRound())});
		gameHandler->sendAndApply(effects);
		ASSERT_TRUE(newHorizonsFrozen::isFrozen(*recipient));
	}

	bool canTarget(const spells::Target & aim, const std::string & physical = "",
		SpellID source = SpellID::NONE) const
	{
		const auto * cure = SpellID(SpellID::CURE).toSpell();
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, cure);
		cast.setCurePhysicalAffliction(physical);
		cast.setCureAffliction(source);
		return cure->battleMechanics(&cast)->canBeCastAt(aim);
	}
};
}

TEST_F(NewHorizonsCureTargetTest, SelectedFrozenAcceptsHumanHexAndExplicitUnitAtFullHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(newHorizonsFrozen::enabled(battle()->getMagicRules()));
	ASSERT_NO_FATAL_FAILURE(freeze(target));
	ASSERT_EQ(target->getSurvivingMissingHealth(), 0);
	EXPECT_TRUE(canTarget({spells::Destination(target)}, "frozen"));
	EXPECT_TRUE(canTarget({spells::Destination(target->getPosition())}, "frozen"));
}

TEST_F(NewHorizonsCureTargetTest, UnselectedFrozenStillRequiresPickerChoice)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(freeze(target));
	EXPECT_FALSE(canTarget({spells::Destination(target)}));
	EXPECT_FALSE(canTarget({spells::Destination(target->getPosition())}));
}

TEST_F(NewHorizonsCureTargetTest, InjuredHealOnlyTargetAcceptsHumanHexAndExplicitUnit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto state = target->acquireState();
	state->damage(2);
	BattleUnitsChanged change;
	change.battleID = BattleID(0);
	change.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
	change.changedStacks.back().data = state->save();
	change.changedStacks.back().healthDelta = -2;
	gameHandler->sendAndApply(change);
	ASSERT_EQ(target->getSurvivingMissingHealth(), 2);
	EXPECT_TRUE(canTarget({spells::Destination(target)}));
	EXPECT_TRUE(canTarget({spells::Destination(target->getPosition())}));
}

TEST_F(NewHorizonsCureTargetTest, FrozenSelectionRejectsInvalidEnemyMultipleAndStaleDestinations)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(freeze(target));
	ASSERT_NO_FATAL_FAILURE(freeze(enemy));
	EXPECT_FALSE(canTarget({}, "frozen"));
	EXPECT_FALSE(canTarget({spells::Destination(BattleHex::INVALID)}, "frozen"));
	EXPECT_FALSE(canTarget({spells::Destination(BattleHex(8, 5))}, "frozen"));
	EXPECT_FALSE(canTarget({spells::Destination(enemy)}, "frozen"));
	EXPECT_FALSE(canTarget({spells::Destination(enemy->getPosition())}, "frozen"));
	EXPECT_FALSE(canTarget({spells::Destination(target->getPosition()),
		spells::Destination(enemy->getPosition())}, "frozen"));
	// A valid occupied hex must never rescue an explicit identity absent from this battle.
	CStack stale;
	ASSERT_EQ(battle()->battleGetUnitByID(stale.unitId()), nullptr);
	EXPECT_FALSE(canTarget({spells::Destination(&stale, target->getPosition())}, "frozen"));
}

TEST_F(NewHorizonsCureTargetTest, SelectedSpellAfflictionRetainsHexAndUnitCompatibility)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto poison = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::POISON,
		BonusSource::SPELL_EFFECT, 30, BonusSourceID(SpellID(SpellID::POISON)));
	poison->turnsRemain = 3;
	target->addNewBonus(poison);
	EXPECT_TRUE(canTarget({spells::Destination(target)}, "", SpellID::POISON));
	EXPECT_TRUE(canTarget({spells::Destination(target->getPosition())}, "", SpellID::POISON));
	EXPECT_FALSE(canTarget({spells::Destination(target->getPosition())}, "", SpellID::DISEASE));
}

TEST_F(NewHorizonsCureTargetTest, OrdinaryDispelLeavesTypedFrozenMarker)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	auto slow = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, -1, BonusSourceID(SpellID(SpellID::SLOW)));
	slow->turnsRemain = 3;
	target->addNewBonus(slow);
	ASSERT_NO_FATAL_FAILURE(freeze(target));
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::DISPEL, target));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_TRUE(target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::SLOW))))->empty());
}
