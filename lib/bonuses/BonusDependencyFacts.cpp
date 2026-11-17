/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BonusDependencyFacts.h"
#include "BonusDependencyTerrain.h"
#include "BonusList.h"
#include "BonusLimitation.h"
#include "BonusParameters.h"
#include "../json/JsonNode.h"
#include "Propagators.h"
#include "Updaters.h"
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <typeinfo>

namespace bonusDependencies
{
namespace
{
struct Classifier
{
	std::set<const void *> active;
	size_t remaining = 4096;
	Classification result;
	bool terrainCertified = false;
	bool enter(const void * pointer, size_t depth)
	{
		if(depth > 64 || !remaining || !active.insert(pointer).second)
		{
			result.supported = false;
			return false;
		}
		--remaining;
		return true;
	}
	void updater(const TUpdaterPtr & rule, size_t depth = 0)
	{
		if(!rule || !enter(rule.get(), depth)) return;
		const auto & type = typeid(*rule);
		if(type == typeid(TimesHeroLevelDivideStackLevelUpdater)
			&& !static_cast<const TimesHeroLevelDivideStackLevelUpdater &>(*rule).hasScalarDivider()) result.supported = false;
		if(type == typeid(CompositeUpdater))
			for(const auto & child : static_cast<const CompositeUpdater &>(*rule).updaters)
			{
				if(!result.supported) break;
				if(!child) result.supported = false;
				else updater(child, depth + 1);
			}
		else if(type == typeid(TimesStackSizeUpdater)) result.dependencies = result.dependencies | Change::Count;
		else if(type == typeid(OwnerUpdater)) result.dependencies = result.dependencies | Change::Controller;
		else if(type == typeid(GrowsWithLevelUpdater) || type == typeid(TimesHeroLevelUpdater)
			|| type == typeid(TimesHeroLevelDivideStackLevelUpdater))
			result.dependencies = result.dependencies | Change::OwnerLevel | Change::Profile;
		else if(type == typeid(TimesStackLevelUpdater) || type == typeid(DivideStackLevelUpdater))
			result.dependencies = result.dependencies | Change::Profile;
		else if(type == typeid(TimesArmySizeUpdater))
		{
			result.dependencies = result.dependencies | Change::Count | Change::Roster | Change::Profile;
			result.supported = false;
		}
		else if(type != typeid(IUpdater)) result.supported = false;
		active.erase(rule.get());
	}
	void limiter(const TLimiterPtr & rule, size_t depth = 0, bool terrainChain = true)
	{
		if(!rule || !enter(rule.get(), depth)) return;
		const auto & type = typeid(*rule);
		if(type == typeid(AllOfLimiter) || type == typeid(AnyOfLimiter) || type == typeid(NoneOfLimiter))
			for(const auto & child : static_cast<const AggregateLimiter &>(*rule).limiters)
			{
				if(!result.supported) break;
				if(!child) result.supported = false;
				else limiter(child, depth + 1, terrainChain && type == typeid(AllOfLimiter));
			}
		else if(type == typeid(CCreatureTypeLimiter) || type == typeid(CreatureLevelLimiter)
			|| type == typeid(FactionLimiter) || type == typeid(CreatureAlignmentLimiter)
			|| type == typeid(RankRangeLimiter)) result.dependencies = result.dependencies | Change::Profile;
		else if(type == typeid(OppositeSideLimiter)) result.dependencies = result.dependencies | Change::Controller;
		else if(type == typeid(TerrainLimiter) && terrainCertified && terrainChain)
			result.dependencies = result.dependencies | Change::TerrainContext | Change::Profile | Change::ActiveEffects;
		else if(type != typeid(ILimiter))
		{
			result.supported = false;
			if(type == typeid(UnitOnHexLimiter)) result.dependencies = result.dependencies | Change::Position;
			if(type == typeid(UnitAdjacentLimiter))
				result.dependencies = result.dependencies | Change::Position | Change::Roster | Change::Controller | Change::Profile;
			if(type == typeid(HasAnotherBonusLimiter) || type == typeid(HasChargesLimiter))
				result.dependencies = result.dependencies | Change::ActiveEffects;
			if(type == typeid(TerrainLimiter)) result.dependencies = result.dependencies | Change::Position | Change::Profile;
		}
		active.erase(rule.get());
	}
};
ILimiter::EDecision decide(const Bonus & bonus, const BonusEvaluationContext & context)
{
	// Native limitation owns both acceptsBonus and limiter order. No bespoke oracle.
	BonusList input, accepted;
	const auto value = std::make_shared<Bonus>(bonus);
	input.push_back(value);
	limitBonuses(input, accepted, context);
	return accepted.empty() ? ILimiter::EDecision::DISCARD : ILimiter::EDecision::ACCEPT;
}

bool primaryRating(const Bonus & bonus)
{
	return bonus.type == BonusType::PRIMARY_SKILL
		&& (bonus.subtype == BonusSubtypeID(PrimarySkill::ATTACK)
			|| bonus.subtype == BonusSubtypeID(PrimarySkill::DEFENSE));
}
}
bool detail::heroBaseManaRegeneration(const Bonus & raw)
{
	if(typeid(raw) != typeid(Bonus) || raw.type != BonusType::MANA_REGENERATION
		|| raw.source != BonusSource::HERO_BASE_SKILL || raw.sid.getNum() < 0
		|| raw.duration != BonusDuration::PERMANENT || raw.turnsRemain != 0
		|| raw.val != 1 || raw.valType != BonusValueType::BASE_NUMBER
		|| raw.effectRange != BonusLimitEffect::NO_LIMIT || raw.subtype != BonusSubtypeID()
		|| raw.targetSourceType != BonusSource::OTHER || raw.parameters || raw.updater
		|| raw.propagator || raw.propagationUpdater || !raw.stacking.empty()
		|| !raw.limiter || typeid(*raw.limiter) != typeid(NoneOfLimiter)) return false;
	const auto & children = static_cast<const NoneOfLimiter &>(*raw.limiter).limiters;
	if(children.size() != 1 || !children.front()
		|| typeid(*children.front()) != typeid(HasAnotherBonusLimiter)) return false;
	const auto & peer = static_cast<const HasAnotherBonusLimiter &>(*children.front());
	return peer.type == BonusType::MANA_REGENERATION && peer.isSourceRelevant
		&& peer.source == BonusSource::SECONDARY_SKILL && !peer.isSourceIDRelevant
		&& !peer.isSubtypeRelevant && peer.minValue == std::numeric_limits<int32_t>::min()
		&& peer.maxValue == std::numeric_limits<int32_t>::max();
}
namespace
{
Classification classifyImpl(const Bonus & raw, bool terrainCertified)
{
	Classifier classifier;
	classifier.terrainCertified = terrainCertified;
	if(typeid(raw) != typeid(Bonus) || raw.propagationUpdater
		|| raw.type == BonusType::CREATURE_ABILITY_SUPPRESSION
		|| (raw.type == BonusType::BIND_EFFECT && raw.propagator)) classifier.result.supported = false;
	if(raw.propagator && typeid(*raw.propagator) != typeid(IPropagator)
		&& typeid(*raw.propagator) != typeid(CPropagatorNodeType)) classifier.result.supported = false;
	if(raw.parameters)
	{
		// Opaque JSON and executable combat-effect payloads have no scalar recipe.
		bool scalar = raw.parameters->isVector();
		const auto holds = [&]<class T>()
		{
			try { (void)raw.parameters->toCustom<T>(); return true; }
			catch(const std::runtime_error &) { return false; }
		};
		scalar = scalar || holds.operator()<int32_t>() || holds.operator()<CreatureID>() || holds.operator()<SpellID>();
		// The native parser preserves this passive pool selector as JSON, not int32_t.
		if(!scalar && raw.type == BonusType::RANDOM_SPELLCASTER)
			try
			{
				const auto & selector = raw.parameters->toCustom<JsonNode>();
				scalar = selector.getType() == JsonNode::JsonType::DATA_INTEGER
					&& (selector.Integer() == 0 || selector.Integer() == 1);
			}
			catch(const std::runtime_error &) { /* Other payload variants remain unsupported. */ }
		if(!scalar) classifier.result.supported = false;
	}
	if(raw.duration != BonusDuration::PERMANENT)
		classifier.result.dependencies = classifier.result.dependencies | Change::Round | Change::ActiveEffects;
	classifier.updater(raw.updater);
	if(detail::heroBaseManaRegeneration(raw))
		classifier.result.dependencies = classifier.result.dependencies | Change::ActiveEffects;
	else classifier.limiter(raw.limiter);
	return classifier.result;
}

Classification classifyLocal(const Bonus & raw, bool terrainCertified)
{
	if(typeid(raw) != typeid(Bonus)) return {false};
	Bonus local(raw);
	local.propagationUpdater.reset();
	if(raw.propagationUpdater && detail::heroBaseManaRegeneration(local)) return {false};
	auto result = classifyImpl(local, terrainCertified);
	Classifier propagation;
	propagation.updater(raw.propagationUpdater);
	result.supported = result.supported && propagation.result.supported
		&& !intersects(propagation.result.dependencies, Change::Count | Change::Position | Change::Roster);
	result.dependencies = result.dependencies | propagation.result.dependencies;
	return result;
}

}
Classification classify(const Bonus & raw) { return classifyImpl(raw, false); }
Classification classifyMaterializedLocal(const Bonus & raw) { return classifyLocal(raw, false); }
Classification detail::TerrainCaptureCertificate::classifyMaterialized(const Bonus & raw) const
{ return classifyLocal(raw, true); }

std::shared_ptr<const detail::TerrainWitnesses> detail::TerrainCaptureCertificate::witnesses(
	std::span<const Stage> stages, const BonusEvaluationContext & finalContext) const
{
	auto owned = std::make_shared<TerrainWitnesses>();
	const auto capture = [&](const BonusEvaluationContext & context)
	{
		TerrainWitness witness{context.getNodeType(), {}, {}, {}, {}, {}, {}};
		if(TerrainLimiter::appliesToNode(witness.destination))
		{
			witness.faction = context.faction();
			if(!witness.faction || !witness.faction->hasValue())
				throw std::invalid_argument("Unbound certified terrain faction");
			witness.adventure = context.adventureTerrain();
			witness.query = context.queryTerrain();
			witness.battle = context.battleTerrain();
			witness.resolved = context.resolveTerrain(witness.adventure);
			witness.nativeMembership = context.nativeTerrainMatches(witness.adventure);
		}
		owned->values.push_back(std::move(witness));
	};
	for(const auto & stage : stages) capture(stage.context);
	capture(finalContext);
	return owned;
}

Recipe capture(const std::shared_ptr<const Bonus> & raw, std::span<const Stage> stages,
	const BonusEvaluationContext & finalContext)
{
	if(!raw || !classify(*raw).supported)
		throw std::invalid_argument("Unsupported modifier dependency recipe");
	const std::shared_ptr<const ownedBonusPayloads::Captured> owner = ownedBonusPayloads::copyOwnedValues({raw});
	return capture(owner, 0, stages, finalContext, false);
}

Recipe capture(const std::shared_ptr<const ownedBonusPayloads::Captured> & owner, size_t rootIndex,
	std::span<const Stage> stages, const BonusEvaluationContext & finalContext, bool materializedLocal)
{
	return detail::captureCertified(owner, rootIndex, stages, finalContext, materializedLocal, nullptr);
}
Recipe detail::captureCertified(const std::shared_ptr<const ownedBonusPayloads::Captured> & owner, size_t rootIndex,
	std::span<const Stage> stages, const BonusEvaluationContext & finalContext, bool materializedLocal,
	const TerrainCaptureCertificate * terrain, const BonusList * nativePeers, const Bonus * nativeOutput)
{
	if(!owner || rootIndex >= owner->roots().size() || stages.size() > 64)
		throw std::invalid_argument("Invalid owned modifier recipe root");
	const auto & raw = owner->roots().at(rootIndex);
	const auto classification = raw ? (terrain ? terrain->classifyMaterialized(*raw)
		: materializedLocal ? classifyMaterializedLocal(*raw) : classify(*raw)) : Classification{false};
	if(!raw || !classification.supported || (stages.empty() && raw->updater))
		throw std::invalid_argument("Unsupported or incomplete modifier dependency trace");
	const bool peerMana = detail::heroBaseManaRegeneration(*raw);
	if(peerMana && (!nativePeers || !nativeOutput || !detail::heroBaseManaRegeneration(*nativeOutput)))
		throw std::invalid_argument("Hero base recovery requires full native peer collection");
	Recipe recipe;
	if(intersects(classification.dependencies, Change::TerrainContext))
		recipe.terrainWitnesses = terrain->witnesses(stages, finalContext);
	recipe.required = classification.dependencies | Change::Profile | Change::Controller;
	recipe.payloads = owner;
	recipe.record = raw;
	auto current = raw;
	for(const auto & stage : stages)
	{
		std::function<void(const TUpdaterPtr &)> append = [&](const TUpdaterPtr & rule)
		{
			if(!rule) return;
			if(typeid(*rule) == typeid(CompositeUpdater))
			{
				for(const auto & child : static_cast<const CompositeUpdater &>(*rule).updaters) append(child);
				return;
			}
			if(recipe.instructions.size() >= 4096)
				throw std::invalid_argument("Oversized modifier recipe");
			if(typeid(*rule) == typeid(TimesStackSizeUpdater) && !stage.currentUnitCount
				&& stage.context.getNodeType() == BonusNodeType::STACK_BATTLE)
				throw std::invalid_argument("Peer battle-count dependency requires roster refresh");
			Recipe::Instruction instruction;
			instruction.kernel = rule;
			instruction.currentUnitCount = stage.currentUnitCount;
			instruction.count = stage.context.stackCount().value_or(0);
			instruction.owner = stage.context.getOwner();
			const auto & type = typeid(*rule);
			if(type == typeid(GrowsWithLevelUpdater) || type == typeid(TimesHeroLevelUpdater))
				instruction.level = stage.context.heroLevel();
			if(type == typeid(TimesStackLevelUpdater) || type == typeid(DivideStackLevelUpdater))
				instruction.level = stage.context.stackLevel();
			recipe.instructions.push_back(std::move(instruction));
		};
		append(current->updater);
		if(current->updater) current = current->updater->createUpdatedBonus(current, stage.context);
		if(stage.inheritedPrimaryRating && primaryRating(*current)
			&& decide(*current, stage.context) == ILimiter::EDecision::ACCEPT)
		{
			recipe.decision = ILimiter::EDecision::DISCARD;
			recipe.materialized = current;
			return recipe; // Same occurrence suppression as collectBonusGraph.
		}
	}
	recipe.materialized = current;
	if(peerMana)
	{
		BonusList accepted;
		limitBonuses(*nativePeers, accepted, finalContext);
		// No updater is legal for this exact row; the native output retains raw identity.
		recipe.decision = std::ranges::any_of(accepted, [&](const auto & row) { return row.get() == nativeOutput; })
			? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
	}
	else recipe.decision = decide(*current, finalContext);
	return recipe;
}

bool Recipe::canRefresh(Change changes) const noexcept
{
	const auto immutable = static_cast<Change>(static_cast<uint16_t>(required)
		& ~static_cast<uint16_t>(Change::Count));
	// Any source/effect, roster or round mutation needs fresh admission, even if
	// this record itself had no explicit predicate on that domain.
	return !intersects(changes, immutable | Change::Profile | Change::Controller | Change::ActiveEffects | Change::Roster | Change::Round | Change::TerrainContext);
}

Values Recipe::evaluate(int currentCount, Change changes) const
{
	if(!canRefresh(changes)) throw std::invalid_argument("Modifier recipe requires rebind before mutation");
	Values result{record->val, record->bonusOwner, decision};
	for(const auto & instruction : instructions)
	{
		const auto & rule = *instruction.kernel;
		const auto & type = typeid(rule);
		if(type == typeid(TimesStackSizeUpdater))
			result.value = static_cast<const TimesStackSizeUpdater &>(rule).valueForCount(
				instruction.currentUnitCount ? currentCount : instruction.count);
		else if(type == typeid(GrowsWithLevelUpdater))
			result.value = static_cast<const GrowsWithLevelUpdater &>(rule).valueForLevel(result.value, instruction.level);
		else if(type == typeid(TimesHeroLevelUpdater))
			result.value = static_cast<const TimesHeroLevelUpdater &>(rule).valueForLevel(result.value, instruction.level);
		else if(type == typeid(TimesStackLevelUpdater))
			result.value = static_cast<const TimesStackLevelUpdater &>(rule).valueForLevel(result.value, instruction.level);
		else if(type == typeid(DivideStackLevelUpdater))
			result.value = static_cast<const DivideStackLevelUpdater &>(rule).valueForLevel(result.value, instruction.level);
		else if(type == typeid(OwnerUpdater))
			result.owner = static_cast<const OwnerUpdater &>(rule).ownerFor(instruction.owner);
		// Exact IUpdater is identity; TimesHeroLevelDivideStackLevel changes
		// only updater metadata, captured by the next native collection stage.
	}
	return result;
}
}
