/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGameState.h"
#include "../StartInfo.h"
#include "../GameSettings.h"
#include "../mapping/CMap.h"
#include "../campaign/CampaignState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/MiscObjects.h"
#include "../CPlayerState.h"
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
	bool supported, bool leadershipSupported, bool fourOfferSupported, bool estatesSupported)
{
	for(const auto & [scenario, pool] : scenarios)
		for(const auto & hero : pool)
			newHorizonsHeroes::validateCommandCrossover(hero, supported, leadershipSupported, fourOfferSupported, estatesSupported);
	for(const auto & [id, hero] : global)
		newHorizonsHeroes::validateCommandCrossover(hero, supported, leadershipSupported, fourOfferSupported, estatesSupported);
}

void CampaignState::validateCommandSerialization(bool supported, bool leadershipSupported, bool fourOfferSupported, bool estatesSupported) const
{
	validateCommandPools(scenarioHeroPool, globalHeroPool, supported, leadershipSupported, fourOfferSupported, estatesSupported);
}

void StartInfo::validateCommandSerialization(bool supported, bool leadershipSupported, bool fourOfferSupported, bool estatesSupported) const
{
	if(campState)
		campState->validateCommandSerialization(supported, leadershipSupported, fourOfferSupported, estatesSupported);
}

void CMap::validateCommandSerialization(bool supported, bool leadershipSupported, bool fourOfferSupported, bool estatesSupported) const
{
	gameSettings->validateCommandSerialization(supported, leadershipSupported, fourOfferSupported, estatesSupported);
	for(const auto & hero : heroesPool)
		if(hero)
			hero->validateCommandSerialization(supported, leadershipSupported, fourOfferSupported, estatesSupported);
	for(const auto & object : objects)
	{
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(object.get()))
			hero->validateCommandSerialization(supported, leadershipSupported, fourOfferSupported, estatesSupported);
		if(const auto * mine = dynamic_cast<const CGMine *>(object.get()))
			mine->validateCompanyDefenseSerialization(estatesSupported);
	}
}

void CGameState::validateCommandSerialization(bool supported, bool leadershipSupported, bool fourOfferSupported, bool estatesSupported) const
{
	for(const auto & [player, state] : players)
		state.estates.validateSerialization(estatesSupported);
	newHorizonsHeroes::validateCommandCaptures(heroCapabilityRules, heroPerkRules, heroCommandRules, supported, leadershipSupported, fourOfferSupported, estatesSupported);
	if(map)
		map->validateCommandSerialization(supported, leadershipSupported, fourOfferSupported, estatesSupported);
	if(scenarioOps)
		scenarioOps->validateCommandSerialization(supported, leadershipSupported, fourOfferSupported, estatesSupported);
	if(initialOpts && initialOpts != scenarioOps)
		initialOpts->validateCommandSerialization(supported, leadershipSupported, fourOfferSupported, estatesSupported);
}
