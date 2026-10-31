/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMeleeSpecialtyFixture.h"
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/RecipientBonusOverlay.h"
#include "../../../lib/spells/IncomingMagicalDamage.h"
#include "../../../server/battles/BattleCreatureBurn.h"
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include <vcmi/Environment.h>
#endif
#include <limits>
namespace
{
namespace burn = newHorizonsCreatureBurn;
namespace fire = newHorizonsMagicalAbilityDamage;
class NewHorizonsCreatureBurnTest : public NewHorizonsMeleeSpecialtyFixture
{
protected:
	bool legacyMagic = false;
	void mapLoaded(CMap * map) override
	{
		NewHorizonsMeleeSpecialtyFixture::mapLoaded(map);
		if(legacyMagic)
		{
			// Match the existing legacy damage fixture: no current starting-book contract.
			map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
			map->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, JsonNode());
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
			captureLegacyPerkOfferPolicy(*map);
		}
	}
	void setup() { prepare("core:sorsha"); }
	Bonus pending(int damage = 20)
	{
		return burn::marker(HeroTypeID(HeroTypeID::decode("core:rashka")), actor->unitId(), damage);
	}
	void add(const Bonus & bonus) { victim->addNewBonus(std::make_shared<Bonus>(bonus)); }
	void reduction(int amount, SpellSchool school, BonusType type = BonusType::SPELL_DAMAGE_REDUCTION)
	{
		victim->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type,
			BonusSource::OTHER, amount, BonusSourceID(), BonusSubtypeID(school)));
	}
	bool hasBurn(const battle::Unit & unit) { return unit.hasBonus(CSelector(burn::isBurn)); }
};
#if ENABLE_BATTLE_AI
class BurnEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit BurnEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}
TEST(NewHorizonsCreatureBurnBudgetTest, BodyLossIsRoundedBoundedAndZeroNeverBecomesDamage)
{
	EXPECT_EQ(burn::damageBudget(0, 20), 0);
	EXPECT_EQ(burn::damageBudget(-1, 20), 0);
	EXPECT_EQ(burn::damageBudget(100, 0), 0);
	EXPECT_EQ(burn::damageBudget(1, 20), 1);
	EXPECT_EQ(burn::damageBudget(9, 20), 1);
	EXPECT_EQ(burn::damageBudget(100, 20), 20);
	EXPECT_EQ(burn::damageBudget(std::numeric_limits<int64_t>::max(), 100), std::numeric_limits<int32_t>::max());
	EXPECT_THROW(burn::damageBudget(1, 101), std::invalid_argument);
}
TEST_F(NewHorizonsCreatureBurnTest, MarkerClaimsRequireExactShapeAndSupportedHero)
{
	setup(); auto valid = pending();
	EXPECT_TRUE(burn::isBurn(&valid)); EXPECT_FALSE(burn::isBurn(nullptr));
	Bonus unrelated; EXPECT_FALSE(burn::isBurn(&unrelated));
	for(int corruption = 0; corruption < 8; ++corruption)
	{
		auto changed = valid;
		if(corruption == 0) changed.stacking.clear();
		if(corruption == 1) changed.statusIdentity.clear();
		if(corruption == 2) changed.duration = BonusDuration::N_TURNS;
		if(corruption == 3) changed.val = 0;
		if(corruption == 4) changed.parameters = std::make_shared<BonusParameters>(-1);
		if(corruption == 5) changed.statusTags = {BonusStatusTag::DEBUFF};
		if(corruption == 6) changed.appliedByEnemy = true;
		if(corruption == 7) changed.sid = BonusSourceID(HeroTypeID(HeroTypeID::decode("core:sorsha")));
		EXPECT_THROW(burn::isBurn(&changed), std::runtime_error);
	}
	EXPECT_THROW(burn::marker(HeroTypeID(HeroTypeID::decode("core:sorsha")), 0, 20), std::invalid_argument);
	EXPECT_THROW(burn::marker(HeroTypeID(HeroTypeID::decode("core:rashka")), UINT32_MAX, 20), std::invalid_argument);
}
TEST_F(NewHorizonsCreatureBurnTest, ExistingHeroAndNonPrimaryAttacksDoNotCreateBurn)
{
	setup(); BattleAttackInfo attack(actor, victim, 0, false);
	EXPECT_FALSE(burn::eligible(*battle(), attack));
	EXPECT_TRUE(burn::plan(*battle(), attack, 100).add.empty());
	attack.retaliation = true;
	EXPECT_TRUE(burn::plan(*battle(), attack, 100).add.empty());
	attack.secondaryAttack = true;
	EXPECT_TRUE(burn::plan(*battle(), attack, 100).add.empty());
}
TEST_F(NewHorizonsCreatureBurnTest, FireAndAnyMdrUseOneCapWhileGenericDamageStaysUnchanged)
{
	setup(); reduction(50, SpellSchool::ANY); reduction(50, SpellSchool::FIRE);
	ASSERT_TRUE(battle()->battleUsesNewHorizonsMultiplicativeMDR());
	EXPECT_EQ(fire::adjustDamage(*battle(), *victim, 100), 50);
	EXPECT_EQ(fire::adjustFireDamage(*battle(), *victim, 100), 25);
	reduction(90, SpellSchool::ANY);
	EXPECT_EQ(fire::adjustFireDamage(*battle(), *victim, 100), 5);
}
TEST_F(NewHorizonsCreatureBurnTest, FireImmunityIsDistinctFromAnySchoolAndCappedAnyMdr)
{
	setup(); reduction(1, SpellSchool::ANY, BonusType::SPELL_SCHOOL_IMMUNITY);
	EXPECT_FALSE(fire::fireImmune(*victim));
	reduction(100, SpellSchool::ANY);
	EXPECT_FALSE(fire::fireImmune(*victim));
	EXPECT_EQ(fire::adjustFireDamage(*battle(), *victim, 100), 5);
	reduction(1, SpellSchool::FIRE, BonusType::SPELL_SCHOOL_IMMUNITY);
	EXPECT_TRUE(fire::fireImmune(*victim));
	EXPECT_EQ(fire::adjustFireDamage(*battle(), *victim, 100), 0);
}
TEST_F(NewHorizonsCreatureBurnTest, NegativeImmunityAndExplicitFireReductionPreventBurnDamage)
{
	setup();
	for(const auto school : {SpellSchool::FIRE, SpellSchool::ANY})
	{
		reduction(1, school, BonusType::NEGATIVE_EFFECTS_IMMUNITY);
		EXPECT_TRUE(fire::fireImmune(*victim));
		victim->removeBonuses(Selector::type()(BonusType::NEGATIVE_EFFECTS_IMMUNITY));
	}
	reduction(100, SpellSchool::FIRE); EXPECT_TRUE(fire::fireImmune(*victim));
	victim->removeBonuses(Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION));
	reduction(10'000, SpellSchool::FIRE, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS);
	EXPECT_TRUE(fire::fireImmune(*victim));
}
TEST_F(NewHorizonsCreatureBurnTest, IncomingFireAndAllMagicalVulnerabilityApplyExactlyOnce)
{
	setup();
	add(Bonus(BonusDuration::ONE_BATTLE, BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED,
		BonusSource::OTHER, 100, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(SpellDamageElement::FIRE)))));
	add(Bonus(BonusDuration::ONE_BATTLE, BonusType::MORE_DAMAGE_FROM_SPELL, BonusSource::OTHER, 100, BonusSourceID()));
	EXPECT_EQ(spells::applyIncomingElementalDamageModifier(*victim, SpellDamageElement::FIRE, 100), 200);
	EXPECT_EQ(fire::adjustFireDamage(*battle(), *victim, 100), 400);
	EXPECT_EQ(fire::adjustDamage(*battle(), *victim, 100), 200);
}
TEST_F(NewHorizonsCreatureBurnTest, GenuineTickIsConsumedBeforeInjuryAndNeverRepeats)
{
	setup(); add(pending()); const auto before = victim->getAvailableHealth();
	EXPECT_TRUE(battleCreatureBurn::onActivation(*gameHandler, *battle(), *victim, BattleUnitTurnReason::TURN_QUEUE));
	EXPECT_EQ(victim->getAvailableHealth(), before - 20); EXPECT_FALSE(hasBurn(*victim));
	EXPECT_FALSE(battleCreatureBurn::onActivation(*gameHandler, *battle(), *victim, BattleUnitTurnReason::TURN_QUEUE));
	EXPECT_EQ(victim->getAvailableHealth(), before - 20);
}
TEST_F(NewHorizonsCreatureBurnTest, WaitAndContinuationReasonsDoNotConsumeOrTick)
{
	setup(); add(pending());
	for(const auto reason : {BattleUnitTurnReason::ACTION_REJECTED, BattleUnitTurnReason::MASTER_GATE_CONTINUATION,
		BattleUnitTurnReason::PURSUIT_CONTINUATION, BattleUnitTurnReason::DREAD_CAVALRY_CONTINUATION,
		BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION, BattleUnitTurnReason::HERO_SPELLCAST,
		BattleUnitTurnReason::UNIT_SPELLCAST, BattleUnitTurnReason::CRISIS_ORDER, BattleUnitTurnReason::CRISIS_RESUME})
		EXPECT_TRUE(burn::activationTick(*battle(), *victim, reason).remove.empty());
	victim->waiting = true;
	EXPECT_TRUE(burn::activationTick(*battle(), *victim, BattleUnitTurnReason::TURN_QUEUE).remove.empty());
	EXPECT_TRUE(burn::activationTick(*battle(), *victim, BattleUnitTurnReason::AUTOMATIC_ACTION).remove.empty());
	EXPECT_TRUE(hasBurn(*victim)); victim->waiting = false;
	EXPECT_EQ(burn::activationTick(*battle(), *victim, BattleUnitTurnReason::TURN_QUEUE).damage, 20);
}
TEST_F(NewHorizonsCreatureBurnTest, TimeStopSuspendsAndNewImmunityConsumesWithoutInjury)
{
	setup(); add(pending());
	add(Bonus(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_TRUE(burn::activationTick(*battle(), *victim, BattleUnitTurnReason::AUTOMATIC_ACTION).remove.empty());
	victim->removeBonuses(Selector::type()(BonusType::TIME_STOP)); reduction(100, SpellSchool::FIRE);
	const auto before = victim->getAvailableHealth();
	EXPECT_TRUE(battleCreatureBurn::onActivation(*gameHandler, *battle(), *victim, BattleUnitTurnReason::AUTOMATIC_ACTION));
	EXPECT_EQ(victim->getAvailableHealth(), before); EXPECT_FALSE(hasBurn(*victim));
}
TEST_F(NewHorizonsCreatureBurnTest, DeadAndGhostRecipientsCannotTick)
{
	setup(); add(pending()); auto dead = victim->acquireState();
	int64_t lethal = dead->getAvailableHealth(); dead->damage(lethal);
	EXPECT_TRUE(burn::activationTick(*battle(), *dead, BattleUnitTurnReason::TURN_QUEUE).remove.empty());
	auto ghost = victim->acquireState(); ghost->makeGhost();
	EXPECT_TRUE(burn::activationTick(*battle(), *ghost, BattleUnitTurnReason::AUTOMATIC_ACTION).remove.empty());
}


TEST_F(NewHorizonsCreatureBurnTest, FrozenAutomaticForfeitStillConsumesOneTickWithoutThawing)
{
	setup(); add(pending());
	add(newHorizonsFrozen::makeFrozenMarker(BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound()));
	ASSERT_TRUE(newHorizonsFrozen::isFrozen(*victim));
	const auto before = victim->getAvailableHealth();
	EXPECT_TRUE(battleCreatureBurn::onActivation(*gameHandler, *battle(), *victim, BattleUnitTurnReason::AUTOMATIC_ACTION));
	EXPECT_EQ(victim->getAvailableHealth(), before - 20);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*victim));
	EXPECT_FALSE(hasBurn(*victim));
}
TEST_F(NewHorizonsCreatureBurnTest, SourceDeathDoesNotCancelCapturedPendingTick)
{
	setup(); add(pending());
	int64_t lethal = actor->getAvailableHealth(); actor->damage(lethal);
	ASSERT_FALSE(actor->alive());
	const auto tick = burn::activationTick(*battle(), *victim, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(tick.source, actor->unitId()); EXPECT_EQ(tick.damage, 20);
	const auto before = victim->getAvailableHealth();
	battleCreatureBurn::onActivation(*gameHandler, *battle(), *victim, BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(victim->getAvailableHealth(), before - 20);
}
TEST_F(NewHorizonsCreatureBurnTest, LethalTickCannotReturnWhenTheSameBodyIsResurrected)
{
	setup(); add(pending(static_cast<int>(victim->getAvailableHealth())));
	ASSERT_TRUE(battleCreatureBurn::onActivation(*gameHandler, *battle(), *victim, BattleUnitTurnReason::TURN_QUEUE));
	ASSERT_FALSE(victim->alive()); EXPECT_FALSE(hasBurn(*victim));
	int64_t restore = victim->getMaxHealth();
	victim->heal(restore, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	ASSERT_TRUE(victim->alive());
	const auto before = victim->getAvailableHealth();
	EXPECT_FALSE(battleCreatureBurn::onActivation(*gameHandler, *battle(), *victim, BattleUnitTurnReason::TURN_QUEUE));
	EXPECT_EQ(victim->getAvailableHealth(), before);
}

TEST_F(NewHorizonsCreatureBurnTest, UnsupportedContextRejectsBurnBeforeConsumptionAndLeavesNoBurnLegacyPathExact)
{
	legacyMagic = true; ASSERT_NO_FATAL_FAILURE(setup());
	ASSERT_FALSE(battle()->battleUsesNewHorizonsMultiplicativeMDR());
	EXPECT_EQ(fire::adjustDamage(*battle(), *victim, 100), 100);
	EXPECT_TRUE(burn::activationTick(*battle(), *victim, BattleUnitTurnReason::TURN_QUEUE).remove.empty());
	EXPECT_THROW(fire::adjustFireDamage(*battle(), *victim, 100), std::runtime_error);
	add(pending()); const auto before = victim->getAvailableHealth();
	EXPECT_THROW(battleCreatureBurn::onActivation(*gameHandler, *battle(), *victim,
		BattleUnitTurnReason::TURN_QUEUE), std::runtime_error);
	EXPECT_TRUE(hasBurn(*victim)); EXPECT_EQ(victim->getAvailableHealth(), before);
}

TEST(NewHorizonsCreatureBurnBodyLossTest, TemporaryAbsorptionCannotBecomeBurnBudget)
{
	EXPECT_EQ(burn::bodyHealthLoss(0, 50), 0);
	EXPECT_EQ(burn::bodyHealthLoss(30, 50), 0);
	EXPECT_EQ(burn::bodyHealthLoss(75, 50), 25);
	EXPECT_EQ(burn::damageBudget(burn::bodyHealthLoss(30, 50), 20), 0);
	EXPECT_EQ(burn::damageBudget(burn::bodyHealthLoss(75, 50), 20), 5);
	EXPECT_THROW(burn::bodyHealthLoss(1, -1), std::invalid_argument);
}
TEST_F(NewHorizonsCreatureBurnTest, ActualDamageStateSeparatesGuardianTemporaryAndBodyLoss)
{
	setup();
	auto state = victim->acquireState();
	state->guardianSpiritHitPoints = 40; state->guardianSpiritRoundsRemaining = 3;
	state->health.addTemporaryHitPoints(30);
	const auto temporaryBefore = state->health.getTemporaryHitPoints();
	const auto bodyBefore = state->health.getCreatureHealthAvailable();
	int64_t damage = 90;
	state->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(damage, 50);
	EXPECT_EQ(burn::bodyHealthLoss(damage, temporaryBefore), 20);
	EXPECT_EQ(bodyBefore - state->health.getCreatureHealthAvailable(), 20);
	EXPECT_EQ(burn::damageBudget(burn::bodyHealthLoss(damage, temporaryBefore), 20), 4);
}

TEST_F(NewHorizonsCreatureBurnTest, SnapshotClassifierAcceptsOnlyExactReservedBurnShape)
{
	setup();
	auto valid = pending();
	EXPECT_TRUE(recipientBonusOverlay::isSnapshotHeroSpecialEffect(&valid));
	EXPECT_FALSE(recipientBonusOverlay::isSnapshotHeroSpecialEffect(nullptr));
	Bonus unrelated(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::HERO_SPECIAL,
		20, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:rashka"))));
	unrelated.statusIdentity = "unrelatedOneBattleStatus";
	unrelated.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	EXPECT_FALSE(recipientBonusOverlay::isSnapshotHeroSpecialEffect(&unrelated));
	for(int corruption = 0; corruption < 6; ++corruption)
	{
		auto claimed = valid;
		if(corruption == 0) claimed.source = BonusSource::OTHER;
		if(corruption == 1) claimed.stacking.clear();
		if(corruption == 2) claimed.statusIdentity.clear();
		if(corruption == 3) claimed.val = 0;
		if(corruption == 4) claimed.parameters = std::make_shared<BonusParameters>(-1);
		if(corruption == 5) claimed.statusTags.clear();
		EXPECT_THROW(recipientBonusOverlay::isSnapshotHeroSpecialEffect(&claimed), std::runtime_error);
	}
}
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsCreatureBurnTest, MaterializedChildKeepsBurnWhenParentConsumesBeforeFirstChildQuery)
{
	setup(); add(pending()); BurnEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto parentTarget = parent->getForUpdate(victim->unitId());
	ASSERT_TRUE(hasBurn(*parentTarget));
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	// Materialize the units while the parent owns the burn, then delay all
	// child/sibling bonus and tick queries until after parent consumption.
	auto childTarget = child->getForUpdate(victim->unitId());
	auto siblingTarget = sibling->getForUpdate(victim->unitId());
	const auto liveHealth = victim->getAvailableHealth();
	const auto parentTick = burn::activationTick(*parent, *parentTarget, BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_EQ(parentTick.damage, 20);
	parentTarget->removeUnitBonus(parentTick.remove);
	ASSERT_FALSE(hasBurn(*parentTarget));
	const auto childTick = burn::activationTick(*child, *childTarget, BattleUnitTurnReason::TURN_QUEUE);
	const auto siblingTick = burn::activationTick(*sibling, *siblingTarget, BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_EQ(childTick.remove.size(), 1u); ASSERT_EQ(siblingTick.remove.size(), 1u);
	EXPECT_EQ(childTick.damage, 20); EXPECT_EQ(siblingTick.damage, 20);
	EXPECT_EQ(childTick.source, actor->unitId()); EXPECT_EQ(siblingTick.source, actor->unitId());
	childTarget->removeUnitBonus(childTick.remove);
	EXPECT_FALSE(hasBurn(*childTarget)); EXPECT_FALSE(hasBurn(*parentTarget));
	EXPECT_TRUE(hasBurn(*siblingTarget)); EXPECT_TRUE(hasBurn(*victim));
	EXPECT_EQ(victim->getAvailableHealth(), liveHealth);
	EXPECT_EQ(childTarget->getAvailableHealth(), liveHealth);
	EXPECT_EQ(siblingTarget->getAvailableHealth(), liveHealth);
}
TEST_F(NewHorizonsCreatureBurnTest, MaterializedBranchesKeepIndependentBurnAfterLiveSourceRemovesIt)
{
	setup(); add(pending()); BurnEnvironment env(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto parentTarget = parent->getForUpdate(victim->unitId());
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	auto childTarget = child->getForUpdate(victim->unitId());
	auto siblingTarget = sibling->getForUpdate(victim->unitId());
	// A materialized native-backed unit also freezes recognized statuses.
	victim->removeBonuses(CSelector(burn::isBurn));
	ASSERT_FALSE(hasBurn(*victim));
	const auto childTick = burn::activationTick(*child, *childTarget, BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_EQ(childTick.remove.size(), 1u); EXPECT_EQ(childTick.damage, 20);
	EXPECT_TRUE(hasBurn(*parentTarget)); EXPECT_TRUE(hasBurn(*siblingTarget));
	parentTarget->removeUnitBonus(CSelector(burn::isBurn));
	EXPECT_FALSE(hasBurn(*parentTarget)); EXPECT_TRUE(hasBurn(*childTarget));
	EXPECT_TRUE(hasBurn(*siblingTarget)); EXPECT_FALSE(hasBurn(*victim));
	childTarget->removeUnitBonus(childTick.remove);
	EXPECT_FALSE(hasBurn(*childTarget)); EXPECT_TRUE(hasBurn(*siblingTarget));
}
#endif
