/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsNecromancy.h"
#include "NewHorizonsReclaimer.h"
#include <algorithm>
namespace newHorizonsNecromancy
{
bool NecromancyResult::isReclaimerSummaryValid() const
{
	if(reclaimerZombiesRaised < 0)
		return false;
	if(reclaimerZombiesRaised == 0)
		return true; // Historical summaries keep their existing validation contract.
	if(reclaimerZombiesRaised > zombiesRaised)
		return false;
	const int64_t equivalents = static_cast<int64_t>(skeletonsRaised) + 3LL * zombiesRaised
		+ 6LL * wightsRaised + 12LL * boneDragonsRaised;
	return active && applied && !blockedByArmyCapacity && skeletonsRaised >= 0
		&& wightsRaised >= 0 && boneDragonsRaised >= 0 && equivalents == skeletonsOffered
		&& darkConversionChosen == (zombiesRaised > reclaimerZombiesRaised);
}
void blockRaisedResult(NecromancyResult & result)
{
	result.applied = false;
	result.blockedByArmyCapacity = true;
	result.skeletonsRaised = result.zombiesRaised = result.wightsRaised = 0;
	result.reclaimerZombiesRaised = 0;
	result.lordOfDeadSkeletonsConsumed = result.boneDragonsRaised = 0;
	result.skeletonCreature = result.raisedCreature = CreatureID::NONE;
	result.darkConversionChosen = false;
	result.manaRecovered = 0;
	result.ossuaryTown = ObjectInstanceID::NONE;
}
void finalizeRaisedResult(NecromancyResult & result, int32_t currentMana, int32_t manaLimit)
{
	result.darkConversionChosen = result.zombiesRaised > result.reclaimerZombiesRaised;
	if(result.skeletonsRaised <= 0)
		result.skeletonCreature = CreatureID::NONE;
	result.raisedCreature = CreatureID::NONE;
	result.manaRecovered = 0;
	const int outputKinds = (result.skeletonsRaised > 0) + (result.zombiesRaised > 0)
		+ (result.wightsRaised > 0) + (result.boneDragonsRaised > 0);
	if(outputKinds == 1)
	{
		if(result.zombiesRaised > 0)
			result.raisedCreature = CreatureID(CreatureID::decode("core:zombie"));
		else if(result.wightsRaised > 0)
			result.raisedCreature = CreatureID(CreatureID::decode("core:wight"));
		else if(result.boneDragonsRaised > 0)
			result.raisedCreature = CreatureID(CreatureID::decode("core:boneDragon"));
		else
			result.raisedCreature = result.skeletonCreature.hasValue() ? result.skeletonCreature
				: CreatureID(CreatureID::decode("core:skeleton"));
	}
	const int64_t total = static_cast<int64_t>(result.skeletonsRaised) + result.zombiesRaised
		+ result.wightsRaised + result.boneDragonsRaised;
	if(total >= 10)
		result.manaRecovered = static_cast<int32_t>(std::min<int64_t>({10, total / 10,
			std::max<int64_t>(0, static_cast<int64_t>(manaLimit) - currentMana)}));
}
NecromancyResult reclaimResidualSkeletons(const NecromancyResult & ordinary,
	int conversionPercent, int32_t currentMana, int32_t manaLimit)
{
	if(conversionPercent < 0 || conversionPercent > 100 || ordinary.reclaimerZombiesRaised != 0)
		throw std::invalid_argument("Invalid or repeated residual Necromancy conversion");
	if(!ordinary.active || !ordinary.applied || ordinary.blockedByArmyCapacity || ordinary.skeletonsRaised <= 0)
		return ordinary;
	const int64_t equivalents = static_cast<int64_t>(ordinary.skeletonsRaised) + 3LL * ordinary.zombiesRaised
		+ 6LL * ordinary.wightsRaised + 12LL * ordinary.boneDragonsRaised;
	if(ordinary.zombiesRaised < 0 || ordinary.wightsRaised < 0 || ordinary.boneDragonsRaised < 0
		|| equivalents != ordinary.skeletonsOffered)
		throw std::invalid_argument("Residual conversion requires a complete ordinary Necromancy batch");
	const auto converted = static_cast<int32_t>(static_cast<int64_t>(ordinary.skeletonsRaised) * conversionPercent / 100 / 3);
	if(converted == 0)
		return ordinary;
	auto result = ordinary;
	result.skeletonsRaised -= converted * 3;
	result.zombiesRaised += converted;
	result.reclaimerZombiesRaised = converted;
	finalizeRaisedResult(result, currentMana, manaLimit);
	if(!result.isReclaimerSummaryValid())
		throw std::runtime_error("Invalid residual Necromancy result");
	return result;
}
}
