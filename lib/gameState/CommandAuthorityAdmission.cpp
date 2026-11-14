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

void CampaignState::validateCommandPools(const CampaignPoolType & scenarios, const GlobalPoolType & global,
	bool supported, bool leadershipSupported)
{
	for(const auto & [scenario, pool] : scenarios)
		for(const auto & hero : pool)
			newHorizonsHeroes::validateCommandCrossover(hero, supported, leadershipSupported);
	for(const auto & [id, hero] : global)
		newHorizonsHeroes::validateCommandCrossover(hero, supported, leadershipSupported);
}

void CampaignState::validateCommandSerialization(bool supported, bool leadershipSupported) const
{
	validateCommandPools(scenarioHeroPool, globalHeroPool, supported, leadershipSupported);
}

void StartInfo::validateCommandSerialization(bool supported, bool leadershipSupported) const
{
	if(campState)
		campState->validateCommandSerialization(supported, leadershipSupported);
}

void CMap::validateCommandSerialization(bool supported, bool leadershipSupported) const
{
	gameSettings->validateCommandSerialization(supported, leadershipSupported);
	for(const auto & hero : heroesPool)
		if(hero)
			hero->validateCommandSerialization(supported, leadershipSupported);
	for(const auto & object : objects)
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(object.get()))
			hero->validateCommandSerialization(supported, leadershipSupported);
}

void CGameState::validateCommandSerialization(bool supported, bool leadershipSupported) const
{
	newHorizonsHeroes::validateCommandCaptures(heroCapabilityRules, heroPerkRules, heroCommandRules, supported, leadershipSupported);
	if(map)
		map->validateCommandSerialization(supported, leadershipSupported);
	if(scenarioOps)
		scenarioOps->validateCommandSerialization(supported, leadershipSupported);
	if(initialOpts && initialOpts != scenarioOps)
		initialOpts->validateCommandSerialization(supported, leadershipSupported);
}
