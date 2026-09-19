/*
 * FocusFireFixture.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "HeroCommandFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusCustomTypes.h"

/// Authored test override, not installed-profile proof. Ordinary angel stats give
/// fixed100 base damage (two angels) against an equal-defense angel target.
class FocusFireFixture : public HeroCommandFixture
{
protected:
	int focusBase = 30;
	int shooterCount = 2;
	CStack * shooter = nullptr;
	CStack * target = nullptr;

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		const JsonNode file(JsonPath::builtin("config/newHorizonsCombatV2"));
		auto rules = file["combat"]["heroCommands"];
		rules["rulesetVersion"].Integer() = heroCommands::TARGETED_RULESET_VERSION;
		auto & focus = rules["commands"]["focusFire"];
		focus["kind"].String() = "order";
		focus["duration"].String() = "round";
		focus["coverage"].String() = "ownOrdinaryShootersAtIssue";
		focus["target"].String() = "enemyUnit";
		auto & formula = focus["effects"]["rangedDamagePercent"];
		formula["base"].Integer() = focusBase;
		formula["attack"].Float() = 0.5;
		formula["defense"].Integer() = 0;
		heroCommands::validateRules(rules);
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
	}

	CStack * addShooter(BattleSide side, BattleHex position, int shots = 1, int count = 2)
	{
		auto * unit = addStack(side, creatureByName("core:angel"), position, count);
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SHOOTER, BonusSource::OTHER, 1, BonusSourceID()));
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SHOTS, BonusSource::OTHER, shots, BonusSourceID()));
		return unit;
	}

	void activate(const CStack * unit)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = unit->unitId();
		pack.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(pack);
	}

	void prepareFocus()
	{
		startGame();
		prepareFocusBattle();
	}

	void prepareFocusBattle()
	{
		startBattle();
		shooter = addShooter(BattleSide::ATTACKER, BattleHex(leftHex), 1, shooterCount);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
		shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::PERCENTAGE_DAMAGE_BOOST, BonusSource::OTHER, 50, BonusSourceID(),
			BonusCustomSubtype::damageTypeRanged));
		beginCombat();
		activate(shooter);
		ASSERT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), 0);
		ASSERT_EQ(shooter->shots.available(), 1);
		ASSERT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()));
		ASSERT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	}

	BattleAction focusAction(uint32_t id) const
	{
		return BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, id);
	}

	bool submit(const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
};
