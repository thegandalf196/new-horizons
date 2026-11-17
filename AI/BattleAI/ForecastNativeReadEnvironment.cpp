/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastNativeReadViewInternal.h"
#include "../../lib/battle/UnitAdjacency.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/nativeRules/CapturedRuleSet.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include <stdexcept>
namespace forecast
{
NativeReadView::Group::Context::Context(Group & group, const Receiver * replacement)
	: group(group), replacement(replacement), queries(*this) {}
void NativeReadView::Group::Context::requireSubject(const battle::Unit & unit) const
{
	if(replacement)
	{
		if(replacement->owns(unit)) return;
	}
	else
	{
		for(const auto & entry : group.views)
			if(entry && entry->owns(unit)) return;
	}
	throw std::invalid_argument("Native owned context requires its EXACT receiver, not another same-ID unit");
}
battle::Units NativeReadView::Group::Context::unitsIf(const battle::UnitFilter & predicate) const
{
	battle::Units result;
	for(size_t index = 0; index < group.occurrences.size(); ++index)
	{
		const auto * unit = replacement && replacement->slot == index
			? replacement->state.get() : group.view(index).state.get();
		if(predicate(unit)) result.push_back(unit);
	}
	return result; // Exact original roster order, including dead Cart occurrences.
}
PlayerColor NativeReadView::Group::Context::sidePlayer(BattleSide side) const { return group.inputs(side).color; }
bool NativeReadView::Group::Context::sideHasAmmoArtifact(BattleSide side) const { return group.inputs(side).ammoArtifact; }
bool NativeReadView::Group::Context::sideHasVeteranCohesion(BattleSide side) const { return group.inputs(side).veteranCohesion; }
SylvanLuckState NativeReadView::Group::Context::fortune(BattleSide side) const { return group.sideState(side).fortune; }
int NativeReadView::Group::Context::bloodrageDamage(BattleSide side) const { return group.sideState(side).bloodrageDamagePercent; }
int NativeReadView::Group::Context::bloodrageRank(BattleSide side) const { return group.inputs(side).bloodrageRank; }
int NativeReadView::Group::Context::bloodrageCap(BattleSide side) const { return group.inputs(side).bloodrageCap; }
int NativeReadView::Group::Context::bloodrageSpeed(BattleSide side) const { return group.inputs(side).bloodrageSpeed; }
int NativeReadView::Group::Context::bloodrageRetaliations(BattleSide side) const { return group.inputs(side).bloodrageRetaliations; }
int NativeReadView::Group::Context::bloodragePain(BattleSide side) const { return group.inputs(side).bloodragePain; }
int NativeReadView::Group::Context::magicResistance(const battle::Unit * unit) const
{
	return battle::UnitMagicResistanceQuery::query(unit);
}
newHorizonsNecromancy::CombatPassives NativeReadView::Group::Context::necromancyPassives(BattleSide side) const
{
	return group.inputs(side).necromancy;
}
newHorizonsNecromancy::CombatPassives NativeReadView::Group::Context::filterNecromancyPassives(
	const newHorizonsNecromancy::CombatPassives & captured, CreatureID creature) const
{
	auto result = newHorizonsNecromancy::passivesForFamily(captured,
		newHorizonsNecromancy::isSkeletonFamily(creature, group.environment->skeletonUpgrades));
	result.luckSource = newHorizonsNecromancy::CombatPassives::LuckSource{group.environment->necromancyLuckSource};
	return result;
}
newHorizonsElementalAttunement::Profile NativeReadView::Group::Context::elementalAttunementProfile(BattleSide side) const
{
	return group.inputs(side).elementalAttunementProfile;
}
uint8_t NativeReadView::Group::Context::elementalAttunementMask(BattleSide side) const
{
	return group.sideState(side).elementalAttunementMask;
}
bool NativeReadView::Group::Context::knowsElementalAttunementProfile(BattleSide side) const
{
	return group.inputs(side).elementalAttunementProfileKnown;
}
const JsonNode & NativeReadView::Group::Context::magicRules() const { return group.environment->magic->document(); }
battle::Units NativeReadView::Group::Context::adjacentUnits(const battle::Unit & geometry) const
{
	requireSubject(geometry);
	const auto & hexes = geometry.getSurroundingHexes();
	return unitsIf([&](const battle::Unit * candidate) { return battle::isAdjacentLivingUnit(hexes, *candidate); });
}
bool NativeReadView::Group::Context::newHorizonsRulesActive() const { return newHorizonsMagic::rulesActive(magicRules()); }
bool NativeReadView::Group::Context::unitHasAmmoCart(const battle::Unit * unit) const { return queries.unitHasAmmoCart(unit); }
PlayerColor NativeReadView::Group::Context::unitEffectiveOwner(const battle::Unit * unit) const { return queries.unitEffectiveOwner(unit); }
std::optional<int> NativeReadView::Group::Context::unitMagicResistance(const battle::Unit * unit) const
{
	return queries.unitMagicResistance(unit);
}
int NativeReadView::Group::Context::unitSylvanTacticsSpeed(const battle::Unit * unit, int turn) const
{
	return queries.unitSylvanTacticsSpeed(unit, turn);
}
int NativeReadView::Group::Context::unitSylvanTacticsInitiativePercent(const battle::Unit * unit) const
{
	return queries.unitSylvanTacticsInitiativePercent(unit);
}
int NativeReadView::Group::Context::unitFortuneSpeed(const battle::Unit * unit) const { return queries.unitFortuneSpeed(unit); }
int NativeReadView::Group::Context::unitSpeedBonus(const battle::Unit * unit) const { return queries.unitSpeedBonus(unit); }
int NativeReadView::Group::Context::unitWarfareStatBonus(const battle::Unit * unit) const
{
	if(!unit) throw std::invalid_argument("Missing owned Warfare subject");
	requireSubject(*unit);
	if(!unit->alive() || unit->isGhost()) return 0;
	const auto found = group.environment->warfare.find(unit->unitId());
	if(found == group.environment->warfare.end()) throw std::logic_error("Unknown owned Warfare occurrence");
	return found->second;
}
bool NativeReadView::Group::Context::unitSlowAffectsSpeed(const battle::Unit * unit) const
{
	return queries.unitSlowAffectsSpeed(unit);
}
newHorizonsNecromancy::CombatPassives NativeReadView::Group::Context::unitNecromancyPassives(const battle::Unit * unit) const
{
	return queries.unitNecromancyPassives(unit);
}
int NativeReadView::Group::Context::unitAdditionalRetaliations(const battle::Unit * unit) const
{
	return queries.unitAdditionalRetaliations(unit);
}
newHorizonsElementalAttunement::UnitPassives NativeReadView::Group::Context::unitElementalAttunement(const battle::Unit * unit) const
{
	return queries.unitElementalAttunement(unit);
}
int NativeReadView::Group::Context::unitBloodragePainIncrement(const battle::Unit * unit) const
{
	return queries.unitBloodragePainIncrement(unit);
}
bool NativeReadView::Group::Context::unitHasVeteranCohesion(const battle::Unit * unit) const
{
	return queries.unitHasVeteranCohesion(unit);
}
std::optional<std::pair<int32_t, int32_t>> NativeReadView::Group::Context::unitMoraleLimits(const battle::Unit * unit) const
{
	if(!unit) throw std::invalid_argument("Missing owned Morale subject");
	requireSubject(*unit);
	const auto saved = queries.unitMoraleLimits(unit);
	return saved ? saved : group.environment->legacyMoraleLimits;
}
std::optional<std::pair<int32_t, int32_t>> NativeReadView::Group::Context::unitLuckLimits(const battle::Unit * unit) const
{
	if(!unit) throw std::invalid_argument("Missing owned Luck subject");
	requireSubject(*unit);
	return group.environment->legacyLuckLimits;
}
std::shared_ptr<battle::CUnitState> NativeReadView::Group::Context::acquireOwnedState(const battle::CUnitState & source) const
{
	requireSubject(source);
	if(replacement) return group.loan(source, replacement->slot);
	for(size_t index = 0; index < group.views.size(); ++index)
		if(group.views[index] && group.views[index]->owns(source)) return group.loan(source, index);
	throw std::logic_error("Owned native acquisition lost its bound occurrence");
}
}
