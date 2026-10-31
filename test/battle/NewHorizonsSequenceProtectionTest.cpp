/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <algorithm>
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsSequenceProtection.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/serializer/CMemorySerializer.h"
#if ENABLE_BATTLE_AI
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/AttackPreviewHitContext.h"
#include "../../lib/GameLibrary.h"
#include <vcmi/Environment.h>
#endif
namespace
{
class NewHorizonsSequenceProtectionTest : public HeroCommandFixture
{
protected:
	bool historical = false;
	bool zero = false;
	CStack * source = nullptr;
	CStack * target = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		for(const char * key : {"cyclopeanBreachIgnorePercent", "runicEnduranceReductionPercent"})
			if(historical) rules["additionalSpecialties"]["ordinaryCreatureSpecialties"].Struct().erase(key);
			else rules["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer() = zero ? 0 : 25;
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(bool yog, bool machine = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(yog ? "core:yog" : machine ? "core:torosar" : "core:orrin")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 10}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode(yog ? "core:orrin" : "core:ufretin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 10}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			// War machines alone cannot keep their side in combat.
			if(machine && unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
				continue;
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		}
		gameHandler->sendAndApply(remove);
		source = addStack(BattleSide::ATTACKER, creatureByName(yog ? "core:cyclopKing" : machine ? "core:ballista" : "core:hobgoblinWolfRider"), BattleHex(5, 5), 10);
		target = addStack(BattleSide::DEFENDER, creatureByName(yog ? "core:pikeman" : "core:battleDwarf"), BattleHex(yog || machine ? 10 : 6, 5), 1000);
		for(auto * unit : {source, target})
			for(const auto type : {BonusType::NO_LUCK, BonusType::NO_MORALE})
				unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, type, BonusSource::OTHER, 1, BonusSourceID()));
		forceMaximumDamage(source);
		blockRetaliation(source);
		for(const auto bound : {BonusValueType::INDEPENDENT_MIN, BonusValueType::INDEPENDENT_MAX})
			source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
				BonusSource::OTHER, 1000, BonusSourceID(), BonusSubtypeID(), bound));
		if(machine)
			ASSERT_FALSE(battle()->battleIsFinished());
	}
	void protectTarget()
	{
		target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, BonusSource::OTHER, 4000, BonusSourceID()));
	}
};
template<typename Random> auto randomBytes(Random & random)
{
	CMemorySerializer memory;
	memory.oser & random;
	return memory.extractBuffer();
}
#if ENABLE_BATTLE_AI
class SequenceEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SequenceEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
}
TEST_F(NewHorizonsSequenceProtectionTest, YogPenetratesProtectionWithoutChangingCreatureDefenseOrRange)
{
	prepare(true);
	BattleAttackInfo shot(source, target, 0, true);
	const auto plain = battle()->calculateDmgRange(shot);
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*battle(), shot), 25);
	protectTarget();
	const auto protectedShot = battle()->calculateDmgRange(shot);
	// 40% reduction loses one quarter of its effect: 30% remains.
	EXPECT_NEAR(protectedShot.damage.min, plain.damage.min * 0.7, 1.0);
	EXPECT_NEAR(protectedShot.damage.max, plain.damage.max * 0.7, 1.0);
	EXPECT_EQ(protectedShot.damageBeforeDefense.min, plain.damageBeforeDefense.min);
	const auto defense = target->getDefense(true);
	const auto range = source->getShootingRangeDistance();
	const auto distance = source->getRangedFullDamageDistance();
	for(int exclusion = 0; exclusion < 6; ++exclusion)
	{
		auto other = shot;
		if(exclusion == 0) other.shooting = false;
		if(exclusion == 1) other.physicalDamage = false;
		if(exclusion == 2) other.secondaryAttack = true;
		if(exclusion == 3) other.retaliation = true;
		if(exclusion == 4) other.bracePreemptive = true;
		if(exclusion == 5) other.cleaveDamagePercent = 50;
		EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*battle(), other), 0);
	}
	EXPECT_EQ(target->getDefense(true), defense);
	EXPECT_EQ(source->getShootingRangeDistance(), range);
	EXPECT_EQ(source->getRangedFullDamageDistance(), distance);
}
TEST_F(NewHorizonsSequenceProtectionTest, UfretinAdditionalBlowIsIndependentAndNewActionStartsUnprotected)
{
	prepare(false); protectTarget();
	BattleAttackInfo hit(source, target, 0, false);
	const auto first = battle()->calculateDmgRange(hit);
	hit.ordinaryAttackIndex = 1;
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), hit), 25);
	const auto second = battle()->calculateDmgRange(hit);
	EXPECT_NEAR(second.damage.min, first.damage.min * 0.75, 1.0);
	EXPECT_EQ(second.damageBeforeDefense.min, first.damageBeforeDefense.min);
	for(int index : {2, 5})
	{
		hit.ordinaryAttackIndex = index;
		EXPECT_EQ(battle()->calculateDmgRange(hit).damage.min, second.damage.min);
	}
	const BattleAttackInfo nextAction(source, target, 0, false);
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), nextAction), 0);
	EXPECT_EQ(battle()->calculateDmgRange(nextAction).damage.min, first.damage.min);
	EXPECT_EQ(hit.reverse().ordinaryAttackIndex, 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, UfretinExcludesReactionsAndFriendlyBlows)
{
	prepare(false);
	BattleAttackInfo hit(source, target, 0, false); hit.ordinaryAttackIndex = 1;
	for(int exclusion = 1; exclusion < 6; ++exclusion)
	{
		auto other = hit;
		if(exclusion == 1) other.retaliation = true;
		if(exclusion == 2) other.bracePreemptive = true;
		if(exclusion == 3) other.preemptiveDamagePercent = 50;
		if(exclusion == 4) other.cleaveDamagePercent = 50;
		if(exclusion == 5) other.physicalDamage = false;
		EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), other), 0);
	}
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), hit), 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, UfretinUsesStrikePositionForFirstAndRepeatedCollateral)
{
	prepare(false);
	BattleAttackInfo collateral(source, target, 0, false);
	collateral.secondaryAttack = true;
	const auto first = battle()->calculateDmgRange(collateral);
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), collateral), 0);
	collateral.ordinaryAttackIndex = 1;
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), collateral), 25);
	EXPECT_NEAR(battle()->calculateDmgRange(collateral).damage.min, first.damage.min * 0.75, 1.0);
	collateral.ordinaryAttackIndex = 0;
	EXPECT_EQ(battle()->calculateDmgRange(collateral).damage.min, first.damage.min);
}
TEST_F(NewHorizonsSequenceProtectionTest, YogSpecialtyFollowsEffectiveArmyHero)
{
	prepare(true);
	BattleAttackInfo shot(source, target, 0, true);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*battle(), shot), 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, UfretinProtectsOrdinaryPhysicalMultiShotButNotFirstShot)
{
	prepare(false);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(2, 2), 10);
	BattleAttackInfo shot(shooter, target, 0, true);
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), shot), 0);
	shot.ordinaryAttackIndex = 1;
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), shot), 25);
	auto * machine = addStack(BattleSide::ATTACKER, creatureByName("core:ballista"), BattleHex(2, 3), 1);
	shot.attacker = machine;
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), shot), 25);
	shot.ordinaryAttackIndex = 0;
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), shot), 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, UfretinUsesEffectiveDwarfFormAndReversion)
{
	prepare(false);
	BattleAttackInfo hit(source, target, 0, false); hit.ordinaryAttackIndex = 1;
	target->beginBattleForm(creatureByName("core:pikeman"), 2);
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), hit), 0);
	target->endBattleForm();
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), hit), 25);
	auto * borrowed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 6), 10);
	hit.defender = borrowed;
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), hit), 0);
	borrowed->beginBattleForm(creatureByName("core:dwarf"), 2);
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), hit), 25);
	borrowed->endBattleForm();
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), hit), 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, YogUsesEffectiveCyclopsFormAndReversion)
{
	prepare(true);
	BattleAttackInfo shot(source, target, 0, true);
	source->beginBattleForm(creatureByName("core:marksman"), 2);
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*battle(), shot), 0);
	source->endBattleForm();
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*battle(), shot), 25);
}
TEST_F(NewHorizonsSequenceProtectionTest, HistoricalAbsenceRetainsOldUfretinIdentity)
{
	historical = true; prepare(false);
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(defenderSideHero));
	BattleAttackInfo hit(source, target, 0, false); hit.ordinaryAttackIndex = 1;
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), hit), 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, HistoricalAbsenceRetainsOldYogIdentity)
{
	historical = true; prepare(true);
	EXPECT_FALSE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*battle(), BattleAttackInfo(source, target, 0, true)), 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, ExplicitZeroRetainsYogReplacementWithoutPenetration)
{
	zero = true; prepare(true);
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*battle(), BattleAttackInfo(source, target, 0, true)), 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, ExplicitZeroRetainsUfretinReplacementWithoutProtection)
{
	zero = true; prepare(false);
	EXPECT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(defenderSideHero));
	BattleAttackInfo hit(source, target, 0, false); hit.ordinaryAttackIndex = 1;
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), hit), 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, AuthoritativeWolfRaiderSequenceProtectsOnlySecondPrimary)
{
	prepare(false); beginCombat();
	ASSERT_EQ(battle()->battleActiveUnit(), source);
	BattleAttackInfo hit(source, target, 0, false);
	const auto first = battle()->calculateDmgRange(hit).damage;
	hit.ordinaryAttackIndex = 1;
	const auto second = battle()->calculateDmgRange(hit).damage;
	ASSERT_EQ(first.min, first.max); ASSERT_EQ(second.min, second.max);
	const auto health = target->getAvailableHealth();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(source, target, source->getPosition())));
	ASSERT_EQ(server.attacks.size(), 2u);
	ASSERT_EQ(server.attacks[0].bsa.size(), 1u); ASSERT_EQ(server.attacks[1].bsa.size(), 1u);
	EXPECT_EQ(server.attacks[0].bsa[0].damageAmount, first.min);
	EXPECT_EQ(server.attacks[1].bsa[0].damageAmount, second.min);
	EXPECT_EQ(health - target->getAvailableHealth(), first.min + second.min);
}
TEST_F(NewHorizonsSequenceProtectionTest, AuthoritativeRepeatedCollateralUsesTheSharedBlowIndex)
{
	prepare(false);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ATTACKS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(5, 4), 1000);
	beginCombat(); ASSERT_EQ(battle()->battleActiveUnit(), source);
	BattleAttackInfo collateral(source, target, 0, false); collateral.secondaryAttack = true;
	const auto first = battle()->calculateDmgRange(collateral).damage.min;
	collateral.ordinaryAttackIndex = 1;
	const auto second = battle()->calculateDmgRange(collateral).damage.min;
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(source, primary, source->getPosition())));
	std::vector<int64_t> damage;
	for(const auto & packet : server.attacks)
		for(const auto & hit : packet.bsa)
			if(hit.stackAttacked == target->unitId())
			{
				EXPECT_TRUE(hit.isSecondary());
				damage.push_back(hit.damageAmount);
			}
	EXPECT_EQ(damage, (std::vector<int64_t>{first, second}));
}
TEST_F(NewHorizonsSequenceProtectionTest, SeparatelySelectedMasterGunnerShotStartsNewProtectionSequence)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_EQ(battle()->battleActiveUnit(), source);
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.masterGunner"));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(source, target)));
	ASSERT_TRUE(battle()->battleHasPendingRangedFollowUp(source));
	BattleAttackInfo selected(source, target, 0, true);
	const auto expected = battle()->calculateDmgRange(selected).damage;
	ASSERT_EQ(expected.min, expected.max);
	selected.ordinaryAttackIndex = 1;
	ASSERT_LT(battle()->calculateDmgRange(selected).damage.max, expected.max);
	const auto before = target->getAvailableHealth();
	const auto shots = source->shots.available();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(source, target)));
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(before - target->getAvailableHealth(), expected.min);
	EXPECT_EQ(source->shots.available(), shots - 1);
	EXPECT_FALSE(battle()->battleHasPendingRangedFollowUp(source));
}
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsSequenceProtectionTest, VictimMetadataPhasePreservesPrimaryCollateralAndFreshActionContext)
{
	prepare(false);
	FortuneStrikeProjection strike;
	strike.protectIntercepted = true;
	strike.perfectMoment = true;
	for(int index : {0, 1, 2})
		for(bool followUp : {false, true})
		{
			BattleAttackInfo hit(source, target, 5, false);
			hit.unluckyStrike = true;
			AttackPreviewHitContext::prepareVictimStrike(hit, *battle(), {strike, *target, *target, index, 15, followUp});
			EXPECT_FALSE(hit.secondaryAttack); EXPECT_TRUE(hit.protectIntercepted);
			EXPECT_EQ(hit.ordinaryAttackIndex, followUp ? 0 : index);
			EXPECT_EQ(hit.chargeDistance, index == 0 ? 5 : 0);
			EXPECT_EQ(hit.relentlessAssaultDamagePercent, 15);
			EXPECT_TRUE(hit.luckyStrike); EXPECT_FALSE(hit.unluckyStrike);
			EXPECT_EQ(hit.defenderPos, target->getPosition());
		}
	auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(5, 4), 10);
	BattleAttackInfo collateral(source, target, 3, false);
	AttackPreviewHitContext::prepareVictimStrike(collateral, *battle(), {strike, *other, *target, 1, 0, false});
	EXPECT_TRUE(collateral.secondaryAttack); EXPECT_FALSE(collateral.protectIntercepted);
	EXPECT_EQ(collateral.ordinaryAttackIndex, 1);
	EXPECT_EQ(collateral.luckyStrike, battle()->getLuckRollRules().affectsAllTargets);
	EXPECT_EQ(collateral.chargeDistance, 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, ProjectedSequenceMatchesNativeAndPreservesLiveStateAndRng)
{
	prepare(false); beginCombat();
	const auto health = target->getAvailableHealth();
	const auto sourceShots = source->shots.available();
	auto * rng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	ASSERT_NE(rng, nullptr);
	const auto random = randomBytes(*rng);
	const auto rolls = randomBytes(*gameHandler->randomizer);
	SequenceEnvironment env(gameState());
	// Full native damage parity requires the same explicitly all-knowing policy.
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	ASSERT_EQ(authority->battleGetMySide(), BattleSide::ALL_KNOWING);
	ASSERT_EQ(authority->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).legacyHero(), defenderSideHero);
	auto branch = std::make_shared<HypotheticBattle>(&env, authority);
	DamageCache cache;
	BattleAttackInfo hit(source, target, 0, false);
	const auto expectedFirst = battle()->calculateDmgRange(hit).damage.min;
	hit.ordinaryAttackIndex = 1;
	const auto expectedSecond = battle()->calculateDmgRange(hit).damage.min;
	hit.ordinaryAttackIndex = 0;
	const auto projected = AttackPossibility::evaluate(hit, source->getPosition(), cache, branch);
	const auto victim = std::ranges::find_if(projected.affectedUnits,
		[this](const auto & unit) { return unit->unitId() == target->unitId(); });
	ASSERT_NE(victim, projected.affectedUnits.end());
	EXPECT_EQ(health - (*victim)->getAvailableHealth(), expectedFirst + expectedSecond);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(source->shots.available(), sourceShots);
	EXPECT_EQ(randomBytes(*rng), random);
	EXPECT_EQ(randomBytes(*gameHandler->randomizer), rolls);
}
TEST_F(NewHorizonsSequenceProtectionTest, ProjectedRepeatedCollateralRetainsStrikePosition)
{
	prepare(false);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ATTACKS_ALL_ADJACENT, BonusSource::OTHER, 1, BonusSourceID()));
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(5, 4), 1000);
	beginCombat();
	BattleAttackInfo collateral(source, target, 0, false); collateral.secondaryAttack = true;
	const auto first = battle()->calculateDmgRange(collateral).damage.min;
	collateral.ordinaryAttackIndex = 1;
	const auto second = battle()->calculateDmgRange(collateral).damage.min;
	const auto health = target->getAvailableHealth();
	SequenceEnvironment env(gameState());
	// Full native damage parity requires the same explicitly all-knowing policy.
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	ASSERT_EQ(authority->battleGetMySide(), BattleSide::ALL_KNOWING);
	ASSERT_EQ(authority->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).legacyHero(), defenderSideHero);
	auto branch = std::make_shared<HypotheticBattle>(&env, authority);
	DamageCache cache;
	const auto projected = AttackPossibility::evaluate(BattleAttackInfo(source, primary, 0, false), source->getPosition(), cache, branch);
	const auto victim = std::ranges::find_if(projected.affectedUnits,
		[this](const auto & unit) { return unit->unitId() == target->unitId(); });
	ASSERT_NE(victim, projected.affectedUnits.end());
	EXPECT_EQ(health - (*victim)->getAvailableHealth(), first + second);
	EXPECT_EQ(target->getAvailableHealth(), health);
}
TEST_F(NewHorizonsSequenceProtectionTest, ProjectedSelectedMasterGunnerShotResetsLegacyEventIndex)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_EQ(battle()->battleActiveUnit(), source);
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.masterGunner"));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(source, target)));
	ASSERT_TRUE(battle()->battleHasPendingRangedFollowUp(source));
	BattleAttackInfo selected(source, target, 0, true);
	const auto expected = battle()->battleExpectedLuckDamage(selected);
	const auto health = target->getAvailableHealth();
	SequenceEnvironment env(gameState());
	// Full native damage parity requires the same explicitly all-knowing policy.
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	ASSERT_EQ(authority->battleGetMySide(), BattleSide::ALL_KNOWING);
	ASSERT_EQ(authority->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).legacyHero(), defenderSideHero);
	auto branch = std::make_shared<HypotheticBattle>(&env, authority);
	DamageCache cache;
	const auto projected = AttackPossibility::evaluate(selected, source->getPosition(), cache, branch);
	const auto victim = std::ranges::find_if(projected.affectedUnits,
		[this](const auto & unit) { return unit->unitId() == target->unitId(); });
	ASSERT_NE(victim, projected.affectedUnits.end());
	EXPECT_EQ(health - (*victim)->getAvailableHealth(), expected);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_TRUE(battle()->battleHasPendingRangedFollowUp(source));
}
TEST_F(NewHorizonsSequenceProtectionTest, YogBranchDamageUsesSamePenetrationWithoutChangingLiveBonuses)
{
	prepare(true); protectTarget();
	SequenceEnvironment env(gameState());
	auto branch = std::make_shared<HypotheticBattle>(&env, std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	const auto actor = branch->getForUpdate(source->unitId());
	const auto victim = branch->getForUpdate(target->unitId());
	const auto live = battle()->calculateDmgRange(BattleAttackInfo(source, target, 0, true));
	const auto forecast = branch->calculateDmgRange(BattleAttackInfo(actor.get(), victim.get(), 0, true));
	EXPECT_EQ(live.damage.min, forecast.damage.min); EXPECT_EQ(live.damage.max, forecast.damage.max);
	EXPECT_TRUE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*branch));
	victim->addUnitBonus({Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*branch, BattleAttackInfo(actor.get(), victim.get(), 0, true)), 0);
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*battle(), BattleAttackInfo(source, target, 0, true)), 25);
}
#endif

