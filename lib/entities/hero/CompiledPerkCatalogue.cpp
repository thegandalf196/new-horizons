/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CompiledPerkCatalogue.h"
#include "NewHorizonsPerkRules.h"
#include "NewHorizonsRuleValidation.h"
#include "../../GameLibrary.h"
#include "../../CSkillHandler.h"
#include <limits>
#include <stdexcept>

namespace newHorizonsHeroes
{
namespace
{
constexpr std::array<std::pair<std::string_view, std::string_view>, static_cast<size_t>(BuiltinCombatPerk::Count)> combatIdentities{{
	{"new-horizons:offense", "new-horizons:offense.executioner"},
	{"new-horizons:offense", "new-horizons:offense.armorPiercer"},
	{"new-horizons:offense", "new-horizons:offense.breakthrough"},
	{"new-horizons:offense", "new-horizons:offense.shockAssault"},
	{"new-horizons:luck", "new-horizons:luck.luckyAim"},
	{"new-horizons:sylvanLuck", "new-horizons:sylvanLuck.elvenPrecision"}
}};
}

CompiledPerkCatalogue::CompiledPerkCatalogue(const ValidatedPerkRules & admitted)
{
	// Empty standalone codec values are also valid before content admission.
	if(!LIBRARY || !LIBRARY->skillh)
	{
		if(admitted.active)
			throw std::runtime_error("Compiled perk catalogue requires admitted Skill identifiers");
		return;
	}
	// Rank queries are independent of perk ownership and remain valid for a
	// historical capture with no perk rows. Compile all canonical Skill IDs once.
	for(const auto & definition : LIBRARY->skillh->objects)
		if(definition)
			skills.emplace(definition->getJsonKey(), definition->getId());
	if(!admitted.active)
		return;
	for(const auto & [skillId, row] : admitted.rules["skills"].Struct())
	{
		const auto decoded = skills.find(skillId);
		// Preserve the optional query API's exact canonical identity requirement.
		// Historical registry admission can accept legacy aliases; such aliases
		// remain inactive rather than acquiring a newly interpreted Skill identity.
		const bool canonical = decoded != skills.end();
		const SecondarySkill resolved(canonical ? decoded->second : SecondarySkill(detail::resolve(SecondarySkill::entityType(), skillId)));
		auto & perkIndices = indices[skillId];
		for(const auto & perk : row["perks"].Vector())
		{
			const auto parsed = admitted.definition(skillId, perk["id"].String());
			if(!parsed || definitions.size() >= std::numeric_limits<uint32_t>::max())
				throw std::runtime_error("Invalid compiled New Horizons perk definition");
			const auto index = static_cast<uint32_t>(definitions.size());
			perkIndices.emplace(parsed->id, index);
			definitions.push_back({index, resolved, perkRequiredRank(parsed->requiredRank),
				canonical && parsed->effect["status"].String() == "active"});
		}
	}
	for(size_t index = 0; index < combatIdentities.size(); ++index)
		combatPerks[index] = find(combatIdentities[index].first, combatIdentities[index].second);
}

PerkId CompiledPerkCatalogue::find(std::string_view skill, std::string_view perk) const
{
	const auto parent = indices.find(skill);
	if(parent == indices.end())
		return {};
	const auto found = parent->second.find(perk);
	return found == parent->second.end() ? PerkId{} : PerkId(identity, found->second);
}

const PerkId & CompiledPerkCatalogue::find(BuiltinCombatPerk perk) const
{
	static const PerkId invalid;
	const auto index = static_cast<size_t>(perk);
	return index < combatPerks.size() ? combatPerks[index] : invalid;
}

std::optional<SecondarySkill> CompiledPerkCatalogue::skill(std::string_view skill) const
{
	const auto found = skills.find(skill);
	return found == skills.end() ? std::nullopt : std::optional(found->second);
}

const CompiledPerkDefinition * CompiledPerkCatalogue::definition(const PerkId & id) const
{
	if(id.identity != identity || id.index >= definitions.size())
		return nullptr;
	return &definitions[id.index];
}
}
