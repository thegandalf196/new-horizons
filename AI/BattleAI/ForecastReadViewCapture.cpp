/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "ForecastNativeReadView.h"
#include "HypotheticBattle.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/OwnedFrameCaptureFacts.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include "../../lib/IGameSettings.h"
#include <map>
#include <stdexcept>
namespace forecast
{
std::shared_ptr<const NativeReadEnvironment> NativeAdapter::captureReadEnvironment(
	const HypotheticBattle & source, std::shared_ptr<const NativeReadEnvironment> previous)
{
	auto environment = previous ? std::make_shared<NativeReadEnvironment>(*previous)
		: std::make_shared<NativeReadEnvironment>();
	if(previous && (previous->magic != source.getCapturedMagicRules()
		|| previous->revision == std::numeric_limits<uint64_t>::max()))
		throw std::invalid_argument("Changed or exhausted owned native environment");
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if(!previous) environment->inputs.at(side) = battle::ProjectedUnitEnvironmentState::captureInputs(*source.getBattle(), side);
		environment->states.at(side) = battle::ProjectedUnitEnvironmentState::captureState(*source.getBattle(), side);
	}
	if(!previous)
	{
		environment->magic = source.getCapturedMagicRules();
		if(!environment->magic) throw std::invalid_argument("Owned native environment needs captured rules");
		environment->skeletonUpgrades = CreatureID(CreatureID::SKELETON).toCreature()->upgrades;
		const auto skill = SecondarySkill::decode("new-horizons:necromancy");
		if(skill >= 0) environment->necromancyLuckSource = SecondarySkill(skill);
		const auto & settings = *LIBRARY->engineSettings();
		environment->legacyMoraleLimits = std::pair{-static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_BAD_MORALE_CHANCE).size()),
			static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE).size())};
		environment->legacyLuckLimits = std::pair{-static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_BAD_LUCK_CHANCE).size()),
			static_cast<int32_t>(settings.getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE).size())};
	}
	else ++environment->revision;
	environment->warfare.clear();
	for(const auto * unit : source.battleGetAllUnits(false))
		environment->warfare.emplace(unit->unitId(), source.unitWarfareStatBonus(unit));
	return environment;
}
void NativeAdapter::bindRootReadViews(HypotheticBattle & source, std::vector<UnitInput> & inputs)
{
	std::map<CreatureID, battle::OwnedUnitCreatureResolver::Creature> definitions;
	for(const auto & input : inputs)
	{
		const auto found = source.stackStates.find(input.definition->id);
		if(found == source.stackStates.end()) throw std::logic_error("Root native capture was not prepared");
		const auto * creature = found->second->unitType();
		definitions.try_emplace(creature->getId(), battle::ownedFrameCapture::descriptor(*creature));
	}
	std::vector<battle::OwnedUnitCreatureResolver::Creature> types;
	for(auto & [id, value] : definitions) types.push_back(std::move(value));
	auto creatures = std::make_shared<const battle::OwnedUnitCreatureResolver>(std::move(types));
	const auto environment = captureReadEnvironment(source);
	std::vector<NativeReadOccurrence> occurrences;
	occurrences.reserve(inputs.size());
	for(auto & input : inputs)
	{
		const auto & unit = *source.stackStates.at(input.definition->id);
		// Retaliation observation history is gameplay state. Preserve its ACTUAL
		// cache here; preparing a derived allowance is not a native observation.
		auto values = std::make_shared<const battle::UnitStateValueSnapshot>(battle::UnitStateValueSnapshot::capture(unit));
		auto definition = std::make_shared<UnitDefinition>(*input.definition);
		definition->readDescriptor = std::make_shared<const NativeReadDescriptor>(NativeReadDescriptor{
			values->identity(), {unit.getBattleStartMaximumAggregateHP(), unit.getRebirthOriginalAggregateHP()}, creatures});
		bonusDependencies::QueryFrameBinding binding{unit.unitId(), unit.creatureId(), unit.getCount(), unit.unitOwner(),
			unit.unitSide(), unit.unitSlot(), unit.isGhost(), unit.summoned, unit.natureSummoned, creatures};
		auto program = input.state.currentModifiers->bindQueries(binding, unit.localSpellEffects());
		if(!program) throw std::invalid_argument("Root modifiers lack an owning query binding");
		auto queries = program->refreshQueries(binding, bonusDependencies::Change::None);
		if(!queries) throw std::invalid_argument("Root native query closure unsupported");
		definition->modifiers = program;
		input.definition = std::move(definition);
		input.state.nativeValues = std::move(values);
		input.state.currentModifiers = std::move(program);
		input.state.queries = std::move(queries);
		occurrences.push_back({input.definition->readDescriptor, input.state.nativeValues,
			input.state.currentModifiers, input.state.queries});
	}
	const auto views = NativeReadView::fromOwned(std::move(occurrences), environment, source.forecastWorkSink());
	if(views.size() != inputs.size()) throw std::logic_error("Incomplete owned native roster");
	for(size_t index = 0; index < inputs.size(); ++index) inputs[index].state.readView = views[index];
}
}
