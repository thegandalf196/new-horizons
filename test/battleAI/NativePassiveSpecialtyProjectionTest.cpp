/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../lib/battle/NewHorizonsPassiveSpecialties.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/Limiters.h"
using namespace heroQueueTest;
class NativePassiveSpecialtyProjectionTest : public HeroQueueSelectorTest
{
protected:
	std::string attackerHeroIdentity = "core:orrin";
	NativePassiveSpecialtyProjectionTest() { useRevisedOrders = true; }
	void mapLoaded(CMap * map) override
	{
		HeroQueueSelectorTest::mapLoaded(map);
		auto * hero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(hero, nullptr);
		hero->setHeroType(HeroTypeID(HeroTypeID::decode(attackerHeroIdentity)));
	}
	void prepareWorld(const char * hero)
	{
		attackerHeroIdentity = hero;
		ASSERT_NO_FATAL_FAILURE(world());
		ASSERT_EQ(attackerSideHero->getHeroTypeID(), HeroTypeID(HeroTypeID::decode(hero)));
	}
	void prepare(const char * hero)
	{
		ASSERT_NO_FATAL_FAILURE(prepareWorld(hero));
		ASSERT_NO_FATAL_FAILURE(arena());
	}
	void branches(PlayerColor observer = PlayerColor(0))
	{
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), observer);
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
};
TEST_F(NativePassiveSpecialtyProjectionTest, UnknownBoneFormationUsesProjectedCountAndResurrectionWithWarmQueries)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:galthran"));
	const auto * skeleton = addStack(BattleSide::ATTACKER, creatureByName("core:skeletonWarrior"), BattleHex(6, 3), 350);
	ASSERT_NE(skeleton, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches(PlayerColor(1)));
	const auto outside = observation();
	const auto unit = child->getForUpdate(skeleton->unitId());
	EXPECT_EQ(child->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_EQ(unit->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 21);
	DamageCache cache;
	const auto oldDamage = cache.getDamage(child->battleGetUnitByID(enemy->unitId()), unit.get(), child);
	int64_t loss = 250 * unit->getMaxHealth();
	unit->damage(loss);
	EXPECT_EQ(unit->getCount(), 100);
	EXPECT_EQ(unit->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 10);
	const auto changed = cache.getDamage(child->battleGetUnitByID(enemy->unitId()), unit.get(), child);
	DamageCache fresh;
	EXPECT_EQ(changed, fresh.getDamage(child->battleGetUnitByID(enemy->unitId()), unit.get(), child));
	EXPECT_GT(changed, oldDamage);
	int64_t healing = loss;
	unit->heal(healing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	EXPECT_EQ(unit->getCount(), 350);
	EXPECT_EQ(unit->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 21);
	EXPECT_EQ(parent->getForUpdate(skeleton->unitId())->getCount(), 350);
	EXPECT_EQ(sibling->getForUpdate(skeleton->unitId())->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 21);
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativePassiveSpecialtyProjectionTest, UnknownChillingPresenceFollowsCurrentRosterAndOriginalArmy)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:charna"));
	const auto * wight = addStack(BattleSide::ATTACKER, creatureByName("core:wight"), BattleHex(7, 4), 100);
	const auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 4), 100);
	const auto * secondWight = addStack(BattleSide::ATTACKER, creatureByName("core:wight"), BattleHex(12, 3), 100);
	ASSERT_NE(wight, nullptr); ASSERT_NE(victim, nullptr); ASSERT_NE(secondWight, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches(PlayerColor(1)));
	const auto outside = observation();
	const auto recipient = child->getForUpdate(victim->unitId());
	const auto morale = recipient->valOfBonuses(BonusType::MORALE);
	const auto luck = recipient->valOfBonuses(BonusType::LUCK);
	const auto version = child->getTreeVersion();
	child->moveUnit(wight->unitId(), wight->getPosition());
	EXPECT_EQ(child->getTreeVersion(), version);
	child->moveUnit(wight->unitId(), BattleHex(4, 2));
	EXPECT_GT(child->getTreeVersion(), version);
	EXPECT_EQ(recipient->valOfBonuses(BonusType::MORALE), morale + 1);
	EXPECT_EQ(recipient->valOfBonuses(BonusType::LUCK), luck + 1);
	child->addUnitBonus(wight->unitId(), {Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED,
		BonusSource::OTHER, 1, BonusSourceID())});
	child->moveUnit(wight->unitId(), wight->getPosition());
	EXPECT_EQ(recipient->valOfBonuses(BonusType::MORALE), morale);
	EXPECT_EQ(recipient->valOfBonuses(BonusType::LUCK), luck);
	child->moveUnit(secondWight->unitId(), BattleHex(9, 4));
	EXPECT_EQ(recipient->valOfBonuses(BonusType::MORALE), morale); // Two sources do not stack.
	const auto second = child->getForUpdate(secondWight->unitId());
	int64_t lethal = second->getAvailableHealth(); second->damage(lethal);
	EXPECT_EQ(recipient->valOfBonuses(BonusType::MORALE), morale);
	const auto first = child->getForUpdate(wight->unitId());
	lethal = first->getAvailableHealth(); first->damage(lethal);
	EXPECT_EQ(recipient->valOfBonuses(BonusType::MORALE), morale + 1);
	EXPECT_EQ(parent->getForUpdate(victim->unitId())->valOfBonuses(BonusType::MORALE), morale);
	EXPECT_EQ(sibling->getForUpdate(victim->unitId())->valOfBonuses(BonusType::LUCK), luck);
	EXPECT_EQ(child->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativePassiveSpecialtyProjectionTest, TreantmootPrunesDeliberateTargetsButKeepsCollateralDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:ryland"));
	const auto * tree = addStack(BattleSide::ATTACKER, creatureByName("core:dendroidSoldier"), BattleHex(6, 3), 100);
	const auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 3), 100);
	const auto * attacker = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 3), 100);
	ASSERT_NE(tree, nullptr); ASSERT_NE(ward, nullptr); ASSERT_NE(attacker, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	DamageCache cache;
	PotentialTargets targets(child->battleGetUnitByID(attacker->unitId()), cache, child);
	EXPECT_FALSE(std::ranges::any_of(targets.possibleAttacks, [&](const auto & candidate)
		{ return candidate.attack.defender->unitId() == ward->unitId(); }));
	EXPECT_GT(child->calculateDmgRange(BattleAttackInfo(attacker, ward, 0, false)).damage.min, 0);
	child->moveUnit(tree->unitId(), BattleHex(3, 2));
	EXPECT_TRUE(child->battleCanAttackUnitAction(child->battleGetUnitByID(attacker->unitId()), child->battleGetUnitByID(ward->unitId())));
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativePassiveSpecialtyProjectionTest, UnknownTreantmootTracksMovementDeathAndResurrectionWithoutChangingSiblings)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:ryland"));
	const auto * tree = addStack(BattleSide::ATTACKER, creatureByName("core:dendroidSoldier"), BattleHex(6, 3), 100);
	const auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(7, 3), 100);
	const auto * attacker = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 3), 100);
	ASSERT_NE(tree, nullptr); ASSERT_NE(ward, nullptr); ASSERT_NE(attacker, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches(PlayerColor(1)));
	const auto outside = observation();
	const auto protectedIn = [&](const std::shared_ptr<HypotheticBattle> & board)
	{
		return !board->battleCanAttackUnitAction(board->battleGetUnitByID(attacker->unitId()),
			board->battleGetUnitByID(ward->unitId()));
	};
	ASSERT_EQ(child->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	EXPECT_TRUE(protectedIn(child));
	EXPECT_TRUE(protectedIn(parent));
	child->moveUnit(tree->unitId(), BattleHex(3, 2));
	EXPECT_FALSE(protectedIn(child));
	EXPECT_TRUE(protectedIn(parent));
	EXPECT_TRUE(protectedIn(sibling));
	child->moveUnit(tree->unitId(), tree->getPosition());
	EXPECT_TRUE(protectedIn(child));
	const auto projectedTree = child->getForUpdate(tree->unitId());
	int64_t lethal = projectedTree->getAvailableHealth();
	projectedTree->damage(lethal);
	EXPECT_FALSE(protectedIn(child));
	int64_t healing = lethal;
	projectedTree->heal(healing, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	EXPECT_TRUE(protectedIn(child));
	EXPECT_TRUE(protectedIn(sibling));
	EXPECT_FALSE(child->battleGetUnitByID(tree->unitId())->hasBonusOfType(BonusType::CREATURE_ATTACK_TARGET_PROTECTION));
	EXPECT_GT(child->calculateDmgRange(BattleAttackInfo(attacker, ward, 0, false)).damage.min, 0);
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativePassiveSpecialtyProjectionTest, MalformedTreantmootCannotBroadenOpaqueSpatialAuthority)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:ryland"));
	const auto * tree = addStack(BattleSide::ATTACKER, creatureByName("core:dendroidGuard"), BattleHex(6, 3), 100);
	ASSERT_NE(tree, nullptr);
	std::shared_ptr<Bonus> malformed;
	for(const auto & bonus : attackerSideHero->getExportedBonusList())
		if(bonus->type == BonusType::CREATURE_ATTACK_TARGET_PROTECTION)
			malformed = std::make_shared<Bonus>(*bonus);
	ASSERT_NE(malformed, nullptr);
	const auto & original = static_cast<const AllOfLimiter &>(*malformed->limiter);
	auto changed = std::make_shared<AllOfLimiter>();
	changed->add(original.limiters.at(0));
	auto spatial = std::make_shared<UnitAdjacentLimiter>(static_cast<const UnitAdjacentLimiter &>(*original.limiters.at(1)));
	spatial->filter.anyCreature = true;
	changed->add(spatial);
	malformed->limiter = changed;
	attackerSideHero->addNewBonus(malformed);
	EXPECT_THROW(branches(PlayerColor(1)), std::invalid_argument);
}
TEST_F(NativePassiveSpecialtyProjectionTest, AerisMagicImmunityDoesNotSuppressPhysicalHeroPoisonTick)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:aeris"));
	const auto * pegasus = addStack(BattleSide::ATTACKER, creatureByName("core:silverPegasus"), BattleHex(6, 3), 100);
	ASSERT_NE(pegasus, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto recipient = child->getForUpdate(pegasus->unitId());
	ASSERT_TRUE(newHorizonsPassiveSpecialties::magicalDamageImmune(*recipient));
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(recipient.get(), 20, -1));
	recipient->physicalPoisonSpellSide = BattleSide::DEFENDER;
	recipient->physicalPoisonActivationsRemaining = 1;
	const auto before = recipient->getAvailableHealth();
	const auto raw = newHorizonsBulwark::physicalPoisonTickDamage(recipient.get());
	const auto expected = newHorizonsMetamagic::spellDamage(raw, nullptr, *child, recipient.get(), PlayerColor(1), false);
	ASSERT_GT(expected, 0);
	child->nextTurn(recipient->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(recipient->getAvailableHealth(), before - expected);
	EXPECT_EQ(recipient->physicalPoisonSpellOrigin(), BattleSide::NONE);
	EXPECT_EQ(parent->getForUpdate(pegasus->unitId())->getAvailableHealth(), before);
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativePassiveSpecialtyProjectionTest, JabarkasUsesCurrentCasualtiesAndNeverCreatesAShootingCandidate)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:jabarkas"));
	const auto * orc = addStack(BattleSide::ATTACKER, creatureByName("core:orcChieftain"), BattleHex(6, 3), 100);
	ASSERT_NE(orc, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto source = child->getForUpdate(orc->unitId());
	const auto * target = child->battleGetUnitByID(enemy->unitId());
	ASSERT_FALSE(source->isShooter());
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(BattleAttackInfo(source.get(), target, 0, false)), 0);
	DamageCache cache;
	cache.getDamage(source.get(), target, child);
	int64_t loss = 50 * source->getMaxHealth();
	source->damage(loss);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(BattleAttackInfo(source.get(), target, 0, false)), 50);
	DamageCache fresh;
	EXPECT_EQ(cache.getDamage(source.get(), target, child), fresh.getDamage(source.get(), target, child));
	int64_t restore = loss;
	source->heal(restore, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	EXPECT_EQ(source->getCount(), 100);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(BattleAttackInfo(source.get(), target, 0, false)), 0);
	PotentialTargets targets(source.get(), cache, child);
	EXPECT_FALSE(std::ranges::any_of(targets.possibleAttacks, [](const auto & candidate) { return candidate.attack.shooting; }));
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativePassiveSpecialtyProjectionTest, VokialTargetCasualtiesInvalidatePrimedPairWithoutBorrowingWounds)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:vokial"));
	const auto * vampire = addStack(BattleSide::ATTACKER, creatureByName("core:vampireLord"), BattleHex(7, 3), 100);
	ASSERT_NE(vampire, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	const auto source = child->getForUpdate(vampire->unitId());
	const auto target = child->getForUpdate(enemy->unitId());
	BattleAttackInfo hit(source.get(), target.get(), 0, false);
	DamageCache cache;
	cache.getDamage(source.get(), target.get(), child);
	int64_t loss = 1;
	target->damage(loss);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(hit), 0);
	loss = 10 * target->getMaxHealth() - 1;
	target->damage(loss);
	EXPECT_EQ(newHorizonsPassiveSpecialties::casualtyDamagePercent(hit), 10);
	DamageCache fresh;
	EXPECT_EQ(cache.getDamage(source.get(), target.get(), child), fresh.getDamage(source.get(), target.get(), child));
	EXPECT_EQ(parent->getForUpdate(enemy->unitId())->getCount(), 100);
	EXPECT_EQ(sibling->getForUpdate(enemy->unitId())->getCount(), 100);
	EXPECT_EQ(observation(), outside);
}
TEST_F(NativePassiveSpecialtyProjectionTest, StoneboundPoisonTicksUseFinalPowerAndConsumeZeroDamageTicksForBothOrigins)
{
	ASSERT_NO_FATAL_FAILURE(prepareWorld("core:brissa"));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 48, ChangeValueMode::ABSOLUTE);
	const std::string skill = "new-horizons:elementalRebirth";
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".elementalAttunement", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".adaptiveElement", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(selectPerk(attackerSideHero, skill, skill + ".perfectConvergence", MasteryLevel::EXPERT));
	ASSERT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 60);
	ASSERT_NO_FATAL_FAILURE(arena());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	for(const auto origin : {BattleSide::NONE, BattleSide::DEFENDER})
	{
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), parent);
		const auto target = branch->getForUpdate(actor->unitId());
		ASSERT_EQ(target->elementalAttunementPassives().physicalReductionBasisPoints, 1500);
		ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(target.get(), 1, enemy->unitId()));
		target->physicalPoisonSpellSide = origin;
		const auto before = target->getAvailableHealth();
		for(int activation = 0; activation < 3; ++activation)
		{
			branch->nextTurn(target->unitId(), BattleUnitTurnReason::TURN_QUEUE);
			EXPECT_EQ(target->physicalPoisonActivationsRemaining, activation < 2 ? 2 - activation : 0);
			EXPECT_EQ(target->getAvailableHealth(), before - (activation == 2 ? 1 : 0));
		}
		EXPECT_EQ(target->physicalPoisonSpellOrigin(), BattleSide::NONE);
		EXPECT_EQ(parent->getForUpdate(actor->unitId())->getAvailableHealth(), before);
	}
	EXPECT_EQ(observation(), outside);
}

