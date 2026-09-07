/*
 * HeroCommandFixture.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "BattleTestFixture.h"
#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

class HeroCommandFixture : public BattleTestFixture
{
protected:
	bool useCommands = true;

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules;
		if(useCommands)
		{
			const JsonNode file(JsonPath::builtin("config/newHorizonsCombat"));
			rules = file["combat"]["heroCommands"];
			heroCommands::validateRules(rules);
		}
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
		// A legacy-command fixture must also retain legacy primary ratings.
		// Expanded ratings without their required commands is invalid, not legacy.
		if(!useCommands)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode());
	}

	void prepareCommands(bool spellbook = false)
	{
		startGame();
		if(spellbook)
		{
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
			attackerSideHero->mana = 100;
		}
		startBattle();
		beginCombat();
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}

	bool issue(HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
	}

	BattleAction heroAction(int kind)
	{
		if(kind == 1)
			return BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE);
		if(kind == 2)
			return BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::AGGRESSIVE);
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::HASTE;
		action.aimToUnit(battle()->battleActiveUnit());
		return action;
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}
};
