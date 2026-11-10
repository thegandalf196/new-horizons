/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGameState.h"
#include "../StartInfo.h"
#include "../GameSettings.h"
#include "../mapping/CMap.h"
#include "../campaign/CampaignState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsCommandAdmission.h"
#include "../battle/BattleInfo.h"

void BattleInfo::validateCommandCaptureSerialization(bool revisedOrders, bool nativeRules, bool commandAuthority) const
{
	heroCommands::validateRevisedRulesSerialization(heroCommandRules, revisedOrders);
	newHorizonsWarfare::validateRulesSerialization(heroCommandRules, commandAuthority);
	newHorizonsCommand::validateRulesSerialization(heroCommandRules, nativeRules);
	newHorizonsElementalAttunement::validateSpellMetadataSerialization(magicRules, nativeRules);
	newHorizonsMagic::validateMagicArrowChargeSerialization(magicRules, nativeRules);
	heroCommands::validateRules(heroCommandRules);
}

void CampaignState::validateCommandPools(const CampaignPoolType & scenarios, const GlobalPoolType & global, bool supported)
{
	for(const auto & [scenario, pool] : scenarios)
		for(const auto & hero : pool)
			newHorizonsHeroes::validateCommandCrossover(hero, supported);
	for(const auto & [id, hero] : global)
		newHorizonsHeroes::validateCommandCrossover(hero, supported);
}

void CampaignState::validateCommandSerialization(bool supported) const
{
	validateCommandPools(scenarioHeroPool, globalHeroPool, supported);
}

void StartInfo::validateCommandSerialization(bool supported) const
{
	if(campState)
		campState->validateCommandSerialization(supported);
}

void CMap::validateCommandSerialization(bool supported) const
{
	gameSettings->validateCommandSerialization(supported);
	for(const auto & hero : heroesPool)
		if(hero)
			hero->validateCommandSerialization(supported);
	for(const auto & object : objects)
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(object.get()))
			hero->validateCommandSerialization(supported);
}

void CGameState::validateCommandSerialization(bool supported) const
{
	newHorizonsHeroes::validateCommandCaptures(heroCapabilityRules, heroPerkRules, heroCommandRules, supported);
	if(map)
		map->validateCommandSerialization(supported);
	if(scenarioOps)
		scenarioOps->validateCommandSerialization(supported);
	if(initialOpts && initialOpts != scenarioOps)
		initialOpts->validateCommandSerialization(supported);
}
