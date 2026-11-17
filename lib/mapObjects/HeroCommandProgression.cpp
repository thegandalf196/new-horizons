/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../network/InitializationProfile.h"
#include "CGHeroInstance.h"
#include "../GameLibrary.h"
#include "../callback/IGameInfoCallback.h"
#include "../IGameSettings.h"
#include "../callback/IGameRandomizer.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include "../entities/hero/NewHorizonsCommandArmy.h"
#include "../entities/hero/NewHorizonsCommandAdmission.h"
#include "../entities/hero/NewHorizonsFourOfferProgression.h"
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
	nhLoadProfile::Scope measured(nhLoadProfile::Metric::HeroCapture);
	const bool creationInitialization = !isFake && !primaryGrowthCaptured;
	auto * pool = cb ? cb->getCapturedRulePoolForAdmission() : nullptr;
	if(creationInitialization)
		protectedAdventureFlightLayer = EPathfindingLayer::LAND;
	if(!isFake && !masteryRulesCaptured)
	{
		if(pool)
		{
			const auto id = pool->intern(nativeRules::CapturedRuleKind::Mastery, cb->getHeroMasteryRules());
			masteryState.rules.bind(pool->document(id, nativeRules::CapturedRuleKind::Mastery));
		}
		else masteryState.rules = cb->getHeroMasteryRules();
		masteryState.validate();
		masteryRulesCaptured = true;
	}
	if(!isFake && !perkRulesCaptured)
	{
		newHorizonsHeroes::PerkState candidate;
		if(pool)
		{
			const auto id = pool->intern(nativeRules::CapturedRuleKind::Perks, cb->getHeroPerkRules());
			candidate.bindCapturedRules(pool->document(id, nativeRules::CapturedRuleKind::Perks)->perks());
		}
		else candidate.replaceRules(cb->getHeroPerkRules());
		auto prepared = candidate.prepareNativeRules();
		perkState = std::move(candidate);
		nhLoadProfile::jsonBytes(nhLoadProfile::Metric::CapturedPerkBytes, perkState.getRules());
		preparedPilotPerks = std::move(prepared);
		perkRulesCaptured = true;
	}
	if(!isFake && !capabilityRulesCaptured)
	{
		if(pool)
		{
			const auto world = pool->intern(nativeRules::CapturedRuleKind::WorldCapability, cb->getHeroCapabilityRules());
			capabilityRules.bind(pool->captureCapability(world, getHeroClass()->getId()));
		}
		else capabilityRules = newHorizonsHeroes::resolveCapabilityRules(cb->getHeroCapabilityRules(), getHeroClass()->getId());
		capabilityRulesCaptured = true;
		nodeHasChanged();
	}
	if(!preparedPilotPerks) preparedPilotPerks = perkState.prepareNativeRules();
	if(creationInitialization)
	{
		if(pool)
		{
			const auto world = pool->intern(nativeRules::CapturedRuleKind::WorldDevelopment, cb->getHeroDevelopmentRules());
			primaryGrowthRules.bind(pool->captureDevelopment(world, getHeroClass()->getId()));
		}
		else primaryGrowthRules = newHorizonsHeroes::resolveHeroRules(cb->getHeroDevelopmentRules(), getHeroClass()->getId());
		auto prepared = compiledDevelopment()->preparedCreature(getHeroTypeID());
		nhLoadProfile::jsonBytes(nhLoadProfile::Metric::CapturedDevelopmentBytes, primaryGrowthRules);
		preparedCreatureMechanics = std::move(prepared);
		primaryGrowthCaptured = true;
		nodeHasChanged();
	}
	return creationInitialization;
}

