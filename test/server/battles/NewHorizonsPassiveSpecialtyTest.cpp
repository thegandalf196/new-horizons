/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/battle/NewHorizonsPassiveSpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../../lib/entities/hero/NewHorizonsPassiveSpecialtyRules.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../client/windows/CreatureDefensePresentation.h"
#include <array>
#include <limits>
namespace
{
class PassiveSpecialtyTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	void prepare(const char * hero, const char * family, int count = 100)
	{
		useRevisedOrders = true;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode(family)), count}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 100}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName(family), BattleHex(3, 5), count);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(4, 5), 100);
	}
	void publish(CStack & unit, battle::CUnitState & state, int64_t previousHealth)
	{
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(unit.unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state.save();
		change.changedStacks.back().healthDelta = state.getAvailableHealth() - previousHealth;
		gameHandler->sendAndApply(change);
	}
	void injure(CStack & unit, int64_t amount)
	{
		const auto previous = unit.getAvailableHealth();
		auto state = unit.acquireState();
		state->damage(amount);
		publish(unit, *state, previous);
	}
	void restore(CStack & unit, int64_t amount)
	{
		const auto previous = unit.getAvailableHealth();
		auto state = unit.acquireState();
		state->heal(amount, EHealLevel::RESURRECT, EHealPower::PERMANENT);
		publish(unit, *state, previous);
	}
	void move(CStack & unit, BattleHex destination)
	{
		const auto previous = unit.getAvailableHealth();
		auto state = unit.acquireState();
		state->setPosition(destination);
		publish(unit, *state, previous);
	}
	void applySlow(CStack & unit)
	{
		const auto * slow = SpellID(SpellID::SLOW).toSpell();
		spells::BattleCast cast(battle(), defenderSideHero, spells::Mode::PASSIVE, slow);
		cast.setSpellLevel(0);
		cast.setEffectDuration(3);
		spells::Target aim;
		aim.emplace_back(&unit);
		cast.applyEffects(gameHandler->spellcastEnvironment(), aim);
		EXPECT_FALSE(unit.getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::SLOW))))->empty());
	}
	static Bonus hypnosis()
	{
		return {BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()};
	}
};
TEST(PassiveSpecialtyScalars, CurrentCasualtiesAndCompleteCountBandsHaveExactBoundaries)
{
	using namespace newHorizonsPassiveSpecialties;
	EXPECT_EQ(casualtyPercent(0, 0), 0);
	EXPECT_EQ(casualtyPercent(100, 100), 0);
	EXPECT_EQ(casualtyPercent(100, 99), 1);
	EXPECT_EQ(casualtyPercent(3, 2), 33);
	EXPECT_EQ(casualtyPercent(100, 150), 0);
	EXPECT_EQ(casualtyPercent(100, -1), 100);
	EXPECT_EQ(casualtyPercent(std::numeric_limits<int32_t>::max(), 1), 99);
	for(const auto & row : {std::pair{0, 0}, {9, 0}, {10, 1}, {59, 5}, {60, 6}, {99, 9}, {100, 10},
		{119, 10}, {120, 11}, {299, 19}, {300, 20}, {349, 20}, {350, 21}})
		EXPECT_EQ(boneFormationDefense(row.first), row.second);
}
TEST(PassiveSpecialtyScalars, NewRecipesRejectPartialGroupsRetiredKeysAndBadCurrentValues)
{
	const JsonNode installed(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto & policy = installed["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	ASSERT_NO_THROW(newHorizonsHeroes::validatePassiveSpecialtyPolicy(policy));
	for(const char * key : {"chillingPresenceMoraleReduction", "chillingPresenceLuckReduction",
		"boneFormationFirstBandSize", "boneFormationFirstBandLimit", "boneFormationSecondBandSize",
		"boneFormationSecondBandLimit", "boneFormationRemainingBandSize", "furyOfTheHordeAttack",
		"furyOfTheHordeDefense", "furyOfTheHordeDamage", "orcRageMaximumDamagePercent",
		"dispellingShotMageChancePercent", "dispellingShotArchMageChancePercent"})
	{
		auto bad = policy;
		bad.Struct().erase(key);
		EXPECT_THROW(newHorizonsHeroes::validatePassiveSpecialtyPolicy(bad), std::runtime_error) << key;
	}
	for(const char * key : {"bloodScentMaximumDamagePercent", "treantmootProtection",
		"perfectMountMagicalDamageImmunity", "wyvernVenomPolicyVersion", "cyclopeanStunChancePercent"})
	{
		auto bad = policy;
		bad[key].Integer() = 0;
		EXPECT_THROW(newHorizonsHeroes::validatePassiveSpecialtyPolicy(bad), std::runtime_error) << key;
	}
	for(const char * key : {"bloodScentDamagePercent", "livingBastionDefense", "boneFormationDefense",
		"wingbeatInitiativePercent", "cruelVolleyDamagePercent", "wyvernsPreyDamagePercent",
		"measuredVolleyMdrPenetrationPercent", "cyclopeanBreachIgnorePercent"})
	{
		auto bad = installed;
		bad["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = 1;
		EXPECT_THROW(newHorizonsHeroes::validateOrdinaryCreatureSpecialtiesSerialization(bad, true), std::runtime_error) << key;
	}
	EXPECT_THROW(newHorizonsHeroes::validatePassiveSpecialtySerialization(installed, false), std::runtime_error);
}
TEST(PassiveSpecialtyScalars, NativeBonusTypesAndNestedOwnersRejectOldCodecsBeforePrefixes)
{
	for(const auto type : {BonusType::MAGICAL_DAMAGE_IMMUNITY, BonusType::CANNOT_SHOOT,
		BonusType::CREATURE_ATTACK_TARGET_PROTECTION, BonusType::CASUALTY_DAMAGE_PERCENTAGE,
		BonusType::DYNAMIC_CREATURE_DAMAGE, BonusType::PHYSICAL_DEFENSE})
	{
		auto child = std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, 1, BonusSourceID());
		CMemorySerializer current;
		ASSERT_NO_THROW(child->serialize(current.oser));
		Bonus restored;
		ASSERT_NO_THROW(restored.serialize(current.iser));
		EXPECT_EQ(restored.type, type);
		Bonus parent(BonusDuration::ONE_BATTLE, BonusType::SPELL_AFTER_ATTACK, BonusSource::OTHER, 1, BonusSourceID());
		BonusParametersOnCombatEvent effects;
		effects.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{child, true});
		parent.parameters = std::make_shared<BonusParameters>(effects);
		CMemorySerializer old;
		old.oser.version = static_cast<ESerializationVersion>(1156);
		EXPECT_THROW(parent.serialize(old.oser), std::runtime_error);
		EXPECT_TRUE(old.extractBuffer().empty());
	}
}
TEST_F(PassiveSpecialtyTest, BloodScentUsesLivingTargetNativeCasualtiesAndRestorationNotWounds)
{
	prepare("core:vokial", "core:vampireLord");
	BattleAttackInfo hit(actor, victim, 0, false);
	EXPECT_EQ(newHorizonsHeroes::ordinaryCreatureDamageMultiplier(*battle(), hit), 100);
	int64_t injury = 1;
	injure(*victim, injury);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(hit), 0);
	injury = static_cast<int64_t>(victim->getMaxHealth()) * 10 - 1;
	injure(*victim, injury);
	ASSERT_EQ(victim->getCount(), 90);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(hit), 10);
	hit.retaliation = true;
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(hit), 10);
	int64_t healing = static_cast<int64_t>(victim->getMaxHealth()) * 5;
	restore(*victim, healing);
	ASSERT_EQ(victim->getCurrentOriginalCreatureCount(), 95);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(hit), 5);
	for(int kind = 0; kind < 5; ++kind)
	{
		auto excluded = hit;
		if(kind == 0) excluded.secondaryAttack = true;
		if(kind == 1) excluded.cleaveDamagePercent = 50;
		if(kind == 2) excluded.preemptiveDamagePercent = 50;
		if(kind == 3) excluded.shooting = true;
		if(kind == 4) excluded.physicalDamage = false;
		EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(excluded), 0);
	}
	auto * undead = addStack(BattleSide::DEFENDER, creatureByName("core:skeleton"), BattleHex(8, 5), 100);
	injury = static_cast<int64_t>(undead->getMaxHealth()) * 10;
	injure(*undead, injury);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(BattleAttackInfo(actor, undead, 0, false)), 0);
}
TEST_F(PassiveSpecialtyTest, OrcConversionAndRageUseOwnCurrentCasualtiesIncludingRetaliation)
{
	prepare("core:jabarkas", "core:orcChieftain");
	EXPECT_FALSE(actor->isShooter());
	EXPECT_FALSE(actor->canShoot());
	EXPECT_EQ(actor->valOfBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL, PrimarySkill::ATTACK)
		.And(Selector::sourceTypeSel(BonusSource::HERO_SPECIAL))), 2);
	EXPECT_EQ(actor->valOfBonuses(Selector::typeSubtype(BonusType::PRIMARY_SKILL, PrimarySkill::DEFENSE)
		.And(Selector::sourceTypeSel(BonusSource::HERO_SPECIAL))), 2);
	EXPECT_EQ(actor->valOfBonuses(Selector::typeSubtype(BonusType::CREATURE_DAMAGE, BonusCustomSubtype::creatureDamageBoth)
		.And(Selector::sourceTypeSel(BonusSource::HERO_SPECIAL))), 1);
	EXPECT_FALSE(battle()->battleCanShootAction(actor, victim->getPosition()));
	BattleAttackInfo hit(actor, victim, 0, false);
	int64_t injury = actor->getMaxHealth() * 25;
	injure(*actor, injury);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(hit), 25);
	hit.retaliation = true;
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(hit), 25);
	actor->addNewBonus(std::make_shared<Bonus>(hypnosis()));
	EXPECT_FALSE(actor->isShooter());
	EXPECT_TRUE(newHorizonsPassiveSpecialties::hasDynamicDamagePassive(*actor));
	auto * imported = addStack(BattleSide::DEFENDER, creatureByName("core:orc"), BattleHex(12, 5), 10);
	EXPECT_TRUE(imported->isShooter());
	EXPECT_FALSE(imported->hasBonusOfType(BonusType::CASUALTY_DAMAGE_PERCENTAGE));
}
TEST_F(PassiveSpecialtyTest, BoneFormationReevaluatesCurrentCompleteBandsAndOnlyPhysicalDefense)
{
	prepare("core:galthran", "core:skeletonWarrior", 1000);
	EXPECT_TRUE(newHorizonsPassiveSpecialties::hasDynamicDamagePassive(*actor));
	for(const int count : {1000, 350, 349, 300, 299, 120, 119, 100, 99, 60, 59, 10, 9, 1})
	{
		EXPECT_EQ(actor->valOfBonuses(BonusType::PHYSICAL_DEFENSE),
			newHorizonsPassiveSpecialties::boneFormationDefense(actor->getCount()));
		int64_t injury = static_cast<int64_t>(actor->getCount() - count) * actor->getMaxHealth();
		injure(*actor, injury);
		ASSERT_EQ(actor->getCount(), count);
		EXPECT_EQ(actor->valOfBonuses(BonusType::PHYSICAL_DEFENSE), newHorizonsPassiveSpecialties::boneFormationDefense(count));
	}
	int64_t healing = static_cast<int64_t>(349) * actor->getMaxHealth();
	restore(*actor, healing);
	ASSERT_EQ(actor->getCount(), 350);
	EXPECT_EQ(actor->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 21);
	BattleAttackInfo physical(victim, actor, 0, false);
	auto magical = physical;
	magical.physicalDamage = false;
	const auto protectedPhysical = battle()->calculateDmgRange(physical);
	const auto protectedMagical = battle()->calculateDmgRange(magical);
	attackerSideHero->removeBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE));
	EXPECT_GT(battle()->calculateDmgRange(physical).damage.min, protectedPhysical.damage.min);
	EXPECT_EQ(battle()->calculateDmgRange(magical).damage.min, protectedMagical.damage.min);
}
TEST_F(PassiveSpecialtyTest, SixtySkeletonDefenseReadoutsFollowBothFormsCurrentCountAndArmy)
{
	prepare("core:galthran", "core:skeleton", 60);
	const auto upgraded = creatureByName("core:skeletonWarrior");
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), upgraded, 60));
	auto * warrior = addStack(BattleSide::ATTACKER, upgraded, BattleHex(3, 6), 60);
	for(const auto * unit : {actor, warrior})
	{
		const auto * creature = unit->creatureId().toCreature();
		const auto base = creatureDefensePresentation::query(*creature, false);
		const auto current = creatureDefensePresentation::query(*unit, false);
		EXPECT_EQ(base.general, unit == actor ? 4 : 6);
		EXPECT_EQ(base.physical, base.general);
		EXPECT_EQ(current.general, base.general);
		EXPECT_EQ(current.physical, unit == actor ? 10 : 12);
	}
	for(const auto slot : {SlotID(0), SlotID(1)})
	{
		const auto * stack = attackerSideHero->getStackPtr(slot);
		const auto view = creatureDefensePresentation::query(*stack, false);
		EXPECT_EQ(view.physical - view.general, 6);
	}

	// Warm readouts must follow count changes in their own context.
	injure(*actor, actor->getMaxHealth());
	EXPECT_EQ(actor->getCount(), 59);
	EXPECT_EQ(creatureDefensePresentation::query(*actor, false).physical, 9);
	auto * armyStack = attackerSideHero->getStackPtr(SlotID(0));
	EXPECT_EQ(creatureDefensePresentation::query(*armyStack, false).physical, 10);
	attackerSideHero->setStackCount(SlotID(0), 59);
	EXPECT_EQ(creatureDefensePresentation::query(*armyStack, false).physical, 9);
	attackerSideHero->setStackCount(SlotID(0), 60);
	EXPECT_EQ(creatureDefensePresentation::query(*armyStack, false).physical, 10);

	// The specialty belongs to the commanding hero, not the stored creature.
	defenderSideHero->putStack(SlotID(1), attackerSideHero->detachStack(SlotID(0)));
	EXPECT_EQ(creatureDefensePresentation::query(*armyStack, false).physical, 4);
	attackerSideHero->putStack(SlotID(0), defenderSideHero->detachStack(SlotID(1)));
	EXPECT_EQ(creatureDefensePresentation::query(*armyStack, false).physical, 10);
	const auto ordinary = creatureDefensePresentation::query(*victim, false);
	EXPECT_EQ(ordinary.physical, ordinary.general);
}
TEST_F(PassiveSpecialtyTest, SixtySkeletonPhysicalDefenseMatchesResolvedIncomingAttack)
{
	prepare("core:galthran", "core:skeleton", 60);
	forceMaximumDamage(victim);
	blockRetaliation(victim);
	beginCombat();
	ASSERT_EQ(victim->luckVal(), 0);
	ASSERT_EQ(actor->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 6);
	ASSERT_EQ(creatureDefensePresentation::query(*actor, false).physical, 10);
	const auto expected = battle()->calculateDmgRange(BattleAttackInfo(victim, actor, 0, false));
	ASSERT_EQ(expected.damage.min, expected.damage.max);
	const auto before = actor->getAvailableHealth();
	ASSERT_TRUE(attack(victim, actor->getPosition()));
	EXPECT_EQ(before - actor->getAvailableHealth(), expected.damage.min);
	EXPECT_EQ(actor->valOfBonuses(BonusType::PHYSICAL_DEFENSE),
		newHorizonsPassiveSpecialties::boneFormationDefense(actor->getCount()));
}
TEST_F(PassiveSpecialtyTest, TreantmootGatesDeliberateCreatureTargetsWithoutBlanketInvulnerability)
{
	prepare("core:ryland", "core:dendroidSoldier");
	move(*victim, BattleHex(5, 5));
	auto * protectedUnit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 10);
	ASSERT_TRUE(protectedUnit->hasBonusOfType(BonusType::CREATURE_ATTACK_TARGET_PROTECTION));
	EXPECT_FALSE(battle()->battleCanAttackUnitAction(victim, protectedUnit));
	EXPECT_TRUE(battle()->battleCanAttackUnitAction(victim, actor));
	EXPECT_TRUE(battle()->isMeleeAttackPossible(victim, protectedUnit)); // retaliation/collateral geometry remains valid
	EXPECT_GT(battle()->calculateDmgRange(BattleAttackInfo(victim, protectedUnit, 0, false)).damage.min, 0);
	applySlow(*protectedUnit);
	auto * shooter = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(12, 5), 10);
	EXPECT_FALSE(battle()->battleCanShootAction(shooter, protectedUnit->getPosition()));
	actor->addNewBonus(std::make_shared<Bonus>(hypnosis()));
	EXPECT_TRUE(protectedUnit->hasBonusOfType(BonusType::CREATURE_ATTACK_TARGET_PROTECTION));
	move(*actor, BattleHex(8, 7));
	EXPECT_FALSE(protectedUnit->hasBonusOfType(BonusType::CREATURE_ATTACK_TARGET_PROTECTION));
	EXPECT_TRUE(battle()->battleCanAttackUnitAction(victim, protectedUnit));
	move(*actor, BattleHex(3, 5));
	EXPECT_TRUE(protectedUnit->hasBonusOfType(BonusType::CREATURE_ATTACK_TARGET_PROTECTION));
	EXPECT_FALSE(battle()->battleCanAttackUnitAction(victim, protectedUnit));
}
TEST_F(PassiveSpecialtyTest, TreantmootRejectsAuthoritativePrimaryBeforeActivationOrRandomMutation)
{
	prepare("core:ryland", "core:dendroidSoldier");
	move(*victim, BattleHex(5, 5));
	auto * protectedUnit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 10);
	beginCombat();
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = victim->unitId();
	active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	const auto beforeActor = victim->acquireState()->save();
	const auto beforeTarget = protectedUnit->acquireState()->save();
	const auto beforeHealth = protectedUnit->getAvailableHealth();
	const auto rng = [&]
	{
		CMemorySerializer wire;
		wire.oser & *gameHandler->randomizer;
		return wire.extractBuffer();
	};
	const auto beforeRng = rng();
	const auto action = BattleAction::makeMeleeAttack(victim, protectedUnit, victim->getPosition());
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));
	EXPECT_EQ(victim->acquireState()->save(), beforeActor);
	EXPECT_EQ(protectedUnit->acquireState()->save(), beforeTarget);
	EXPECT_EQ(rng(), beforeRng);

	Bonus berserk(BonusDuration::N_TURNS, BonusType::ATTACKS_NEAREST_CREATURE, BonusSource::SPELL_EFFECT,
		1, BonusSourceID(SpellID(SpellID::BERSERK)));
	berserk.turnsRemain = 1;
	berserk.spellCasterOwner = PlayerColor(0);
	victim->addNewBonus(std::make_shared<Bonus>(berserk));
	const auto forced = battle()->getBerserkForcedActions(victim);
	ASSERT_FALSE(forced.empty());
	for(const auto & candidate : forced)
		EXPECT_NE(candidate.target, protectedUnit);
	victim->removeBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::BERSERK))));
	move(*actor, BattleHex(8, 7));
	EXPECT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));
	EXPECT_LT(protectedUnit->getAvailableHealth(), beforeHealth);
}
TEST_F(PassiveSpecialtyTest, ChillingPresenceHasOneMeleePenaltyAndUnrestrictedMoraleLuckWithOriginalAllegiance)
{
	prepare("core:charna", "core:wraith");
	move(*actor, BattleHex(9, 7));
	EXPECT_TRUE(newHorizonsPassiveSpecialties::hasDynamicDamagePassive(*victim));
	const int morale = victim->valOfBonuses(BonusType::MORALE);
	const int luck = victim->valOfBonuses(BonusType::LUCK);
	move(*actor, BattleHex(3, 5));
	auto * secondSource = addStack(BattleSide::ATTACKER, creatureByName("core:wight"), BattleHex(5, 5), 10);
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), morale - 1);
	EXPECT_EQ(victim->valOfBonuses(BonusType::LUCK), luck - 1);
	const auto debuffs = victim->getBonuses(Selector::source(BonusSource::HERO_SPECIAL, BonusSourceID(attackerSideHero->getHeroTypeID())));
	int melee = 0;
	for(const auto & bonus : *debuffs)
	{
		if(bonus->type == BonusType::GENERAL_ATTACK_REDUCTION)
		{
			EXPECT_EQ(bonus->val, 10);
			EXPECT_EQ(bonus->effectRange, BonusLimitEffect::ONLY_MELEE_FIGHT);
			++melee;
		}
		if(bonus->type == BonusType::MORALE || bonus->type == BonusType::LUCK)
			EXPECT_EQ(bonus->effectRange, BonusLimitEffect::NO_LIMIT);
	}
	EXPECT_EQ(melee, 1);
	move(*secondSource, BattleHex(11, 7));
	actor->addNewBonus(std::make_shared<Bonus>(hypnosis()));
	EXPECT_EQ(battle()->battleGetActionController(actor), PlayerColor(1));
	move(*actor, BattleHex(9, 7));
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), morale);
	EXPECT_EQ(victim->valOfBonuses(BonusType::LUCK), luck);
	move(*actor, BattleHex(3, 5));
	EXPECT_EQ(victim->valOfBonuses(BonusType::MORALE), morale - 1);
	EXPECT_EQ(victim->valOfBonuses(BonusType::LUCK), luck - 1);
}
TEST_F(PassiveSpecialtyTest, PerfectMountBlocksMagicalDamageAfterPenetrationAndSoulExecutionButKeepsOtherEffects)
{
	prepare("core:aeris", "core:silverPegasus", 10);
	ASSERT_TRUE(newHorizonsPassiveSpecialties::magicalDamageImmune(*actor));
	EXPECT_TRUE(newHorizonsPassiveSpecialties::hasDynamicDamagePassive(*actor));
	const auto * arrow = SpellID(SpellID::MAGIC_ARROW).toSpell();
	EXPECT_EQ(arrow->adjustRawDamage(nullptr, actor, 10000, 100, 0, 100, true, true, false, 0, {100}), 0);
	EXPECT_EQ(newHorizonsMetamagic::spellDamage(100, nullptr, *battle(), actor, PlayerColor(1)), 0);
	EXPECT_GT(newHorizonsMetamagic::spellDamage(100, nullptr, *battle(), actor, PlayerColor(1), false), 0);
	EXPECT_FALSE(actor->hasImmunity(SpellID(SpellID::SLOW)));
	applySlow(*actor);
	const auto physical = battle()->calculateDmgRange(BattleAttackInfo(victim, actor, 0, false));
	auto magic = BattleAttackInfo(victim, actor, 0, false);
	magic.physicalDamage = false;
	EXPECT_GT(physical.damage.min, 0);
	EXPECT_EQ(battle()->calculateDmgRange(magic).damage.max, 0);
	int64_t injury = actor->getAvailableHealth() - 1;
	injure(*actor, injury);
	const auto * reaper = SpellID(SpellID::decode("new-horizons:soulReaper")).toSpell();
	spells::BattleCast cast(battle(), defenderSideHero, spells::Mode::HERO, reaper);
	EXPECT_EQ(reaper->battleMechanics(&cast)->adjustEffectValue(actor), 0);
	const auto immunity = actor->getBonuses(Selector::type()(BonusType::MAGICAL_DAMAGE_IMMUNITY));
	ASSERT_EQ(immunity->size(), 1u);
	Bonus stored = *immunity->front();
	CMemorySerializer wire;
	stored.serialize(wire.oser);
	Bonus restored;
	restored.serialize(wire.iser);
	EXPECT_EQ(restored.type, BonusType::MAGICAL_DAMAGE_IMMUNITY);
	CMemorySerializer obsolete;
	obsolete.oser.version = static_cast<ESerializationVersion>(1156);
	EXPECT_THROW(stored.serialize(obsolete.oser), std::runtime_error);
	EXPECT_TRUE(obsolete.extractBuffer().empty());
}
}
