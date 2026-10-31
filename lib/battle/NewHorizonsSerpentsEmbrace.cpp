/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "Unit.h"
#include "BattleInfo.h"
#include "../CStack.h"
#include "../GameLibrary.h"
#include "../CCreatureHandler.h"
#include "IBattleState.h"
#include "CBattleInfoCallback.h"
#include "../entities/hero/NewHorizonsSerpentsEmbrace.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusSelector.h"
#include "../bonuses/BonusList.h"
#include <charconv>
#include <set>
namespace
{
constexpr std::string_view PREFIX = "serpentsEmbrace:";
void validateLoadedIdentity(const Bonus & bonus)
{
	if(!LIBRARY || bonus.sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:fafner"))))
		throw std::runtime_error("Source-bound restraint requires its canonical loaded hero identity");
}
bool naga(CreatureID creature)
{
	if(!creature.hasValue())
		return false;
	const auto * type = creature.toCreature();
	return type && (type->getJsonKey() == "core:naga" || type->getJsonKey() == "core:nagaQueen");
}
}
namespace newHorizonsSerpentsEmbrace
{
bool qualifiesAttack(const CBattleInfoCallback & battle, const battle::Unit * source,
	const battle::Unit * primary, bool ranged, bool physical, bool secondary)
{
	if(!source || !primary || ranged || !physical || secondary || !naga(source->creatureId())
		|| !source->alive() || !primary->alive() || source->isGhost() || primary->isGhost()
		|| source->isTimeStopped() || primary->isTimeStopped()
		|| battle.battleGetOwner(source) == battle.battleGetOwner(primary)
		|| !battle.battleMatchActionController(source, primary))
		return false;
	const auto side = battle.playerToSide(battle.battleGetOwner(source));
	return (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& newHorizonsHeroes::serpentsEmbraceEnabled(battle.battleGetFightingHero(side));
}
bool isMarker(const Bonus & bonus) noexcept
{
	return bonus.statusIdentity == "serpentsEmbrace" || bonus.statusIdentity.starts_with(PREFIX);
}
std::optional<uint32_t> sourceId(const Bonus & bonus)
{
	if(!isMarker(bonus))
		return std::nullopt;
	uint32_t source = 0;
	if(bonus.statusIdentity.size() <= PREFIX.size())
		throw std::runtime_error("Missing Serpent’s Embrace source identity");
	const auto text = std::string_view(bonus.statusIdentity).substr(PREFIX.size());
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), source);
	if(parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()
		|| text != std::to_string(source) || source > std::numeric_limits<int32_t>::max()
		|| bonus.type != BonusType::BIND_EFFECT || bonus.source != BonusSource::HERO_SPECIAL
		|| !bonus.sid.as<HeroTypeID>().hasValue()
		|| bonus.duration != BonusDuration::ONE_BATTLE || bonus.val != 0 || bonus.turnsRemain != 0
		|| !bonus.stacking.empty() || bonus.effectRange != BonusLimitEffect::NO_LIMIT
		|| bonus.valType != BonusValueType::ADDITIVE_VALUE || bonus.subtype != BonusSubtypeID()
		|| bonus.propagator || bonus.updater || bonus.propagationUpdater || bonus.limiter || !bonus.parameters
		|| bonus.statusTags != std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE})
		throw std::runtime_error("Invalid Serpent's Embrace source-bound restraint");
	if(bonus.parameters->toNumber() != static_cast<int32_t>(source))
		throw std::runtime_error("Serpent's Embrace source identity disagrees with provenance");
	return source;
}
std::optional<uint32_t> validatedApplicationSource(const Bonus & bonus)
{
	const auto source = sourceId(bonus);
	if(source) validateLoadedIdentity(bonus);
	return source;
}
void validateApplicationUnits(const IBattleInfo & battle, uint32_t sourceId, uint32_t targetId,
	const battle::Units & sources, const battle::Units & targets)
{
	if(sources.size() != 1 || targets.size() != 1 || sourceId == targetId
		|| !sources.front()->alive() || sources.front()->isGhost()
		|| !targets.front()->alive() || targets.front()->isGhost())
		throw std::runtime_error("Serpent's Embrace application references unavailable source or target");
	const auto * context = dynamic_cast<const CBattleInfoCallback *>(&battle);
	if(!context || !qualifiesAttack(*context, sources.front(), targets.front(), false, true, false))
		throw std::runtime_error("Serpent's Embrace application lacks its captured source entitlement");
}
void validateApplication(const IBattleInfo & battle, uint32_t targetId, const Bonus & bonus)
{
	validateApplicationWithLookup(battle, targetId, bonus, [&battle](uint32_t id)
	{
		return battle.getUnitsIf([id](const battle::Unit * unit) { return unit && unit->unitId() == id; });
	});
}
Bonus marker(uint32_t sourceUnitId, CreatureID sourceCreature)
{
	if(!naga(sourceCreature) || sourceUnitId > std::numeric_limits<int32_t>::max())
		throw std::invalid_argument("Serpent's Embrace requires a representable Naga source");
	Bonus result(BonusDuration::ONE_BATTLE, BonusType::BIND_EFFECT,
		BonusSource::HERO_SPECIAL, 0, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:fafner"))));
	result.parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(sourceUnitId));
	// Existing identified-status refresh keys keep independent sources separate.
	result.statusIdentity = std::string(PREFIX) + std::to_string(sourceUnitId);
	result.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	sourceId(result);
	return result;
}
bool blocksVoluntaryMovement(const battle::Unit & target)
{
	const auto bonuses = target.getBonuses(Selector::type()(BonusType::BIND_EFFECT));
	return bonuses && std::ranges::any_of(*bonuses, [](const auto & bonus)
	{
		return bonus && sourceId(*bonus).has_value();
	});
}
std::vector<Bonus> releasePlan(const battle::Unit & target, uint32_t sourceUnitId)
{
	std::vector<Bonus> result;
	const auto bonuses = target.getBonuses(Selector::type()(BonusType::BIND_EFFECT));
	if(bonuses)
		for(const auto & bonus : *bonuses)
			if(bonus && sourceId(*bonus) == sourceUnitId)
				result.push_back(*bonus);
	return result;
}
void releaseBindings(IBattleState & battle, uint32_t sourceUnitId)
{
	std::vector<std::pair<uint32_t, std::vector<Bonus>>> removals;
	for(const auto * unit : battle.getUnitsIf([](const battle::Unit *) { return true; }))
	{
		if(!unit || !unit->hasBonusOfType(BonusType::BIND_EFFECT))
			continue;
		auto plan = releasePlan(*unit, sourceUnitId);
		if(!plan.empty())
			removals.emplace_back(unit->unitId(), std::move(plan));
	}
	// Validate/collect before mutation; unrelated recipient overlays stay lazy.
	for(const auto & [target, plan] : removals)
		battle.removeUnitBonus(target, plan);
}