int CGHeroInstance::getPerkSkillRank(const std::string & skillId) const
{
	if(const auto * catalogue = perkState.compiledCatalogue())
	{
		const auto skill = catalogue->skill(skillId);
		return skill ? getSecSkillLevel(*skill) : 0;
	}
	// Uncaptured/fake legacy contexts retain generic canonical rank queries.
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

newHorizonsHeroes::PerkId CGHeroInstance::getPerkId(std::string_view skill, std::string_view perk) const
{
	const auto * catalogue = perkState.compiledCatalogue();
	return catalogue ? catalogue->find(skill, perk) : newHorizonsHeroes::PerkId{};
}

bool CGHeroInstance::hasActivePerk(const newHorizonsHeroes::PerkId & perk) const
{
	const auto * definition = perkState.selectedDefinition(perk);
	return definition && definition->active && getSecSkillLevel(definition->skill) >= definition->requiredRank;
}

bool CGHeroInstance::hasActivePerk(newHorizonsHeroes::BuiltinCombatPerk perk) const
{
	const auto * catalogue = perkState.compiledCatalogue();
	return catalogue && hasActivePerk(catalogue->find(perk));
}

bool CGHeroInstance::hasActivePerk(const std::string & skill, const std::string & perk) const
{
	return hasActivePerk(getPerkId(skill, perk));
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
	validateCommandSerialization(true, true, true, true);
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

void CGHeroInstance::validateCommandSerialization(bool supported, bool leadershipSupported, bool fourOfferSupported, bool estatesSupported) const
{
	newHorizonsEstates::validateHoarderReceipt(newHorizonsHoarderLastWeek, estatesSupported);
	if(commandLevelEventPending)
		throw std::runtime_error("Cannot save an unfinished ordinary level choice");
	if(!(supported && leadershipSupported && fourOfferSupported && estatesSupported))
	{
		newHorizonsHeroes::validateLeadershipEfficiencySerialization(capabilityRules, leadershipSupported);
		newHorizonsHeroes::validateUnifiedSkillProgressionSerialization(perkState.getRules(), leadershipSupported);
		newHorizonsNecromancy::validateLeadershipPolicySerialization(perkState.getRules(), leadershipSupported);
		newHorizonsHeroes::validateCommandPerkSerialization(perkState.getRules(), supported, leadershipSupported, fourOfferSupported, estatesSupported);
		newHorizonsHeroes::validateArmyCommandSerialization(capabilityRules, supported);
	}
	const auto rules = getArmyCommandRules();
	commandState.validateSerialization(rules, supported);
	validateCommandHeroHistory(commandState, rules, level,
		rules && !commandState.empty() ? getPerkSkillRank("new-horizons:authority") : 0);
}

void CGHeroInstance::levelUpAutomatically(IGameRandomizer & gameRandomizer)
{
	const bool unified = newHorizonsHeroes::usesUnifiedSkillProgression(perkState.getRules());
	// Owned heroes finish their initial ranks and any authored XP through the
	// authoritative map-ready/turn-start transaction, including human choices.
	if(newHorizonsHeroes::usesPersistentHeroProgression(perkState.getRules()) && getOwner().isValidPlayer()) return;
	const auto completePerks = [this, unified]()
	{
		if(!unified) return;
		for(size_t count = 0; count < newHorizonsHeroes::PERK_CANONICAL_SKILL_COUNT * newHorizonsHeroes::PERK_MAX_PERKS_PER_SKILL; ++count)
		{
			const auto offer = perkState.nextRequiredPerkOffer([this](const std::string & skill) { return getPerkSkillRank(skill); });
			if(offer.empty()) return;
			applyPerkSelection(offer.front().selection);
		}
		throw std::runtime_error("Non-interactive hero perk completion exceeded the registry");
	};
	completePerks();
	while(gainsLevel())
	{
		auto gains = gameRandomizer.rollPrimarySkillsForLevelup(this);
		const bool four = newHorizonsHeroes::usesFourOfferProgression(perkState.getRules());
		const auto fourOffers = four ? gameRandomizer.rollFourOfferProgression(this) : newHorizonsHeroes::FourOfferProgressionOffer{};
		const auto proposedSecondarySkills = four ? std::vector<SecondarySkill>{} : gameRandomizer.rollSecondarySkills(this);
		captureMasteryEligibility(level + 1);
		for(int i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
		{
			const auto before = getBasePrimarySkillValue(PrimarySkill(i));
			if(gains[i])
				setPrimarySkill(PrimarySkill(i), gains[i], ChangeValueMode::RELATIVE);
			gains[i] = getBasePrimarySkillValue(PrimarySkill(i)) - before;
		}
		if(four && !fourOffers.empty())
		{
			const auto legal = newHorizonsHeroes::legalFourOfferReplies(fourOffers, perkState,
				newHorizonsHeroes::fourOfferCandidates(*cb, *this));
			const auto choice = legal.empty() ? newHorizonsHeroes::FourOfferProgressionSelection{SecondarySkill::NONE, std::nullopt, true}
				: *newHorizonsHeroes::fourOfferSelection(fourOffers, perkState, legal.front());
			if(choice.skill != SecondarySkill::NONE)
				setSecSkillLevel(choice.skill, 1, ChangeValueMode::RELATIVE);
			else if(choice.perk) applyPerkSelection(choice.perk->selection);
		}
		else if(!proposedSecondarySkills.empty())
			setSecSkillLevel(proposedSecondarySkills.front(), 1, ChangeValueMode::RELATIVE);
		levelUp(gains);
		completePerks();
		captureCommandProgression();
	}
}
