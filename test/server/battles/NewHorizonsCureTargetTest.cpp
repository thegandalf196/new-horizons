/*
 * NewHorizonsCureTargetTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include <algorithm>
#include <stdexcept>
#include "../../../lib/spells/CurePhysicalAffliction.h"
#include "../../../lib/battle/NewHorizonsGraveDust.h"

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

	void installGraveDust(CStack * recipient, int value = 5000)
	{
		SetStackEffect effects;
		effects.battleID = BattleID(0);
		effects.toAdd.emplace_back(recipient->unitId(), newHorizonsGraveDust::records(value, PlayerColor(1)));
		gameHandler->sendAndApply(effects);
		ASSERT_EQ(spells::curePhysical::graveDustRemoval(*recipient).size(), 2u);
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

TEST_F(NewHorizonsCureTargetTest, SelectedFrozenRejectsNonLivingWaterWithoutSpendingOrRemovingState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * water = addStack(BattleSide::ATTACKER, creatureByName("core:waterElemental"), BattleHex(4, 5), 100);
	ASSERT_NE(water, nullptr);
	ASSERT_TRUE(water->hasBonusOfType(BonusType::NON_LIVING));
	ASSERT_FALSE(water->hasBonusOfType(BonusType::UNDEAD));
	ASSERT_NO_FATAL_FAILURE(freeze(water));
	// A valid living Frozen ally keeps Cure available: rejection must be about
	// the Water Elemental's taxonomy, not the absence of any appropriate target.
	ASSERT_NO_FATAL_FAILURE(freeze(target));
	ASSERT_TRUE(canTarget({spells::Destination(target)}, "frozen"));
	EXPECT_FALSE(canTarget({spells::Destination(water)}, "frozen"));
	EXPECT_FALSE(canTarget({spells::Destination(water->getPosition())}, "frozen"));
	ASSERT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
	const auto mana = attackerSideHero->getManaAvailable();
	const auto casts = battle()->battleCastSpells(BattleSide::ATTACKER);
	const auto health = water->getAvailableHealth();
	const auto count = water->getCount();
	const auto stamp = water->frozenLastAppliedRound();
	const auto round = battle()->getBattle()->getRound();
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	const auto active = battle()->battleActiveUnit()->unitId();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::CURE;
	action.spellCurePhysicalAffliction = "frozen";
	action.aimToUnit(water);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), casts);
	EXPECT_EQ(battle()->battleCanCastSpell(attackerSideHero, spells::Mode::HERO), ESpellCastProblem::OK);
	EXPECT_EQ(water->getAvailableHealth(), health);
	EXPECT_EQ(water->getCount(), count);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*water));
	EXPECT_EQ(water->frozenLastAppliedRound(), stamp);
	EXPECT_FALSE(newHorizonsFrozen::canApply(*water, round));
	EXPECT_EQ(battle()->getBattle()->getRound(), round);
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	EXPECT_EQ(battle()->battleActiveUnit()->unitId(), active);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
	// The same unspent Hero Action can perform the valid paid living-target cast.
	action.target.clear();
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->getOwner(), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), casts + 1);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*water));
	EXPECT_EQ(water->frozenLastAppliedRound(), stamp);
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
	int64_t damage = 2;
	state->damage(damage);
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

TEST_F(NewHorizonsCureTargetTest, GraveDustSelectedCureAtFullHealthRemovesOnlyItsCanonicalGroup)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(installGraveDust(target, 10000));
	Bonus unrelated(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, BonusSource::OTHER, 2, BonusSourceID());
	auto retained = std::make_shared<Bonus>(unrelated);
	retained->stacking = "grave-dust-cleanse-unrelated-control";
	target->addNewBonus(retained);
	const auto health = target->getAvailableHealth();
	ASSERT_EQ(target->getSurvivingMissingHealth(), 0);
	ASSERT_TRUE(canTarget({spells::Destination(target)}, "graveDust"));
	ASSERT_TRUE(canTarget({spells::Destination(target->getPosition())}, "graveDust"));
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::CURE;
	action.spellCurePhysicalAffliction = "graveDust";
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(spells::curePhysical::graveDustRemoval(*target).empty());
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_TRUE(std::ranges::any_of(target->getExportedBonusList(), [&retained](const auto & bonus)
	{ return bonus == retained; }));
}

TEST_F(NewHorizonsCureTargetTest, GraveDustDoesNotRelaxCureLivingBodyOrOwnerAdmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(installGraveDust(target));
	const auto mana = attackerSideHero->getManaAvailable();
	for(const auto & entry : std::vector<std::pair<std::string, BattleHex>>{
		{"core:skeleton", BattleHex(4, 1)}, {"core:waterElemental", BattleHex(7, 1)}, {"core:ballista", BattleHex(10, 1)}})
	{
		auto * body = addStack(BattleSide::ATTACKER, creatureByName(entry.first), entry.second, 1);
		ASSERT_NE(body, nullptr);
		ASSERT_NO_FATAL_FAILURE(installGraveDust(body));
		EXPECT_FALSE(canTarget({spells::Destination(body)}, "graveDust"));
		EXPECT_EQ(spells::curePhysical::graveDustRemoval(*body).size(), 2u);
	}
	ASSERT_NO_FATAL_FAILURE(installGraveDust(enemy));
	EXPECT_FALSE(canTarget({spells::Destination(enemy)}, "graveDust"));
	EXPECT_FALSE(canTarget({spells::Destination(target)}, "unrelatedPhysicalKind"));
	EXPECT_FALSE(canTarget({spells::Destination(target)}, "graveDust", SpellID::DISEASE));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsCureTargetTest, GraveDustDoesNotBecomeAnOrdinaryDispelEffect)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(installGraveDust(target));
	const auto * dispel = SpellID(SpellID::DISPEL).toSpell();
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, dispel);
	EXPECT_FALSE(dispel->battleMechanics(&cast)->canBeCastAt({spells::Destination(target)}));
	Bonus slow(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT,
		-2, BonusSourceID(SpellID(SpellID::SLOW)));
	slow.turnsRemain = 3;
	SetStackEffect effect;
	effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{slow});
	gameHandler->sendAndApply(effect);
	ASSERT_TRUE(dispel->battleMechanics(&cast)->canBeCastAt({spells::Destination(target)}));
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::DISPEL;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_FALSE(target->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::SLOW)))));
	EXPECT_EQ(spells::curePhysical::graveDustRemoval(*target).size(), 2u);
}

TEST_F(NewHorizonsCureTargetTest, SelectedGraveDustRemovalPrecedesTheSameCureHealingReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	StacksInjured injury;
	injury.battleID = BattleID(0);
	auto & hit = injury.stacks.emplace_back();
	hit.stackAttacked = target->unitId();
	hit.damageAmount = 5;
	target->prepareAttacked(hit, gameHandler->getRandomGenerator());
	gameHandler->sendAndApply(injury);
	ASSERT_NO_FATAL_FAILURE(installGraveDust(target, 10000));
	const auto before = target->getAvailableHealth();
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::CURE;
	action.spellCurePhysicalAffliction = "graveDust";
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(spells::curePhysical::graveDustRemoval(*target).empty());
	EXPECT_GT(target->getAvailableHealth(), before);
}

TEST_F(NewHorizonsCureTargetTest, ForgedGraveDustPhysicalGroupFailsClosedBeforeCureCosts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto malformed = newHorizonsGraveDust::records(5000, PlayerColor(1)).front();
	malformed.source = BonusSource::HERO_SPECIAL;
	target->addNewBonus(std::make_shared<Bonus>(malformed)); // Deliberately invalid installed-state witness.
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_THROW(canTarget({spells::Destination(target)}, "graveDust"), std::invalid_argument);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}