TEST_F(NativePassiveSpecialtyProjectionTest, ReboundUnknownSourceRefusesBeforeExecutingNativeGraphUpdater)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:galthran"));
	auto * skeleton = addStack(BattleSide::ATTACKER, creatureByName("core:skeletonWarrior"), BattleHex(6, 3), 350);
	ASSERT_NE(skeleton, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches(PlayerColor(1)));
	const auto unit = child->getForUpdate(skeleton->unitId());
	ASSERT_EQ(unit->valOfBonuses(BonusType::PHYSICAL_DEFENSE), 21); // Warm the real capsule path.
	// Capture the concrete carrier before installing the unknown updater.
	const auto rebound = std::make_shared<HypotheticBattle>(environment.get(),
		std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1)));
	class CountingUpdater final : public IUpdater
	{
		std::shared_ptr<int> calls;
	public:
		explicit CountingUpdater(std::shared_ptr<int> value) : calls(std::move(value)) {}
		std::shared_ptr<Bonus> createUpdatedBonus(const std::shared_ptr<Bonus> & bonus,
			const BonusEvaluationContext &) const override
		{
			++*calls;
			return std::make_shared<Bonus>(*bonus);
		}
	};
	const auto calls = std::make_shared<int>(0);
	auto sentinel = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	sentinel->updater = std::make_shared<CountingUpdater>(calls);
	skeleton->addNewBonus(sentinel);
	static_cast<BattleProxy &>(*parent) = static_cast<const BattleProxy &>(*rebound);
	EXPECT_THROW(unit->valOfBonuses(BonusType::PHYSICAL_DEFENSE), std::invalid_argument);
	EXPECT_EQ(*calls, 0);
}
