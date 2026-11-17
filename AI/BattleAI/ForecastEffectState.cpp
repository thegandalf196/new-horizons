/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastEffectState.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/BonusParameters.h"
#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>

namespace forecastEffects
{
namespace
{
bool numeric(BonusType type)
{
	switch(type)
	{
	case BonusType::PRIMARY_SKILL: case BonusType::STACKS_SPEED: case BonusType::STACKS_INITIATIVE:
	case BonusType::STACK_HEALTH: case BonusType::MORALE: case BonusType::LUCK:
	case BonusType::CREATURE_DAMAGE: case BonusType::GENERAL_DAMAGE_PREMY:
	case BonusType::ALWAYS_MINIMUM_DAMAGE: case BonusType::ALWAYS_MAXIMUM_DAMAGE:
	case BonusType::GENERAL_ATTACK_REDUCTION: case BonusType::ENEMY_DEFENCE_REDUCTION:
	case BonusType::PHYSICAL_DEFENSE: case BonusType::ENEMY_ATTACK_REDUCTION:
	case BonusType::PERCENTAGE_DAMAGE_BOOST: case BonusType::GENERAL_DAMAGE_REDUCTION:
	case BonusType::SPELL_DAMAGE_REDUCTION: case BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS:
	case BonusType::MAGIC_RESISTANCE:
	case BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS: case BonusType::ADDITIONAL_RETALIATION:
	case BonusType::DEFENSIVE_STANCE: case BonusType::BLOCKS_RETALIATION:
	case BonusType::NO_MELEE_PENALTY: case BonusType::NO_DISTANCE_PENALTY:
	case BonusType::NO_WALL_PENALTY: case BonusType::SPELL_IMMUNITY:
	case BonusType::LEVEL_SPELL_IMMUNITY: case BonusType::MIND_IMMUNITY:
		return true;
	default: return false;
	}
}
bool source(const Bonus & value, std::optional<SpellID> id)
{
	return id && value.source == BonusSource::SPELL_EFFECT && value.sid == BonusSourceID(*id);
}
void require(bool condition, const char * message)
{
	if(!condition) throw std::invalid_argument(message);
}
}
Kind State::classify(const RawRecord & raw, const Admission & admission)
{
	require(raw.value != nullptr, "Null timed-effect record");
	const auto & bonus = *raw.value;
	require(!bonus.updater && !bonus.propagationUpdater && !bonus.propagator && !bonus.limiter,
		"Timed-effect provider/updater/limiter has no owned transition proof");
	require(bonus.hasValidStatusMetadata(), "Invalid timed-effect status metadata");
	constexpr auto allowed = BonusDuration::PERMANENT | BonusDuration::ONE_BATTLE | BonusDuration::N_TURNS
		| BonusDuration::UNTIL_ATTACK | BonusDuration::UNTIL_OWN_ATTACK | BonusDuration::UNTIL_BEING_ATTACKED
		| BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE | BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE
		| BonusDuration::STACK_GETS_TURN | BonusDuration::STACK_ACTIVATION | BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION;
	require(bonus.duration != 0 && (bonus.duration & ~allowed) == 0, "Unsupported effect clock");
	require(raw.roundClock == Bonus::NTurns(&bonus), "Missing or foreign source-owned round clock");
	if(bonus.stacking == "new-horizons:focusMagic" || source(bonus, admission.focusMagic))
	{
		require(source(bonus, admission.focusMagic), "Focus identity not captured from registry");
	}
	if(source(bonus, admission.focusMagic))
	{
		require(bonus.duration == BonusDuration::ONE_BATTLE && !bonus.parameters
			&& bonus.stacking == "new-horizons:focusMagic", "Unproven Focus payload");
		require((bonus.type == BonusType::NONE && bonus.val >= 1 && bonus.val <= 3)
			|| (bonus.type == BonusType::SHOOTER && bonus.val == 1)
			|| (bonus.type == BonusType::PERCENTAGE_DAMAGE_BOOST
				&& bonus.subtype == BonusSubtypeID(BonusCustomSubtype::damageTypeRanged) && bonus.val >= 0),
			"Unproven Focus component");
		return Kind::FocusManaged;
	}
	if(const auto frozenRound = newHorizonsFrozen::markerApplicationRound(bonus))
	{
		require(admission.frozenApplicationRound >= *frozenRound && *frozenRound <= admission.round,
			"Frozen application receipt missing or from future round");
		return Kind::Frozen;
	}
	if(bonus.type == BonusType::BIND_EFFECT)
	{
		require(bonus.source == BonusSource::SPELL_EFFECT, "Typed non-spell Bind needs separate lifecycle proof");
		if(bonus.stacking == "new-horizons:forceLance")
			require(source(bonus, admission.forceLance), "Force Lance identity not captured from registry");
		if(source(bonus, admission.forceLance))
		{
			require(bonus.duration == BonusDuration::N_TURNS && bonus.turnsRemain == 1 && bonus.val == 1
				&& !bonus.parameters && bonus.stacking == "new-horizons:forceLance", "Invalid Force Lance clock");
			return Kind::ForceLance;
		}
		if(bonus.parameters)
			require(bonus.parameters->toNumber() >= 0, "Invalid classic Bind source");
		return Kind::Bind;
	}
	require(numeric(bonus.type), "Unsupported typed timed-effect family");
	if(bonus.parameters) (void)bonus.parameters->toNumber(); // finite scalar addInfo only
	require(bonus.source == BonusSource::SPELL_EFFECT
		|| (bonus.source == BonusSource::OTHER && bonus.duration == BonusDuration::STACK_GETS_TURN),
		"Unproven timed-effect source family");
	return Kind::Numeric;
}
std::shared_ptr<const State> State::capture(std::span<const RawRecord> raw, Admission admission)
{
	require(admission.round >= 0 && admission.frozenApplicationRound >= -1
		&& admission.frozenApplicationRound <= admission.round, "Invalid effect capture clock");
	require(raw.size() <= std::numeric_limits<RecordId>::max(), "Too many effect records");
	auto result = std::make_shared<State>();
	result->admission = std::move(admission);
	std::vector<std::shared_ptr<const Bonus>> roots;
	std::map<const Bonus *, Origin> aliases;
	for(const auto & input : raw)
	{
		const auto kind = classify(input, result->admission);
		if(const auto existing = aliases.find(input.value.get()); existing != aliases.end())
		{
			require(existing->second == input.origin, "One record shared across multiple native exporter clocks");
			const auto & earlier = *std::ranges::find_if(result->records, [&](const Record & record)
			{ return record.origin == input.origin; });
			require(earlier.roundClock == input.roundClock && earlier.transientOnDeath == input.transientOnDeath,
				"Conflicting source-identical alias metadata");
			continue;
		}
		require(std::ranges::none_of(result->records, [&](const Record & record) { return record.origin == input.origin; }),
			"Exporter occurrence has multiple record identities");
		aliases.emplace(input.value.get(), input.origin);
		const auto id = static_cast<RecordId>(result->records.size());
		result->records.push_back({id, input.origin, kind, input.roundClock, input.transientOnDeath, {}});
		roots.push_back(input.value);
	}
	ownedBonusPayloads::validateTypesAndTopology(roots);
	const std::shared_ptr<const ownedBonusPayloads::Captured> owner = ownedBonusPayloads::copyOwnedValues(roots);
	for(size_t index = 0; index < roots.size(); ++index)
	{
		result->records[index].value = owner->roots()[index];
		result->payloadOwners.emplace(result->records[index].id, owner);
	}
	result->nextRecordId = roots.size();
	return result;
}
std::shared_ptr<State> State::copy(const State & source)
{
	auto result = std::make_shared<State>();
	result->records = source.records;
	result->admission = source.admission;
	result->forfeit = source.forfeit;
	result->revisionValue = source.revisionValue;
	result->nextRecordId = source.nextRecordId;
	// Admitted payloads are immutable. Only a changed scalar record is copied;
	// no repeated payload validation, serialization or full graph clone per event.
	result->payloadOwners = source.payloadOwners;
	return result;
}
const Record * State::find(RecordId id) const
{
	const auto found = std::ranges::find_if(records, [id](const Record & record) { return record.id == id; });
	return found == records.end() ? nullptr : &*found;
}
bool State::frozen() const
{
	return std::ranges::any_of(records, [](const Record & record) { return record.kind == Kind::Frozen; });
}
}
