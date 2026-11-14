/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsLeadership.h"
#include "../creature/NewHorizonsCreatureCategoryRules.h"
#include "../../CCreatureHandler.h"
#include "../../mapObjects/CGHeroInstance.h"
#include <set>

namespace newHorizonsHeroes
{
LeadershipComposition captureLeadershipComposition(const CGHeroInstance & hero,
	const CCreatureSet * armyContext)
{
	const auto & rules = hero.getCapabilityRules();
	if(!usesLeadershipEfficiency(rules)) return {};
	LeadershipComposition result;
	result.captured = true;
	const auto * army = armyContext ? armyContext : hero.getArmy();
	if(!army) return result;
	std::set<std::string> lines;
	bool repeated = false, core = false, elite = false, champion = false;
	const auto & capturedLines = rules["leadership"]["resolvedCreatureLines"].Struct();
	for(const auto & [slot, stack] : army->Slots())
	{
		if(!stack || stack->getCount() <= 0 || !stack->getCreatureID().hasValue()) continue;
		const auto * creature = stack->getCreatureID().toCreature();
		if(!creature) continue;
		const auto found = capturedLines.find(creature->getJsonKey());
		const auto line = found != capturedLines.end() ? found->second.String() : creature->getJsonKey();
		repeated |= !lines.insert(line).second;
		const auto traits = hero.getCommandCreatureTraits(stack->getCreatureID());
		if(!traits.category) continue;
		core |= *traits.category == newHorizonsCreatures::CreatureCategory::CORE;
		elite |= *traits.category == newHorizonsCreatures::CreatureCategory::ELITE;
		champion |= *traits.category == newHorizonsCreatures::CreatureCategory::CHAMPION;
	}
	result.regimentalOrganization = !repeated && lines.size() >= 3;
	result.combinedArms = core && elite && champion;
	return result;
}
}
