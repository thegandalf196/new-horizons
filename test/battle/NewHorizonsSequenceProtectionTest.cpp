/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <algorithm>
#include "../server/battles/HeroCommandFixture.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/spells/NewHorizonsRunicReflection.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/networkPacks/SetStackEffect.h"
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
	CStack * source = nullptr;
	CStack * target = nullptr;
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));

		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
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
		if(machine)
		{
			const SecondarySkill skill(SecondarySkill::decode("new-horizons:warMachines"));
			attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.masterGunner"));
		}
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

TEST_F(NewHorizonsSequenceProtectionTest, AuthoritativeWolfRaiderSequenceHasNoRetiredAdditionalHitProtection)
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
	ASSERT_EQ(battle()->calculateDmgRange(selected).damage.max, expected.max);
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

#endif

TEST_F(NewHorizonsSequenceProtectionTest, YogNoLongerPenetratesIndependentProtectionOrChangesDefenseAndRange)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	BattleAttackInfo shot(source, target, 0, true);
	const auto ordinary = battle()->calculateDmgRange(shot);
	const auto defense = target->getDefense(true);
	const auto range = source->getShootingRangeDistance();
	const auto fullDamage = source->getRangedFullDamageDistance();
	protectTarget();
	const auto protectedDamage = battle()->calculateDmgRange(shot);
	EXPECT_NEAR(protectedDamage.damage.min, ordinary.damage.min * .6, 1.0);
	EXPECT_EQ(protectedDamage.damageBeforeDefense.min, ordinary.damageBeforeDefense.min);
	EXPECT_EQ(protectedDamage.damageBeforeDefense.max, ordinary.damageBeforeDefense.max);
	EXPECT_EQ(target->getDefense(true), defense);
	EXPECT_EQ(source->getShootingRangeDistance(), range);
	EXPECT_EQ(source->getRangedFullDamageDistance(), fullDamage);
}

TEST_F(NewHorizonsSequenceProtectionTest, UfretinHasNoAdditionalHitReductionIncludingRepeatedCollateral)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	protectTarget();
	for(const bool secondary : {false, true})
	{
		BattleAttackInfo hit(source, target, 0, false); hit.secondaryAttack = secondary;
		const auto ordinary = battle()->calculateDmgRange(hit);
		for(const int index : {1, 2, 5})
		{
			hit.ordinaryAttackIndex = index;
			const auto repeated = battle()->calculateDmgRange(hit);
			EXPECT_EQ(repeated.damage.min, ordinary.damage.min);
			EXPECT_EQ(repeated.damage.max, ordinary.damage.max);
			EXPECT_EQ(repeated.damageBeforeDefense.min, ordinary.damageBeforeDefense.min);
			EXPECT_EQ(repeated.damageBeforeDefense.max, ordinary.damageBeforeDefense.max);
		}
	}
}

TEST_F(NewHorizonsSequenceProtectionTest, RunicNativeShareTracksEffectiveFormWithoutAddingResistance)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*target), 40);
	EXPECT_EQ(target->magicResistance(), 40);
	auto * dwarf = addStack(BattleSide::DEFENDER, creatureByName("core:dwarf"), BattleHex(7, 6), 10);
	EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*dwarf), 20);
	EXPECT_EQ(dwarf->magicResistance(), 20);
	const auto runicRows = [&]()
	{
		return std::ranges::count_if(defenderSideHero->getExportedBonusList(), [this](const auto & bonus)
		{
			return bonus && bonus->type == BonusType::NATIVE_RESISTANCE_REFLECTION
				&& bonus->source == BonusSource::HERO_SPECIAL && bonus->sid == BonusSourceID(defenderSideHero->getHeroTypeID())
				&& bonus->stacking == "nativeResistanceReflection";
		});
	};
	EXPECT_EQ(runicRows(), 2);
	for(int refresh = 0; refresh < 2; ++refresh)
	{
		defenderSideHero->initHero(*gameHandler->randomizer);
		EXPECT_EQ(runicRows(), 2);
		EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*dwarf), 20);
		EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*target), 40);
	}
	target->beginBattleForm(creatureByName("core:pikeman"), 2);
	EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*target), 0);
	EXPECT_EQ(target->magicResistance(), 0);
	target->endBattleForm();
	EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*target), 40);
	auto * borrowed = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 6), 10);
	borrowed->beginBattleForm(creatureByName("core:dwarf"), 2);
	EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*borrowed), 20);
	borrowed->endBattleForm();
	EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*borrowed), 0);
}

TEST_F(NewHorizonsSequenceProtectionTest, CurrentNumericRunicEffectIsPublicWhileItsHeroPolicyRemainsHidden)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const CPlayerBattleCallback hidden(battle(), PlayerColor(0));
	const CPlayerBattleCallback known(battle(), PlayerColor(1));
	ASSERT_EQ(hidden.battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	ASSERT_EQ(known.battleGetVisibleHeroPolicy(BattleSide::DEFENDER).legacyHero(), defenderSideHero);
	EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*hidden.battleGetUnitByID(target->unitId())), 40);
	EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*known.battleGetUnitByID(target->unitId())), 40);
	SetStackEffect control; control.battleID = BattleID(0);
	control.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
	gameHandler->sendAndApply(control);
	ASSERT_EQ(battle()->battleGetOwner(target), PlayerColor(0));
	EXPECT_EQ(target->unitSide(), BattleSide::DEFENDER);
	EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*target), 40);
	auto * foreign = addStack(BattleSide::ATTACKER, creatureByName("core:dwarf"), BattleHex(4, 6), 10);
	EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*foreign), 0);
}
#if ENABLE_BATTLE_AI
TEST_F(NewHorizonsSequenceProtectionTest, UnknownRunicProjectionKeepsNumericNativeShareWithoutPromotingHeroKnowledge)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	SequenceEnvironment env(gameState());
	const auto view = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&env, view);
	auto child = std::make_shared<HypotheticBattle>(&env, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&env, parent);
	const auto before = target->save();
	for(const auto & branch : {parent, child, sibling})
	{
		ASSERT_EQ(branch->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
		const auto victim = branch->getForUpdate(target->unitId());
		EXPECT_EQ(newHorizonsRunicReflection::nativeChance(*victim), 40);
		EXPECT_EQ(victim->magicResistance(), 40);
	}
	EXPECT_EQ(target->save(), before);
}
#endif
