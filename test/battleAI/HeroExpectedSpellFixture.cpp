/*
 * HeroExpectedSpellFixture.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroExpectedSpellFixture.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"

namespace heroExpectedTest
{
void HeroExpectedSpellTest::prepareExpected(const std::string & key)
{
	ASSERT_NO_FATAL_FAILURE(world()); // Canonical captures, fixture seed, one existing book.
	selectedSpell = SpellID(SpellID::decode(key));
	ASSERT_TRUE(selectedSpell.hasValue());
	ASSERT_TRUE(attackerSideHero->hasSpellbook());
	attackerSideHero->removeAllSpells();
	attackerSideHero->addSpellToSpellbook(selectedSpell);
	const auto chaos = SecondarySkill(SecondarySkill::decode("new-horizons:chaosMagic"));
	ASSERT_TRUE(chaos.hasValue());
	attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	const bool fate = key == "new-horizons:handOfFate";
	const bool mental = key == "new-horizons:confusion" || key == "new-horizons:puppetMaster";
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, fate ? 1000 : 100, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(battle()->getMagicRules(), selectedSpell));
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	active = addStack(BattleSide::ATTACKER, creatureByName(fate ? "core:stoneGolem" : "core:pikeman"),
		mental ? BattleHex(3, 5) : BattleHex(3, 4), fate ? 1 : mental ? 20 : 2000);
	waiter = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 7), 1);
	enemy = addStack(BattleSide::DEFENDER, creatureByName(mental ? "core:archer" : "core:powerLich"),
		mental ? BattleHex(12, 5) : BattleHex(14, 4), fate ? 100 : mental ? 50 : 1000);
	actor = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"),
		mental ? BattleHex(8, 5) : BattleHex(14, 7), mental ? 100 : 1000);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(waiter, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_NE(actor, nullptr);
	int initiative = 1000;
	for(const auto * unit : {active, enemy, waiter, actor})
	{
		effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, initiative, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		initiative -= 200;
	}
	for(const auto * unit : {active, waiter, actor})
		effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
			BonusSource::OTHER, -static_cast<int64_t>(unit->getMovementRange()), BonusSourceID()));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_EQ(battle()->battleActiveUnit(), active);
	ASSERT_EQ(active->getMovementRange(), 0);
	ASSERT_TRUE(enemy->willMove(0));
	if(mental)
	{
		ASSERT_FALSE(enemy->hasBonusOfType(BonusType::MIND_IMMUNITY));
	}
	ASSERT_TRUE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	const auto * spell = selectedSpell.toSpell();
	ASSERT_TRUE(spell->canBeCast(battle(), spells::Mode::HERO, attackerSideHero));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const spells::Target target{spells::Destination(enemy)};
	ASSERT_TRUE(spell->battleMechanics(&cast)->canBeCastAt(target));
}

void HeroExpectedSpellTest::expectMentalPressure(bool confusion)
{
	ASSERT_TRUE(enemy->alive());
	ASSERT_TRUE(enemy->willMove(0));
	ASSERT_TRUE(enemy->isShooter());
	ASSERT_FALSE(enemy->hasBonusOfType(BonusType::MIND_IMMUNITY));
	ASSERT_FALSE(enemy->confusionState.pending);
	ASSERT_FALSE(newHorizonsPuppetMaster::hasControlMarker(enemy));
	ASSERT_TRUE(battle()->battleCanShootAction(enemy, active->getPosition()));
	// characterize() has retained the normal cold/warm raw observations before this probe.
	auto projected = detached();
	const auto outside = observation();
	const auto * target = projected->battleGetUnitByID(enemy->unitId());
	ASSERT_NE(target, nullptr);
	DamageCache damage;
	PotentialTargets ordinary(target, damage, projected);
	ASSERT_FALSE(ordinary.possibleAttacks.empty());
	ASSERT_GT(ordinary.bestActionValue(), 0);
	const auto * spell = selectedSpell.toSpell();
	spells::BattleCast preview(projected.get(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&preview);
	const spells::Target aim{spells::Destination(target)};
	ASSERT_TRUE(mechanics->canBeCastAt(aim));
	if(confusion)
	{
		const auto expected = SpellTargetEvaluator::confusionExpectedActivationValue(
			mechanics.get(), aim, environment.get(), projected);
		ASSERT_TRUE(expected);
		ASSERT_GT(*expected, 0.0f);
	}
	else
	{
		mechanics->castEval(projected->getServerCallback(), aim);
		const auto * controlled = projected->battleGetUnitByID(enemy->unitId());
		ASSERT_NE(controlled, nullptr);
		// The retained pre-cast view stays unchanged across the child COW update.
		ASSERT_NE(controlled, target);
		ASSERT_FALSE(newHorizonsPuppetMaster::hasControlMarker(target));
		ASSERT_EQ(projected->battleGetActionController(target), PlayerColor(1));
		ASSERT_TRUE(newHorizonsPuppetMaster::hasValidControlMarker(*projected, controlled));
		ASSERT_EQ(projected->battleGetActionController(controlled), PlayerColor(0));
		ASSERT_EQ(projected->battleGetOwner(controlled), PlayerColor(1));
		ASSERT_TRUE(projected->battleCanShootAction(controlled, actor->getPosition()));
		DamageCache controlledDamage;
		PotentialTargets controlledActions(controlled, controlledDamage, projected);
		ASSERT_FALSE(controlledActions.possibleAttacks.empty());
		ASSERT_GT(controlledActions.bestActionValue(), 0);
	}
	ASSERT_EQ(observation(), outside);
}

void HeroExpectedSpellTest::expectSelectedSpell()
{
	ASSERT_NO_FATAL_FAILURE(characterize(true)); // Real selector, complete existing raw observation.
	const auto & key = selectedSpell.toSpell()->getJsonKey();
	if(key == "new-horizons:confusion" || key == "new-horizons:puppetMaster")
	{
		ASSERT_NO_FATAL_FAILURE(expectMentalPressure(key == "new-horizons:confusion"));
	}
	ASSERT_EQ(callback->heroRequests.size(), 1u);
	const auto & action = callback->heroRequests.front();
	ASSERT_EQ(action.actionType, EActionType::HERO_SPELL);
	ASSERT_EQ(action.spell, selectedSpell);
}

std::shared_ptr<HypotheticBattle> HeroExpectedSpellTest::detached()
{
	if(!parent)
	{
		callback = std::make_shared<heroQueueTest::QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		for(const auto * unit : live->battleGetAllUnits(false))
			(void)unit->ableToRetaliate();
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
	return std::make_shared<HypotheticBattle>(environment.get(), parent);
}

void HeroExpectedSpellTest::prepareTransition(const std::shared_ptr<HypotheticBattle> & model,
	std::optional<HypotheticBattle::ProjectedSpellAllowance> & prepared, bool begin)
{
	prepared = model->prepareHeroSpellAllowance(BattleSide::ATTACKER, selectedSpell, false, false);
	ASSERT_TRUE(prepared);
	if(begin)
	{
		ASSERT_TRUE(model->beginProjectedHeroAction(BattleSide::ATTACKER, *prepared));
	}
}
}
