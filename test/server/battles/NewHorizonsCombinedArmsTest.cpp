/*
 * NewHorizonsCombinedArmsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"

namespace
{
constexpr char COMMAND_SKILL[] = "new-horizons:command";
constexpr char AGGRESSIVE_COMMANDER[] = "new-horizons:command.aggressiveCommander";
constexpr char COMBINED_ARMS[] = "new-horizons:command.combinedArms";

class NewHorizonsCombinedArmsTest : public HeroCommandFixture
{
protected:
	bool useLegacyRules = false;

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
		if(useLegacyRules)
		{
			const JsonNode config(JsonPath::builtin("config/newHorizonsCombatV2"));
			loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS,
				config["combat"]["heroCommands"]);
		}
	}

	void prepareCombinedHero()
	{
		startGame();
		const int commandId = SecondarySkill::decode(COMMAND_SKILL);
		ASSERT_GE(commandId, 0);
		const auto command = SecondarySkill(commandId);
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 95, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(command, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({COMMAND_SKILL, AGGRESSIVE_COMMANDER});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, AGGRESSIVE_COMMANDER));
		attackerSideHero->setSecSkillLevel(command, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({COMMAND_SKILL, COMBINED_ARMS});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(COMMAND_SKILL, COMBINED_ARMS));
		startBattle();
	}

	void activate(const CStack * unit)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = unit->unitId();
		pack.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(pack);
	}

	bool submit(HeroCommand command, uint32_t targetId)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, command, targetId));
	}
};
}

TEST_F(NewHorizonsCombinedArmsTest, MeleeOnlyFocusFireUsesHalfOfTheOddFrozenBonus)
{
	prepareCombinedHero();
	auto * melee = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	auto * otherTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 5), 100);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 5), 100);
	beginCombat();
	activate(melee);

	ASSERT_FALSE(melee->isShooter());
	EXPECT_TRUE(battle()->battleGetUnitsIf([](const battle::Unit * unit)
	{
		return unit->isShooter() && unit->unitSide() == BattleSide::ATTACKER;
	}).empty());
	ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(melee, target, 0, false));
	const auto actionsBefore = server.startedActions.size();
	EXPECT_FALSE(submit(HeroCommand::FOCUS_FIRE, ally->unitId()));
	EXPECT_EQ(server.startedActions.size(), actionsBefore);
	ASSERT_TRUE(submit(HeroCommand::FOCUS_FIRE, target->unitId()));

	const auto mark = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(mark);
	ASSERT_EQ(mark->targetUnitId, target->unitId());
	ASSERT_EQ(mark->rangedDamagePercent, 25);
	ASSERT_TRUE(std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), melee->unitId()));
	EXPECT_DOUBLE_EQ(heroCommands::combinedArmsFocusFirePercent(mark->rangedDamagePercent, *attackerSideHero), 12.5);

	// Membership is frozen at issue: a later melee arrival cannot borrow the Order.
	auto * lateMelee = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 3), 100);
	EXPECT_FALSE(std::binary_search(mark->recipientUnitIds.begin(), mark->recipientUnitIds.end(), lateMelee->unitId()));

	const auto marked = battle()->calculateDmgRange(BattleAttackInfo(melee, target, 0, false));
	EXPECT_EQ(marked.attackerOrderCause, HeroCommand::FOCUS_FIRE);
	EXPECT_GT(marked.damage.min, before.damage.min);
	EXPECT_FALSE(battle()->battleIsTargetedRangedCommand(melee, target, true));
	EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(melee, target, true), 0);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(melee, otherTarget, 0, false)).attackerOrderCause,
		HeroCommand::NONE);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(melee, ally, 0, false)).attackerOrderCause,
		HeroCommand::NONE);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(lateMelee, target, 0, false)).attackerOrderCause,
		HeroCommand::NONE);
	BattleAttackInfo secondary(melee, target, 0, false);
	secondary.secondaryAttack = true;
	EXPECT_EQ(battle()->calculateDmgRange(secondary).attackerOrderCause, HeroCommand::NONE);
	BattleAttackInfo nonphysical(melee, target, 0, false);
	nonphysical.physicalDamage = false;
	EXPECT_EQ(battle()->calculateDmgRange(nonphysical).attackerOrderCause, HeroCommand::NONE);

	blockRetaliation(target);
	server.attacks.clear();
	ASSERT_TRUE(this->attack(melee, target->getPosition()));
	const auto attack = std::ranges::find_if(server.attacks, [melee](const BattleAttack & value)
	{
		return value.stackAttacking == melee->unitId() && !value.counter();
	});
	ASSERT_NE(attack, server.attacks.end());
	const auto hit = std::ranges::find(attack->bsa, target->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, attack->bsa.end());
	const auto causeLine = std::ranges::find_if(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Focus Fire:") != std::string::npos;
	});
	ASSERT_NE(causeLine, server.battleLogLines.end());
	EXPECT_THAT(*causeLine, ::testing::HasSubstr(std::to_string(hit->damageAmount) + " damage"));
}

TEST_F(NewHorizonsCombinedArmsTest, FlankRangedDamageUsesOnlyHalfTheAttackTermAndNeverRecordsSides)
{
	prepareCombinedHero();
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 4), 100);
	beginCombat();
	activate(shooter);
	ASSERT_TRUE(shooter->isShooter());
	ASSERT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()));
	ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FLANK));

	const auto before = battle()->calculateDmgRange(BattleAttackInfo(shooter, target, 0, true));
	ASSERT_TRUE(submit(HeroCommand::FLANK, target->unitId()));
	const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(state);
	const auto * flank = state->flankFor(target->unitId());
	ASSERT_NE(flank, nullptr);
	EXPECT_EQ(flank->sideMask, 0);

	const auto & formula = battle()->getHeroCommandRules()["commands"]["flank"]["effects"]["meleeDamagePercent"];
	const auto combinedArms = heroCommands::combinedArmsFlankPercent(formula, *attackerSideHero);
	EXPECT_DOUBLE_EQ(combinedArms, 7.98); // 0.12 * 95 Attack * 140% efficiency / 2
	EXPECT_NE(combinedArms, heroCommands::coefficient(formula, *attackerSideHero) / 2.0);

	const auto marked = battle()->calculateDmgRange(BattleAttackInfo(shooter, target, 0, true));
	EXPECT_EQ(marked.attackerOrderCause, HeroCommand::FLANK);
	EXPECT_GT(marked.damage.min, before.damage.min);
	BattleAttackInfo secondary(shooter, target, 0, true);
	secondary.secondaryAttack = true;
	EXPECT_EQ(battle()->calculateDmgRange(secondary).attackerOrderCause, HeroCommand::FLANK);
	BattleAttackInfo nonphysical(shooter, target, 0, true);
	nonphysical.physicalDamage = false;
	EXPECT_EQ(battle()->calculateDmgRange(nonphysical).attackerOrderCause, HeroCommand::NONE);
	auto * otherTarget = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex + 7), 100);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex + 7), 100);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(shooter, otherTarget, 0, true)).attackerOrderCause,
		HeroCommand::NONE);
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(shooter, ally, 0, true)).attackerOrderCause,
		HeroCommand::NONE);
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(shooter, target)));
	const auto attack = std::ranges::find_if(server.attacks, [shooter](const BattleAttack & value)
	{
		return value.stackAttacking == shooter->unitId() && value.shot() && !value.counter();
	});
	ASSERT_NE(attack, server.attacks.end());
	const auto hit = std::ranges::find(attack->bsa, target->unitId(), &BattleStackAttacked::stackAttacked);
	ASSERT_NE(hit, attack->bsa.end());
	const auto resolvedState = battle()->battleGetHeroOrderState(BattleSide::ATTACKER);
	ASSERT_TRUE(resolvedState);
	const auto * resolvedFlank = resolvedState->flankFor(target->unitId());
	ASSERT_NE(resolvedFlank, nullptr);
	EXPECT_EQ(resolvedFlank->sideMask, 0);
}

TEST_F(NewHorizonsCombinedArmsTest, ActivePerkDoesNotBroadenLegacyFocusFireCohortsOrAdmission)
{
	useLegacyRules = true;
	prepareCombinedHero();
	auto * melee = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	beginCombat();
	activate(melee);

	ASSERT_TRUE(heroCommands::hasCombinedArms(attackerSideHero));
	EXPECT_FALSE(heroCommands::isCanonicalRules(battle()->getHeroCommandRules()));
	EXPECT_FALSE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	EXPECT_FALSE(submit(HeroCommand::FOCUS_FIRE, target->unitId()));
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_EQ(battle()->calculateDmgRange(BattleAttackInfo(melee, target, 0, false)).attackerOrderCause,
		HeroCommand::NONE);
}
