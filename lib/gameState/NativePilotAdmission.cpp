/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGameState.h"
#include "CGameStateCampaign.h"
#include "../StartInfo.h"
#include "../mapping/CMap.h"
#include "../campaign/CampaignState.h"
#include "../nativeRules/PreparedPilotRules.h"
void CGameState::validateNativePilotSerialization(bool supported) const
{
	// Current perk rows are admitted by the existing weighted-rule preflight.
	if(!supported)
		nativeRules::validatePilotPerks(heroPerkRules, false);
	nativeRules::validatePilotCreatures(heroDevelopmentRules, supported);
	if(map) map->validateNativePilotSerialization(supported);
	const auto * scenarioCampaign = scenarioOps ? scenarioOps->campState.get() : nullptr;
	const auto * initialCampaign = initialOpts ? initialOpts->campState.get() : nullptr;
	if(scenarioCampaign) scenarioCampaign->validateNativePilotSerialization(supported);
	if(initialCampaign && initialCampaign != scenarioCampaign)
		initialCampaign->validateNativePilotSerialization(supported);
}