TEST_F(NewHorizonsSequenceProtectionTest, NativePlayerCannotReadHiddenYogPolicyButOwnAndAuthorityCan)
{
	prepare(true);
	const CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	const CPlayerBattleCallback known(battle(), PlayerColor(0));
	const BattleAttackInfo shot(source, target, 0, true);
	ASSERT_EQ(hidden.battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	ASSERT_EQ(known.battleGetVisibleHeroPolicy(BattleSide::ATTACKER).legacyHero(), attackerSideHero);
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(hidden, shot), 0);
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(known, shot), 25);
	EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*battle(), shot), 25);
}
TEST_F(NewHorizonsSequenceProtectionTest, NativePlayerCannotReadHiddenUfretinPolicyButOwnAndAuthorityCan)
{
	prepare(false);
	const CPlayerBattleCallback hidden(battle(), PlayerColor(0));
	const CPlayerBattleCallback known(battle(), PlayerColor(1));
	BattleAttackInfo hit(source, target, 0, false); hit.ordinaryAttackIndex = 1;
	ASSERT_EQ(hidden.battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	ASSERT_EQ(known.battleGetVisibleHeroPolicy(BattleSide::DEFENDER).legacyHero(), defenderSideHero);
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(hidden, hit), 0);
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(known, hit), 25);
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*battle(), hit), 25);
	hit.ordinaryAttackIndex = 0;
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(known, hit), 0);
}
TEST_F(NewHorizonsSequenceProtectionTest, VisibilityPolicyUsesEffectiveOwnerRatherThanOriginalSide)
{
	prepare(false);
	const CPlayerBattleCallback ufretinView(battle(), PlayerColor(1));
	auto * borrowed = addStack(BattleSide::ATTACKER, creatureByName("core:dwarf"), BattleHex(4, 5), 10);
	borrowed->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	BattleAttackInfo hit(source, borrowed, 0, false); hit.ordinaryAttackIndex = 1;
	ASSERT_EQ(borrowed->unitSide(), BattleSide::ATTACKER);
	ASSERT_EQ(ufretinView.battleGetOwner(borrowed), PlayerColor(1));
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(ufretinView, hit), 25);
	const CPlayerBattleCallback hidden(battle(), PlayerColor(0));
	EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(hidden, hit), 0);
}
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsSequenceProtectionTest, ProjectionCannotReadHiddenYogPolicyButKnownAndAuthorityCan)
{
	prepare(true);
	SequenceEnvironment env(gameState());
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	const std::vector<std::pair<std::shared_ptr<CBattleInfoCallback>, int>> views{
		{std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1)), 0},
		{std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)), 25}, {authority, 25}};
	for(const auto & [view, expected] : views)
	{
		const auto knowledge = expected == 0 ? VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN
			: VisibleHeroPolicy::Knowledge::VISIBLE_HERO;
		ASSERT_EQ(view->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), knowledge);
		auto branch = std::make_shared<HypotheticBattle>(&env, view);
		const auto actor = branch->getForUpdate(source->unitId());
		const auto victim = branch->getForUpdate(target->unitId());
		ASSERT_EQ(branch->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(), knowledge);
		EXPECT_EQ(newHorizonsSequenceProtection::cyclopeanBreachPercent(*branch,
			BattleAttackInfo(actor.get(), victim.get(), 0, true)), expected);
	}
}
TEST_F(NewHorizonsSequenceProtectionTest, ProjectionCannotReadHiddenUfretinPolicyButKnownAndAuthorityCan)
{
	prepare(false);
	SequenceEnvironment env(gameState());
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	const std::vector<std::pair<std::shared_ptr<CBattleInfoCallback>, int>> views{
		{std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)), 0},
		{std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1)), 25}, {authority, 25}};
	for(const auto & [view, expected] : views)
	{
		const auto knowledge = expected == 0 ? VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN
			: VisibleHeroPolicy::Knowledge::VISIBLE_HERO;
		ASSERT_EQ(view->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), knowledge);
		auto branch = std::make_shared<HypotheticBattle>(&env, view);
		const auto actor = branch->getForUpdate(source->unitId());
		const auto victim = branch->getForUpdate(target->unitId());
		BattleAttackInfo hit(actor.get(), victim.get(), 0, false); hit.ordinaryAttackIndex = 1;
		ASSERT_EQ(branch->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), knowledge);
		EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*branch, hit), expected);
		hit.ordinaryAttackIndex = 0;
		EXPECT_EQ(newHorizonsSequenceProtection::runicEndurancePercent(*branch, hit), 0);
	}
}
#endif
