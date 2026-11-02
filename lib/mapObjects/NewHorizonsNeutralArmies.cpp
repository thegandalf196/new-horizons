/*
 * NewHorizonsNeutralArmies.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsNeutralArmies.h"
#include "NewHorizonsNeutralCalibration.h"

#include "CGCreature.h"
#include "CGHeroInstance.h"
#include "CRewardableObject.h"
#include "Quest.h"
#include "army/CStackInstance.h"
#include "../IGameSettings.h"
#include "../CCreatureHandler.h"
#include "../gameState/CGameState.h"
#include "../callback/IGameInfoCallback.h"
#include "../json/JsonNode.h"
#include "../mapping/CMap.h"
#include "../networkPacks/PacksForLobby.h"
#include "../rewardable/Configuration.h"

#include <stdexcept>
#include <set>

namespace newHorizonsNeutralArmies
{
namespace
{
int readPercentage(const JsonNode & value)
{
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 1 || value.Integer() > 100)
		throw std::runtime_error("Neutral army size percentage must be an integer in [1,100]");
	return static_cast<int>(value.Integer());
}

std::set<ObjectInstanceID> protectedEncounters(const CMap & map)
{
	std::set<ObjectInstanceID> targets;
	std::vector<const Rewardable::Limiter *> pending;
	for(const auto * object : map.getObjects())
	{
		if(const auto * source = dynamic_cast<const QuestSource *>(object))
			for(const auto & quest : source->allQuests())
				pending.push_back(&quest->mission);
		if(const auto * rewardable = dynamic_cast<const CRewardableObject *>(object))
			for(const auto & visit : rewardable->configuration.info)
				pending.push_back(&visit.limiter);
	}
	std::set<const Rewardable::Limiter *> visited;
	while(!pending.empty())
	{
		const auto * limiter = pending.back();
		pending.pop_back();
		if(!limiter || !visited.insert(limiter).second)
			continue;
		targets.insert(limiter->destroyedObjects.begin(), limiter->destroyedObjects.end());
		for(const auto * children : {&limiter->allOf, &limiter->anyOf, &limiter->noneOf})
			for(const auto & child : *children)
				pending.push_back(child.get());
	}
	return targets;
}

bool calibratable(const CArmedInstance & army)
{
	// Authored fortifications, quests and scripted rewards retain their accepted
	// percentage policy. An ordinary wandering count alone is not a script tag.
	if(army.asQuestSource() || !army.getVisitScriptHandler().empty())
		return false;
	if(army.ID == Obj::TOWN || army.ID == Obj::GARRISON || army.ID == Obj::GARRISON2
		|| army.ID == Obj::QUEST_GUARD || army.ID == Obj::PANDORAS_BOX || army.ID == Obj::EVENT)
		return false;
	if(const auto * monster = dynamic_cast<const CGCreature *>(&army))
		return monster->message.empty() && !monster->notGrowingTeam
			&& !monster->resources.nonZero() && monster->gainedArtifact == ArtifactID::NONE
			&& monster->stacksCount == -1
			&& monster->upgradedStackPresence == CGCreature::UpgradedStackPresence::RANDOM;
	return true;
}

calibration::Rules capturedRules(const CArmedInstance & army)
{
	return calibration::Rules(army.cb->getSettings().getValue(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION));
}

void scaleConfiguration(Rewardable::Configuration & configuration, int percent,
	const calibration::Rules & rules)
{
	std::vector<CStackBasicDescriptor *> guards;
	std::vector<calibration::FormationStack> formation;
	for(auto & visit : configuration.info)
		for(auto & guard : visit.reward.guards)
		{
			guards.push_back(&guard);
			if(rules.enabled())
				formation.push_back({guard.getCount(), guard.getCreature()->getAIValue()});
		}
	// initializeGuards consumes at most seven slots. Unsupported multi-reward
	// shapes retain their original percentage rather than rewriting hidden guards.
	std::vector<TQuantity> counts;
	if(rules.enabled() && guards.size() <= 7)
		counts = rules.counts(formation);
	for(size_t i = 0; i < guards.size(); ++i)
		guards[i]->setCount(scaleCount(counts.empty() ? guards[i]->getCount() : counts[i], percent));
}

bool configurationMatchesArmy(const CRewardableObject & army)
{
	auto physical = army.Slots().begin();
	for(const auto & visit : army.configuration.info)
		for(const auto & guard : visit.reward.guards)
		{
			if(physical == army.Slots().end() || physical->second->getId() != guard.getId()
				|| physical->second->getCount() != guard.getCount())
				return false;
			++physical;
		}
	return physical == army.Slots().end();
}

void synchronizeGuardCounts(CRewardableObject & army)
{
	// Identity/count matching was checked before calibration. Update counts in
	// place so stack experience, artifacts and bonus attachments remain intact.
	auto physical = army.Slots().begin();
	for(const auto & visit : army.configuration.info)
		for(const auto & guard : visit.reward.guards)
			(physical++)->second->setCount(guard.getCount());
}

void scaleArmy(CArmedInstance & army, int percent, const calibration::Rules & rules)
{
	std::vector<CStackInstance *> stacks;
	std::vector<calibration::FormationStack> formation;
	for(const auto & [slot, stack] : army.Slots())
	{
		stacks.push_back(stack.get());
		if(rules.enabled())
			formation.push_back({stack->getCount(), stack->getCreature()->getAIValue()});
	}
	std::vector<TQuantity> counts;
	if(rules.enabled() && calibratable(army))
		counts = rules.counts(formation);
	for(size_t i = 0; i < stacks.size(); ++i)
		stacks[i]->setCount(scaleCount(counts.empty() ? stacks[i]->getCount() : counts[i], percent));
}

}

int percentage(const IGameSettings & settings)
{
	return readPercentage(settings.getValue(EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT));
}

TQuantity scaleCount(TQuantity count, int percent)
{
	if(count < 0 || percent < 1 || percent > 100)
		throw std::runtime_error("Invalid neutral army size scaling input");
	// TQuantity is signed 32-bit; the product is wide, and the result cannot grow.
	return static_cast<TQuantity>((static_cast<int64_t>(count) * percent + 99) / 100);
}

bool eligible(const CArmedInstance & army)
{
	const auto owner = army.getOwner();
	return (owner == PlayerColor::NEUTRAL || owner == PlayerColor::UNFLAGGABLE)
		&& dynamic_cast<const CGHeroInstance *>(&army) == nullptr;
}

TQuantity generatedCount(const CArmedInstance & army, TQuantity rawCount)
{
	return eligible(army) ? scaleCount(rawCount, percentage(army.cb->getSettings())) : rawCount;
}

std::vector<TQuantity> generatedCounts(const CArmedInstance & army,
	const std::vector<std::pair<CreatureID, TQuantity>> & raw)
{
	std::vector<TQuantity> result;
	const int percent = eligible(army) ? percentage(army.cb->getSettings()) : 100;
	std::vector<calibration::FormationStack> formation;
	for(const auto & [creature, count] : raw)
	{
		result.push_back(count);
		formation.push_back({count, creature.toCreature()->getAIValue()});
	}
	if(eligible(army) && calibratable(army) && formation.size() <= 7)
	{
		const auto rules = capturedRules(army);
		if(rules.enabled() && !protectedEncounters(army.cb->gameState().getMap()).contains(army.id))
			result = rules.counts(formation);
	}
	for(auto & count : result)
		count = scaleCount(count, percent);
	return result;
}

void scaleGeneratedGuards(const CArmedInstance & army, Rewardable::Configuration & raw)
{
	if(!eligible(army))
		return;
	const int percent = percentage(army.cb->getSettings());
	const auto rules = capturedRules(army);
	const bool ordinary = rules.enabled() && calibratable(army)
		&& !protectedEncounters(army.cb->gameState().getMap()).contains(army.id);
	scaleConfiguration(raw, percent, ordinary ? rules : calibration::Rules(JsonNode()));
}

void initializeWorld(CMap & map)
{
	const int percent = percentage(map.getSettings());
	map.overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_ARMY_SIZE_PERCENT, JsonNode(percent));
	const auto snapshot = calibration::capture(map.getSettings());
	map.overrideGameSetting(EGameSettings::CREATURES_NEUTRAL_LEADERSHIP_CALIBRATION, snapshot);
	const calibration::Rules rules(snapshot);
	if(percent == 100 && !rules.enabled())
		return; // Legacy identity preserves army, experience and bonus caches exactly.

	const auto protectedTargets = rules.enabled() ? protectedEncounters(map) : std::set<ObjectInstanceID>{};
	for(const auto & object : map.getObjects())
	{
		auto * army = dynamic_cast<CArmedInstance *>(object);
		if(!army || !eligible(*army))
			continue;
		const auto & appliedRules = protectedTargets.contains(army->id) ? calibration::Rules(JsonNode()) : rules;
		if(auto * rewardable = dynamic_cast<CRewardableObject *>(army))
		{
			if(appliedRules.enabled() && calibratable(*army) && configurationMatchesArmy(*rewardable))
			{
				scaleConfiguration(rewardable->configuration, percent, appliedRules);
				synchronizeGuardCounts(*rewardable); // Copy the single transformed formation.
			}
			else
			{
				const calibration::Rules legacy{JsonNode()};
				scaleArmy(*army, percent, legacy);
				scaleConfiguration(rewardable->configuration, percent, legacy);
			}
		}
		else
			scaleArmy(*army, percent, appliedRules);
		if(auto * monster = dynamic_cast<CGCreature *>(army))
			monster->temppower = static_cast<int64_t>(monster->getStackCount(SlotID(0))) * 1000;
	}
}

void validateOverrides(const JsonNode & settings, bool supported)
{
	if(!settings.isNull() && !settings.isStruct())
		throw std::runtime_error("Saved settings must be an object");
	const auto & creatures = settings["creatures"];
	if(!creatures.isNull() && !creatures.isStruct())
		throw std::runtime_error("Saved creature settings must be an object");
	if(creatures.Struct().contains(SETTING_KEY))
	{
		const int percent = readPercentage(creatures[SETTING_KEY]);
		if(!supported && percent != 100)
			throw std::runtime_error("Reduced neutral army size requires the new save format");
	}
}

void validateSerialization(const IGameSettings & settings, bool supported)
{
	if(percentage(settings) != 100 && !supported)
		throw std::runtime_error("Reduced neutral army size requires the new save format");
}
}

void CGameState::validateNeutralArmySizeSerialization(bool supported) const
{
	if(map)
		newHorizonsNeutralArmies::validateSerialization(map->getSettings(), supported);
}

void LobbyStartGame::validateNeutralArmySizeSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNeutralArmySizeSerialization(supported);
}

void CGameState::validateNeutralLeadershipCalibrationSerialization(bool supported) const
{
	if(map)
		newHorizonsNeutralArmies::calibration::validateSerialization(map->getSettings(), supported);
}

void LobbyStartGame::validateNeutralLeadershipCalibrationSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNeutralLeadershipCalibrationSerialization(supported);
}
