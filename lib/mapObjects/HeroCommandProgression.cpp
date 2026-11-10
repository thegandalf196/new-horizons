/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../GameLibrary.h"
#include "../callback/IGameInfoCallback.h"
#include "../IGameSettings.h"
#include "../callback/IGameRandomizer.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include "../entities/hero/NewHorizonsCommandArmy.h"
#include "../entities/hero/NewHorizonsCommandAdmission.h"
#include "../entities/hero/CHeroClass.h"
#include <stdexcept>

namespace
{
void validateCommandHeroHistory(const newHorizonsHeroes::CommandState & state,
	const std::optional<newHorizonsHeroes::CommandRules> & rules, uint32_t level, int authorityRank)
{
	state.validate(rules);
	if(rules && !state.empty() && (state.getLastCaptureLevel() != level
		|| state.getHighestCommand() < newHorizonsHeroes::commandValue(*rules, level, authorityRank).effective()))
		throw std::runtime_error("Command history does not describe this hero's completed advancement");
}
}

bool CGHeroInstance::captureInitialDevelopmentRules(bool isFake)
{
	const bool creationInitialization = !isFake && !primaryGrowthCaptured;
	if(creationInitialization)
		protectedAdventureFlightLayer = EPathfindingLayer::LAND;
	if(!isFake && !masteryRulesCaptured)
	{
		masteryState.rules = cb->getHeroMasteryRules();
		masteryState.validate();
		masteryRulesCaptured = true;
	}
	if(!isFake && !perkRulesCaptured)
	{
		newHorizonsHeroes::PerkState candidate;
		candidate.rules = cb->getHeroPerkRules();
		auto prepared = candidate.prepareNativeRules();
		perkState = std::move(candidate);
		preparedPilotPerks = std::move(prepared);
		perkRulesCaptured = true;
	}
	if(!isFake && !capabilityRulesCaptured)
	{
		capabilityRules = newHorizonsHeroes::resolveCapabilityRules(cb->getHeroCapabilityRules(), getHeroClass()->getId());
		capabilityRulesCaptured = true;
		nodeHasChanged();
	}
	if(!preparedPilotPerks) preparedPilotPerks = perkState.prepareNativeRules();
	if(creationInitialization)
	{
		auto candidate = newHorizonsHeroes::resolveHeroRules(cb->getHeroDevelopmentRules(), getHeroClass()->getId());
		auto prepared = nativeRules::preparePilotCreature(candidate, getHeroTypeID());
		primaryGrowthRules = std::move(candidate);
		preparedCreatureMechanics = std::move(prepared);
		primaryGrowthCaptured = true;
		nodeHasChanged();
	}
	return creationInitialization;
}

int CGHeroInstance::getPerkSkillRank(const std::string & skillId) const
{
	const auto decoded = LIBRARY->identifiers()->getIdentifier(
		ModScope::scopeGame(), SecondarySkill::entityType(), skillId, true);
	if(!decoded)
		return 0;
	const SecondarySkill skill(*decoded);
	// Optional perk queries require an exact canonical identity, never a legacy
	// unscoped alias. Required captured registry validation remains separate.
	if(SecondarySkill::encode(*decoded) != skillId)
		return 0;
	return getSecSkillLevel(skill);
}

bool CGHeroInstance::hasActivePerk(const std::string & skillId, const std::string & perkId) const
{
	if(!perkState.hasSelection(skillId, perkId))
		return false;

	// This private saved state is validated on initialization, selection and load.
	// Combat estimates query individual perks thousands of times: do not project
	// every selection (and revalidate the entire registry) for each damage estimate.
	// Read the current rank rather than caching enabled flags across skill changes.
	for(const auto & perk : perkState.rules["skills"][skillId]["perks"].Vector())
	{
		if(perk["id"].String() == perkId)
			return perk["effect"]["status"].String() == "active"
				&& getPerkSkillRank(skillId) >= newHorizonsHeroes::perkRequiredRank(perk["requires"].String());
	}
	return false;
}

std::optional<newHorizonsHeroes::CommandRules> CGHeroInstance::getArmyCommandRules() const
{
	return newHorizonsHeroes::capturedCommandRules(capabilityRules);
}

const JsonNode & CGHeroInstance::getHeroOrderRules() const
{
	if(!cb)
		throw std::runtime_error("Hero Order rules require an owning world");
	return cb->getSettings().getValue(EGameSettings::COMBAT_HERO_COMMANDS);
}

std::optional<newHorizonsHeroes::CommandValue> CGHeroInstance::getArmyCommand() const
{
	const auto rules = getArmyCommandRules();
	return rules ? std::optional(newHorizonsHeroes::commandValue(*rules, level,
		getPerkSkillRank("new-horizons:authority"))) : std::nullopt;
}

newHorizonsHeroes::CommandPlacementError CGHeroInstance::getCommandSlotEligibility(
	SlotID slot, CreatureID creature, bool) const
{
	// Old callers may carry a preview Boolean; never trust it as creature authority.
	return getCommandSlotEligibility(slot, creature);
}

std::optional<newHorizonsHeroes::CommandSlotModifiers> CGHeroInstance::getCommandSlotModifiers(SlotID slot) const
{
	const auto rules = getArmyCommandRules();
	if(!rules)
		return std::nullopt;
	std::array<bool, 10> active{};
	for(size_t i = 0; i < active.size(); ++i)
		active[i] = hasActivePerk("new-horizons:authority", "new-horizons:authority." + std::string(newHorizonsHeroes::AUTHORITY_PERK_KEYS[i]));
	const auto command = newHorizonsHeroes::commandValue(*rules, level, getPerkSkillRank("new-horizons:authority"));
	return commandState.modifiers(*rules, command.effective(), slot, active);
}

