/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitEnvironment.h"
#include "UnitOwnershipQuery.h"
#include "ProjectedUnitMagicResistance.h"
#include "../entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../bonuses/ProjectedBonusRoster.h"
#include <stdexcept>
namespace
{
bool knownAffiliation(PlayerColor value)
{
	return value.isValidPlayer() || value == PlayerColor::NEUTRAL || value == PlayerColor::UNFLAGGABLE;
}
std::vector<const battle::Unit *> rawUnits(const std::vector<std::shared_ptr<const battle::Unit>> & units)
{
	std::vector<const battle::Unit *> result;
	for(const auto & unit : units) result.push_back(unit.get());
	return result;
}
}
namespace battle
{
class OwnedUnitEnvironment::Data final : public UnitEnvironmentContext, public UnitOwnershipQuery
{
	// Retire queries/context BEFORE their captured metadata/roster/unit owners.
	std::vector<std::shared_ptr<const Unit>> units;
	Facts facts;
	ProjectedBonusRoster roster;
	ProjectedUnitMagicResistance resistance;
	const ProjectedUnitEnvironmentState::SideInputs & inputs(BattleSide side) const
	{
		if(!facts.inputs.at(side)) throw std::logic_error("Owned side policy facts are unknown");
		return *facts.inputs.at(side);
	}
	const ProjectedUnitEnvironmentState::SideState & sideState(BattleSide side) const
	{
		if(!facts.states.at(side)) throw std::logic_error("Owned side state facts are unknown");
		return *facts.states.at(side);
	}
	static const JsonNode & rules(const Facts & values)
	{
		if(!values.magicRules) throw std::invalid_argument("Owned magic context is unknown");
		return *values.magicRules;
	}
public:
	UnitEnvironmentQueries queries;
	Data(std::vector<std::shared_ptr<const Unit>> values, Facts metadata)
		: units(std::move(values)), facts(std::move(metadata)),
		roster(rawUnits(units), ProjectedBonusRoster::Identity::OrderedOccurrences),
		resistance(roster, rules(facts)), queries(*this)
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			if(!facts.colors.at(side) || !knownAffiliation(*facts.colors.at(side))
				|| (facts.inputs.at(side) && facts.inputs.at(side)->color != *facts.colors.at(side)))
				throw std::invalid_argument("Owned side affiliation facts are inconsistent");
	}
	ProjectedUnitEnvironmentState::SideState & state(BattleSide side)
	{
		if(!facts.states.at(side)) throw std::logic_error("Owned side state facts are unknown");
		return *facts.states.at(side);
	}
	bool available() const override { return true; }
	void requireSubject(const Unit & unit) const override
	{
		if(!roster.contains(unit)) throw std::invalid_argument("Owned environment subject is outside its roster");
	}
	const UnitOwnershipQuery & ownership() const override { return *this; }
	PlayerColor sidePlayer(BattleSide side) const override { return *facts.colors.at(side); }
	Units unitsIf(const UnitFilter & predicate) const override
	{
		Units result;
		for(const auto * unit : roster.orderedUnits()) if(predicate(unit)) result.push_back(unit);
		return result;
	}
	bool sideHasAmmoArtifact(BattleSide side) const override { return inputs(side).ammoArtifact; }
	bool sideHasVeteranCohesion(BattleSide side) const override { return inputs(side).veteranCohesion; }
	SylvanLuckState fortune(BattleSide side) const override { return sideState(side).fortune; }
	int bloodrageDamage(BattleSide side) const override { return sideState(side).bloodrageDamagePercent; }
	int bloodrageRank(BattleSide side) const override { return inputs(side).bloodrageRank; }
	int bloodrageCap(BattleSide side) const override { return inputs(side).bloodrageCap; }
	int bloodrageSpeed(BattleSide side) const override { return inputs(side).bloodrageSpeed; }
	int bloodrageRetaliations(BattleSide side) const override { return inputs(side).bloodrageRetaliations; }
	int bloodragePain(BattleSide side) const override { return inputs(side).bloodragePain; }
	int magicResistance(const Unit * unit) const override { return resistance.query(unit); }
	newHorizonsNecromancy::CombatPassives necromancyPassives(BattleSide side) const override { return inputs(side).necromancy; }
	newHorizonsNecromancy::CombatPassives filterNecromancyPassives(
		const newHorizonsNecromancy::CombatPassives & captured, CreatureID creature) const override
	{
		return newHorizonsNecromancy::passivesForFamily(captured,
			newHorizonsNecromancy::isSkeletonFamily(creature, facts.skeletonUpgrades));
	}
	const JsonNode & magicRules() const override { return rules(facts); }
};
OwnedUnitEnvironment::OwnedUnitEnvironment(std::vector<std::shared_ptr<const Unit>> units, Facts facts)
	: data(std::make_unique<Data>(std::move(units), std::move(facts))) {}
OwnedUnitEnvironment::~OwnedUnitEnvironment() = default;
ProjectedUnitEnvironmentState::SideState & OwnedUnitEnvironment::state(BattleSide side) { return data->state(side); }
bool OwnedUnitEnvironment::unitHasAmmoCart(const Unit * unit) const { return data->queries.unitHasAmmoCart(unit); }
PlayerColor OwnedUnitEnvironment::unitEffectiveOwner(const Unit * unit) const { return data->queries.unitEffectiveOwner(unit); }
std::optional<int> OwnedUnitEnvironment::unitMagicResistance(const Unit * unit) const { return data->queries.unitMagicResistance(unit); }
int OwnedUnitEnvironment::unitSylvanTacticsSpeed(const Unit * unit, int turn) const
{
	return data->queries.unitSylvanTacticsSpeed(unit, turn);
}
int OwnedUnitEnvironment::unitSylvanTacticsInitiativePercent(const Unit * unit) const
{
	return data->queries.unitSylvanTacticsInitiativePercent(unit);
}
int OwnedUnitEnvironment::unitFortuneSpeed(const Unit * unit) const { return data->queries.unitFortuneSpeed(unit); }
int OwnedUnitEnvironment::unitSpeedBonus(const Unit * unit) const { return data->queries.unitSpeedBonus(unit); }
bool OwnedUnitEnvironment::unitSlowAffectsSpeed(const Unit * unit) const { return data->queries.unitSlowAffectsSpeed(unit); }
int OwnedUnitEnvironment::unitAdditionalRetaliations(const Unit * unit) const { return data->queries.unitAdditionalRetaliations(unit); }
int OwnedUnitEnvironment::unitBloodragePainIncrement(const Unit * unit) const { return data->queries.unitBloodragePainIncrement(unit); }
bool OwnedUnitEnvironment::unitHasVeteranCohesion(const Unit * unit) const { return data->queries.unitHasVeteranCohesion(unit); }
std::optional<std::pair<int32_t, int32_t>> OwnedUnitEnvironment::unitMoraleLimits(const Unit * unit) const { return data->queries.unitMoraleLimits(unit); }
newHorizonsNecromancy::CombatPassives OwnedUnitEnvironment::unitNecromancyPassives(const Unit * unit) const
{
	return data->queries.unitNecromancyPassives(unit);
}
}