void releaseDeathBindings(IBattleState & battle, const battle::CUnitState & source)
{
	releaseBindings(battle, source.unitId());
	if(source.cloneID >= 0 && static_cast<uint32_t>(source.cloneID) != source.unitId())
		releaseBindings(battle, static_cast<uint32_t>(source.cloneID));
}

}

void BattleInfo::validateStoredSerpentBindings() const
{
	std::vector<std::pair<uint32_t, uint32_t>> references;
	for(const auto & target : stacks)
		if(target)
			for(const auto & bonus : target->getExportedBonusList())
				if(bonus)
					if(const auto source = newHorizonsSerpentsEmbrace::sourceId(*bonus))
						references.emplace_back(target->unitId(), *source);
	if(references.empty())
		return;
	std::set<uint32_t> unitIds;
	for(const auto & unit : stacks)
		if(!unit || !unitIds.insert(unit->unitId()).second)
			throw std::runtime_error("Source-bound restraint snapshot requires unique unit identities");
	// Descriptor IDs are serialized before localInit. Do not query count/position
	// or dereference a guessed live binder while validating a decoded snapshot.
	for(const auto & [target, source] : references)
		if(target == source || !unitIds.contains(source))
			throw std::runtime_error("Source-bound restraint snapshot has a missing or self source");
}

void BattleInfo::validateLoadedSerpentBindings() const
{
	validateStoredSerpentBindings();
	for(const auto & target : stacks)
		if(target)
			for(const auto & bonus : target->getExportedBonusList())
				if(bonus && newHorizonsSerpentsEmbrace::sourceId(*bonus))
					validateLoadedIdentity(*bonus);
}