size_t CGHeroInstance::captureCommandProgression()
{
	if(commandLevelEventPending)
		throw std::runtime_error("Cannot capture Command before the ordinary level choice");
	const auto rules = getArmyCommandRules();
	if(!rules)
		return 0;
	const auto count = commandState.capture(*rules, level, getPerkSkillRank("new-horizons:authority"));
	nodeHasChanged();
	return count;
}

void CGHeroInstance::initializeCommandProgression()
{
	const auto rules = getArmyCommandRules();
	if(!rules || !commandState.empty())
		return;
	// Fresh authored XP can have already established the level in JSON loading.
	// Creation has a fixed starting Skill roster, but retains every individual level.
	auto candidate = commandState;
	for(uint32_t acquiredLevel = 1; acquiredLevel < level; ++acquiredLevel)
		candidate.capture(*rules, acquiredLevel, getPerkSkillRank("new-horizons:authority"));
	candidate.capture(*rules, level, getPerkSkillRank("new-horizons:authority"));
	commandState = std::move(candidate);
	nodeHasChanged();
}

void CGHeroInstance::normalizeStartingCommandArmy()
{
	const auto command = getArmyCommand();
	if(!command)
		return;
	std::vector<newHorizonsHeroes::CommandPlacementStack> inputs;
	for(const auto & [slot, stack] : stacks)
		inputs.push_back({stack->getCreatureID(), stack->getCount(),
			newHorizonsHeroes::commandArmyCreatureTraits(*this, stack->getCreatureID()), slot});
	const auto plan = newHorizonsHeroes::planCommandPlacement(command->effective(), inputs);
	if(!plan)
		throw std::runtime_error("Authored starting army has no legal Command arrangement");
	// Allocate the complete replacement before moving ownership; preserve exact stack objects.
	TSlots arranged;
	for(const auto & row : *plan)
		arranged.emplace(row.slot, nullptr);
	for(const auto & row : *plan)
		arranged.at(row.slot) = std::move(stacks.at(inputs.at(row.inputIndex).preferredSlot));
	stacks.swap(arranged);
	armyChanged();
}

void CGHeroInstance::beginCommandLevelEvent()
{
	if(!getArmyCommandRules())
		return;
	if(commandLevelEventPending)
		throw std::runtime_error("Previous Command level choice is unfinished");
	commandLevelEventPending = true;
}

void CGHeroInstance::completeCommandLevelEvent(uint32_t acquiredLevel, int authorityRank)
{
	const auto rules = getArmyCommandRules();
	if(!rules || !commandLevelEventPending || acquiredLevel != level
		|| authorityRank != getPerkSkillRank("new-horizons:authority"))
		throw std::runtime_error("Invalid coherent Command level event");
	auto candidate = commandState;
	candidate.capture(*rules, acquiredLevel, authorityRank);
	commandState = std::move(candidate);
	commandLevelEventPending = false;
	nodeHasChanged();
}

void CGHeroInstance::selectCommandEnhancement(int64_t milestone, newHorizonsHeroes::CommandEnhancement enhancement)
{
	const auto rules = getArmyCommandRules();
	if(!rules)
		throw std::runtime_error("Command enhancement selection without captured army Command rules");
	commandState.select(*rules, milestone, enhancement);
	nodeHasChanged();
}

JsonNode CGHeroInstance::commandProgressionToJson() const
{
	validateCommandSerialization(true);
	return commandState.toJson(getArmyCommandRules());
}

void CGHeroInstance::restoreCommandProgression(const JsonNode & node)
{
	const auto rules = getArmyCommandRules();
	auto candidate = newHorizonsHeroes::CommandState::fromJson(node, rules);
	validateCommandHeroHistory(candidate, rules, level,
		rules && !candidate.empty() ? getPerkSkillRank("new-horizons:authority") : 0);
	commandState = std::move(candidate);
	commandLevelEventPending = false;
	nodeHasChanged();
}

void CGHeroInstance::validateCommandSerialization(bool supported) const
{
	if(commandLevelEventPending)
		throw std::runtime_error("Cannot save an unfinished ordinary level choice");
	newHorizonsHeroes::validateCommandPerkSerialization(perkState.rules, supported);
	newHorizonsHeroes::validateArmyCommandSerialization(capabilityRules, supported);
	const auto rules = getArmyCommandRules();
	commandState.validateSerialization(rules, supported);
	validateCommandHeroHistory(commandState, rules, level,
		rules && !commandState.empty() ? getPerkSkillRank("new-horizons:authority") : 0);
}

void CGHeroInstance::levelUpAutomatically(IGameRandomizer & gameRandomizer)
{
	while(gainsLevel())
	{
		auto gains = gameRandomizer.rollPrimarySkillsForLevelup(this);
		const auto proposedSecondarySkills = gameRandomizer.rollSecondarySkills(this);
		captureMasteryEligibility(level + 1);
		for(int i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
		{
			const auto before = getBasePrimarySkillValue(PrimarySkill(i));
			if(gains[i])
				setPrimarySkill(PrimarySkill(i), gains[i], ChangeValueMode::RELATIVE);
			gains[i] = getBasePrimarySkillValue(PrimarySkill(i)) - before;
		}
		if(!proposedSecondarySkills.empty())
			setSecSkillLevel(proposedSecondarySkills.front(), 1, ChangeValueMode::RELATIVE);
		levelUp(gains);
		captureCommandProgression();
	}
}
