/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPerkState.h"
#include "../../nativeRules/CapturedPerkRules.h"
#include <stdexcept>

namespace newHorizonsHeroes
{
const JsonNode & PerkState::getRules() const
{
	static const JsonNode empty;
	return capturedRules ? capturedRules->document() : empty;
}

const CompiledPerkCatalogue * PerkState::compiledCatalogue() const
{
	return capturedRules ? &capturedRules->catalogue() : nullptr;
}

std::vector<uint64_t> PerkState::projectSelections(const std::vector<PerkSelection> & selections) const
{
	const auto * catalogue = compiledCatalogue();
	std::vector<uint64_t> result(catalogue ? (catalogue->size() + 63) / 64 : 0);
	for(const auto & selection : selections)
	{
		const auto id = catalogue ? catalogue->find(selection.skillId, selection.perkId) : PerkId{};
		const auto * definition = catalogue ? catalogue->definition(id) : nullptr;
		if(!definition)
			throw std::runtime_error("Selected perk is absent from captured compiled catalogue");
		result[definition->denseIndex / 64] |= uint64_t{1} << (definition->denseIndex % 64);
	}
	return result;
}

void PerkState::bindCapturedRules(std::shared_ptr<const nativeRules::CapturedPerkRules> rules)
{
	// Build the complete replacement before publication, including validation
	// against the existing acquired selections. Failed admission leaves us intact.
	PerkState candidate;
	candidate.capturedRules = std::move(rules);
	candidate.selected = selected;
	candidate.validateSelections();
	candidate.acquiredPerks = candidate.projectSelections(candidate.selected);
	*this = std::move(candidate);
}

void PerkState::replaceRules(JsonNode rules)
{
	bindCapturedRules(nativeRules::CapturedPerkRules::compile(std::move(rules)));
}

void PerkState::replaceSelected(std::vector<PerkSelection> selections)
{
	validateSelections(selections);
	auto projection = projectSelections(selections);
	selected = std::move(selections);
	acquiredPerks = std::move(projection);
}

void PerkState::rebuildProjection()
{
	acquiredPerks = projectSelections(selected);
}

bool PerkState::hasSelection(const PerkId & id) const
{
	return selectedDefinition(id) != nullptr;
}

bool PerkState::hasSelection(const std::string & skill, const std::string & perk) const
{
	const auto * catalogue = compiledCatalogue();
	return catalogue && hasSelection(catalogue->find(skill, perk));
}

const CompiledPerkDefinition * PerkState::selectedDefinition(const PerkId & id) const
{
	const auto * catalogue = compiledCatalogue();
	const auto * definition = catalogue ? catalogue->definition(id) : nullptr;
	if(!definition || definition->denseIndex / 64 >= acquiredPerks.size()
		|| !(acquiredPerks[definition->denseIndex / 64] & (uint64_t{1} << (definition->denseIndex % 64))))
		return nullptr;
	return definition;
}
}
