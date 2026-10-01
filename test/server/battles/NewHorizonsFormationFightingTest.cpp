/*
 * NewHorizonsFormationFightingTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/battle/NewHorizonsShroud.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
constexpr char ARMORER_SKILL[] = "new-horizons:armorer";
constexpr char ARMORER_PAVISE[] = "new-horizons:armorer.pavise";
constexpr char FORMATION_FIGHTING[] = "new-horizons:armorer.formationFighting";
constexpr char COMMAND_SKILL[] = "new-horizons:command";
constexpr char AGGRESSIVE_COMMANDER[] = "new-horizons:command.aggressiveCommander";
constexpr char COMBINED_ARMS[] = "new-horizons:command.combinedArms";

class FormationFightingTest : public HeroCommandFixture
{
protected:
	int physicalReductionCapPercent = 80;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		auto magicRules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		magicRules["physicalDamageReductionCapPercent"].Integer() = physicalReductionCapPercent;
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
	}

	SecondarySkill skill(const char * identifier) const
	{
		const int decoded = SecondarySkill::decode(identifier);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void selectArmorerAdvanced(CGHeroInstance * hero)
	{
		const auto armorer = skill(ARMORER_SKILL);
		hero->setSecSkillLevel(armorer, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({ARMORER_SKILL, ARMORER_PAVISE});
		ASSERT_TRUE(hero->hasActivePerk(ARMORER_SKILL, ARMORER_PAVISE));
		hero->setSecSkillLevel(armorer, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({ARMORER_SKILL, FORMATION_FIGHTING});
		ASSERT_TRUE(hero->hasActivePerk(ARMORER_SKILL, FORMATION_FIGHTING));
		EXPECT_EQ(newHorizonsCombatSkills::formationFightingReductionPercent(hero), 10);
	}

	void selectCommandCombinedArms(CGHeroInstance * hero)
	{
		const auto command = skill(COMMAND_SKILL);
		hero->setPrimarySkill(PrimarySkill::ATTACK, 95, ChangeValueMode::ABSOLUTE);
		hero->setSecSkillLevel(command, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({COMMAND_SKILL, AGGRESSIVE_COMMANDER});
		ASSERT_TRUE(hero->hasActivePerk(COMMAND_SKILL, AGGRESSIVE_COMMANDER));
		hero->setSecSkillLevel(command, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({COMMAND_SKILL, COMBINED_ARMS});
		ASSERT_TRUE(hero->hasActivePerk(COMMAND_SKILL, COMBINED_ARMS));
	}

	void activate(CStack * unit)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = unit->unitId();
		pack.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(pack);
	}

	void removeStartingStacks()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void moveStack(CStack * stack, const BattleHex & destination)
	{
		auto movedState = stack->acquireState();
		movedState->setPosition(destination);

		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = movedState->save();
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}

	void killStack(CStack * stack)
	{
		auto killedState = stack->acquireState();
		int64_t damage = killedState->getAvailableHealth();
		ASSERT_GT(damage, 0);
		killedState->damage(damage);

		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = killedState->save();
		change.healthDelta = -damage;
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
	}
};

class FormationFightingEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit FormationFightingEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

TEST_F(FormationFightingTest, ProtectionRequiresSelectedAdvancedPerkAndCurrentAdvancedRank)
{
	startGame();
	const auto armorer = skill(ARMORER_SKILL);
	defenderSideHero->setSecSkillLevel(armorer, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	defenderSideHero->applyPerkSelection({ARMORER_SKILL, ARMORER_PAVISE});
	ASSERT_TRUE(defenderSideHero->hasActivePerk(ARMORER_SKILL, ARMORER_PAVISE));

	startBattle();
	removeStartingStacks();
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 10);
	auto * friendUnit = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(80), 10);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(friendUnit, nullptr);
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(defender));
	EXPECT_EQ(newHorizonsCombatSkills::formationFightingReductionPercent(defenderSideHero), 0);

	defenderSideHero->setSecSkillLevel(armorer, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(defenderSideHero->hasActivePerk(ARMORER_SKILL, FORMATION_FIGHTING));
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(defender));

	defenderSideHero->applyPerkSelection({ARMORER_SKILL, FORMATION_FIGHTING});
	ASSERT_TRUE(defenderSideHero->hasActivePerk(ARMORER_SKILL, FORMATION_FIGHTING));
	EXPECT_TRUE(battle()->battleHasFormationFightingProtection(defender));

	defenderSideHero->setSecSkillLevel(armorer, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(defenderSideHero->hasActivePerk(ARMORER_SKILL, FORMATION_FIGHTING));
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(defender))
		<< "A stored Advanced choice stops applying if Armorer rank falls below Advanced";
}

TEST_F(FormationFightingTest, PhysicalReductionMatchesDetachedForecastAndAuthoritativeMeleeHit)
{
	startGame();
	selectArmorerAdvanced(defenderSideHero);
	startBattle();
	removeStartingStacks();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	auto * friendUnit = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 1), 100);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(friendUnit, nullptr);
	forceMaximumDamage(attacker);
	blockRetaliation(defender);
	beginCombat();

	const BattleAttackInfo physical(attacker, defender, 0, false);
	ASSERT_TRUE(battle()->battleHasFormationFightingProtection(defender));
	const auto protectedDamage = battle()->calculateDmgRange(physical).damage;
	ASSERT_GT(protectedDamage.max, 0);

	BattleAttackInfo unprotected = physical;
	unprotected.defenderPos = BattleHex(rightHex + 3 * GameConstants::BFIELD_WIDTH);
	const auto unprotectedDamage = battle()->calculateDmgRange(unprotected).damage;
	EXPECT_NEAR(protectedDamage.min, unprotectedDamage.min * 90 / 100, 1);
	EXPECT_NEAR(protectedDamage.max, unprotectedDamage.max * 90 / 100, 1)
		<< "Formation Fighting is an independent additional 10% physical reduction";

	BattleAttackInfo nonphysical = physical;
	nonphysical.physicalDamage = false;
	BattleAttackInfo nonphysicalAtFarPosition = nonphysical;
	nonphysicalAtFarPosition.defenderPos = unprotected.defenderPos;
	EXPECT_EQ(battle()->calculateDmgRange(nonphysical).damage.max,
		battle()->calculateDmgRange(nonphysicalAtFarPosition).damage.max)
		<< "Formation Fighting does not reduce magical or otherwise nonphysical damage";

	FormationFightingEnvironment environment(gameState());
	// Use the defending player's view so the projected model can include its own
	// Armorer perk without depending on hidden opposing-hero information.
	auto liveCallback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	HypotheticBattle prediction(&environment, liveCallback);
	auto predictedAttacker = prediction.getForUpdate(attacker->unitId());
	auto predictedDefender = prediction.getForUpdate(defender->unitId());
	const auto predicted = prediction.calculateDmgRange(
		BattleAttackInfo(predictedAttacker.get(), predictedDefender.get(), 0, false)).damage;
	EXPECT_EQ(predicted.min, protectedDamage.min);
	EXPECT_EQ(predicted.max, protectedDamage.max);

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	const auto attackResult = std::ranges::find_if(server.attacks, [attacker](const BattleAttack & value)
	{
		return value.stackAttacking == attacker->unitId() && !value.counter();
	});
	ASSERT_NE(attackResult, server.attacks.end());
	const auto hit = std::ranges::find(attackResult->bsa, defender->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, attackResult->bsa.end());
	EXPECT_EQ(hit->damageAmount, protectedDamage.max)
		<< "The authoritative melee result uses the same protected physical damage as the live forecast";
}

TEST_F(FormationFightingTest, ProtectsRearContactFromShroudAndFlankButPreservesCombinedArmsRangedBonus)
{
	startGame();
	selectArmorerAdvanced(defenderSideHero);
	selectCommandCombinedArms(attackerSideHero);
	const auto shroudSkill = SecondarySkill(SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID)));
	attackerSideHero->setSecSkillLevel(shroudSkill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	startBattle();
	removeStartingStacks();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(82), 100);
	auto * rangedShooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	auto * friendUnit = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(80), 100);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(rangedShooter, nullptr);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(friendUnit, nullptr);
	forceMaximumDamage(attacker);
	blockRetaliation(defender);
	beginCombat();
	activate(attacker);

	BattleAttackInfo rearAttack(attacker, defender, 0, false);
	ASSERT_FALSE(battle()->battleIsShroudFlankingAttack(rearAttack));
	EXPECT_FALSE(battle()->battleShroudDeniesRetaliation(rearAttack));
	const auto beforeFlank = battle()->calculateDmgRange(rearAttack);
	ASSERT_TRUE(battle()->battleCanShoot(rangedShooter, defender->getPosition()));
	const BattleAttackInfo rangedAttack(rangedShooter, defender, 0, true);
	const auto beforeRangedFlank = battle()->calculateDmgRange(rangedAttack);
	ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FLANK));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FLANK, defender->unitId())));
	EXPECT_EQ(battle()->battleHeroOrderFlankSide(attacker, defender), 0);
	const auto protectedFlank = battle()->calculateDmgRange(rearAttack);
	EXPECT_EQ(protectedFlank.damage.min, beforeFlank.damage.min);
	EXPECT_EQ(protectedFlank.damage.max, beforeFlank.damage.max);
	EXPECT_EQ(protectedFlank.attackerOrderCause, HeroCommand::NONE)
		<< "A protected target receives neither Shroud's rear bonus nor FLANK's melee bonus";
	const auto combinedArmsShot = battle()->calculateDmgRange(rangedAttack);
	EXPECT_EQ(combinedArmsShot.attackerOrderCause, HeroCommand::FLANK);
	EXPECT_GT(combinedArmsShot.damage.min, beforeRangedFlank.damage.min)
		<< "Formation Fighting protects melee from FLANK but leaves Combined Arms' ranged component intact";

	moveStack(friendUnit, BattleHex(75));
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(defender));
	EXPECT_TRUE(battle()->battleIsShroudFlankingAttack(rearAttack));
	EXPECT_TRUE(battle()->battleShroudDeniesRetaliation(rearAttack));
	EXPECT_NE(battle()->battleHeroOrderFlankSide(attacker, defender), 0)
		<< "Once the formation is broken, FLANK and Shroud can recognize the same rear contact again";
	EXPECT_EQ(battle()->calculateDmgRange(rearAttack).attackerOrderCause, HeroCommand::FLANK);
	moveStack(friendUnit, BattleHex(80));
	ASSERT_TRUE(battle()->battleHasFormationFightingProtection(defender));

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	const auto attackResult = std::ranges::find_if(server.attacks, [attacker](const BattleAttack & value)
	{
		return value.stackAttacking == attacker->unitId() && !value.counter();
	});
	ASSERT_NE(attackResult, server.attacks.end());
	const auto hit = std::ranges::find(attackResult->bsa, defender->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, attackResult->bsa.end());
	EXPECT_EQ(hit->damageAmount, protectedFlank.damage.max);
}

TEST_F(FormationFightingTest, NeighborOwnershipMovementAndDeathUpdateProtection)
{
	startGame();
	selectArmorerAdvanced(defenderSideHero);
	startBattle();
	removeStartingStacks();
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 10);
	auto * sameSide = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(80), 10);
	auto * opposingSide = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(82), 10);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(sameSide, nullptr);
	ASSERT_NE(opposingSide, nullptr);
	ASSERT_TRUE(battle()->battleHasFormationFightingProtection(defender));

	auto sameSideControl = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	sameSide->addNewBonus(sameSideControl);
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(defender))
		<< "An adjacent stack that is currently controlled by the opponent is not a friendly formation member";

	auto convertedEnemyControl = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	opposingSide->addNewBonus(convertedEnemyControl);
	EXPECT_TRUE(battle()->battleHasFormationFightingProtection(defender))
		<< "A currently allied controlled stack qualifies even when its original side differs";
	opposingSide->removeBonus(convertedEnemyControl);
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(defender));
	sameSide->removeBonus(sameSideControl);
	EXPECT_TRUE(battle()->battleHasFormationFightingProtection(defender));

	moveStack(sameSide, BattleHex(75));
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(defender));
	moveStack(sameSide, BattleHex(80));
	EXPECT_TRUE(battle()->battleHasFormationFightingProtection(defender));
	killStack(sameSide);
	ASSERT_FALSE(sameSide->alive());
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(defender))
		<< "A dead adjacent stack no longer provides Formation Fighting";
}

TEST_F(FormationFightingTest, DoubleWideFootprintAndDetachedSelfAliasesDoNotCreateFalseFormation)
{
	startGame();
	selectArmorerAdvanced(defenderSideHero);
	startBattle();
	removeStartingStacks();
	auto * wide = addStack(BattleSide::DEFENDER, creatureByName("core:blackDragon"), BattleHex(81), 1);
	ASSERT_NE(wide, nullptr);
	ASSERT_TRUE(wide->unitType()->isDoubleWide());
	const auto loneWideState = wide->acquireState();
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(loneWideState.get()))
		<< "A detached copy of a lone two-hex stack is not its own supporting neighbor";

	auto * support = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(83), 1);
	ASSERT_NE(support, nullptr);
	EXPECT_TRUE(battle()->battleHasFormationFightingProtection(wide))
		<< "The supporting stack touches only the wide unit's occupied tail hex";
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(wide, BattleHex(85)))
		<< "A projected wide-stack position outside the current support does not reuse stale adjacency";

	auto * singleton = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(115), 1);
	ASSERT_NE(singleton, nullptr);
	auto movedAlias = singleton->acquireState();
	movedAlias->setPosition(BattleHex(116));
	EXPECT_FALSE(battle()->battleHasFormationFightingProtection(movedAlias.get(), BattleHex(116)))
		<< "The live position of a detached single-hex copy cannot count as a second unit with the same ID";
}

TEST_F(FormationFightingTest, PhysicalReductionCannotExceedTheConfiguredCap)
{
	startGame();
	selectArmorerAdvanced(defenderSideHero);
	startBattle();
	removeStartingStacks();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	auto * friendUnit = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 1), 100);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	ASSERT_NE(friendUnit, nullptr);
	ASSERT_TRUE(battle()->battleHasFormationFightingProtection(defender));
	forceMaximumDamage(attacker);
	BattleAttackInfo magical(attacker, defender, 0, false);
	magical.physicalDamage = false;
	const auto rawWithoutPhysicalMitigation = battle()->calculateDmgRange(magical).damage;
	defender->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::GENERAL_DAMAGE_REDUCTION, BonusSource::OTHER, 90, BonusSourceID(),
		BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));

	const auto rawPhysical = battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, false)).damage;
	ASSERT_GT(rawWithoutPhysicalMitigation.max, 0);
	EXPECT_NEAR(rawPhysical.max, rawWithoutPhysicalMitigation.max * 20 / 100, 1)
		<< "Formation Fighting and other reductions respect the saved 80% physical reduction cap";
}
