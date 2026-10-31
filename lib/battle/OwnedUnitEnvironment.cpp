/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitEnvironment.h"
#include "UnitOwnershipQuery.h"
#include "UnitMagicResistanceQuery.h"
#include "CUnitState.h"
#include "../spells/NewHorizonsMagic.h"
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
	friend class OwnedUnitEnvironment;
	class Resistance final : public UnitMagicResistanceQuery
	{
		const Data & data;
	public:
		explicit Resistance(const Data & data) : data(data) {}
		void requireSubject(const Unit & unit) const override { data.requireSubject(unit); }
		Units adjacentUnits(const Unit & unit) const override
		{
			return OwnedUnitEnvironment::adjacentInRoster(data.roster, unit);
		}
		bool newHorizonsRulesActive() const override { return newHorizonsMagic::rulesActive(data.magicRules()); }
	};
	// Retire queries/context BEFORE their captured metadata/roster/unit owners.
	std::vector<std::shared_ptr<const Unit>> units;
	const Unit * derived; // Borrowed ONLY by the retaining Scratch loan; never a native object.
	Facts facts;
	ProjectedBonusRoster roster;
	Resistance resistance;
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
	Data(std::vector<std::shared_ptr<const Unit>> values, Facts metadata, const Unit * subject)
		: units(std::move(values)), derived(subject), facts(std::move(metadata)),
		roster(rawUnits(units), ProjectedBonusRoster::Identity::OrderedOccurrences),
		resistance(*this), queries(*this)
	{
		(void)rules(facts); // Retain the original required-rules construction admission.
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
	std::pair<int32_t, int32_t> legacyMoraleLimits() const
	{
		if(!facts.legacyMoraleLimits) throw std::logic_error("Owned legacy morale limits are unknown");
		const auto limits = *facts.legacyMoraleLimits;
		if(limits.first > 0 || limits.second < 0)
			throw std::invalid_argument("Owned legacy morale limits are invalid");
		return limits;
	}
	std::pair<int32_t, int32_t> legacyLuckLimits() const
	{
		if(!facts.legacyLuckLimits) throw std::logic_error("Owned legacy luck limits are unknown");
		const auto limits = *facts.legacyLuckLimits;
		if(limits.first > 0 || limits.second < 0)
			throw std::invalid_argument("Owned legacy luck limits are invalid");
		return limits;
	}
	bool available() const override { return true; }
	void requireSubject(const Unit & unit) const override
	{
		if(!roster.contains(unit) && &unit != derived) throw std::invalid_argument("Owned environment subject is outside its bound occurrences");
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
		auto result = newHorizonsNecromancy::passivesForFamily(captured,
			newHorizonsNecromancy::isSkeletonFamily(creature, facts.skeletonUpgrades));
		result.luckSource = newHorizonsNecromancy::CombatPassives::LuckSource{facts.necromancyLuckSource};
		return result;
	}
	const JsonNode & magicRules() const override { return rules(facts); }
};
OwnedUnitEnvironment::OwnedUnitEnvironment(std::vector<std::shared_ptr<const Unit>> units, Facts facts)
	: OwnedUnitEnvironment(std::move(units), std::move(facts), nullptr) {}
OwnedUnitEnvironment::OwnedUnitEnvironment(std::vector<std::shared_ptr<const Unit>> units, Facts facts, const Unit * derived)
	: data(std::make_unique<Data>(std::move(units), std::move(facts), derived)) {}
Units OwnedUnitEnvironment::adjacentInRoster(const ProjectedBonusRoster & roster, const Unit & geometry)
{
	const auto adjacent = roster.adjacentUnitsAt(geometry); // Same canonical geometry loop, exact original roster/order.
	return Units(adjacent.begin(), adjacent.end());
}
std::unique_ptr<OwnedUnitEnvironment> OwnedUnitEnvironment::forDerivedSubject(
	const Unit & source, const CUnitStateDetached & derived, const IUnitInfo & info) const
{
	const auto * original = dynamic_cast<const CUnitStateDetached *>(&source);
	if(!data->roster.contains(source) || !original || !original->usesUnitInfo(info)
		|| !derived.usesUnitInfo(info) || !derived.hasBonusProviderAncestor(source))
		throw std::invalid_argument("Derived environment requires its exact retained source/info/provider");
	return std::unique_ptr<OwnedUnitEnvironment>(new OwnedUnitEnvironment(data->units, data->facts, &derived));
}
Units OwnedUnitEnvironment::adjacentGeometry(const Unit & geometry) const { return adjacentInRoster(data->roster, geometry); }
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
std::optional<std::pair<int32_t, int32_t>> OwnedUnitEnvironment::unitMoraleLimits(const Unit * unit) const
{
	const auto captured = data->queries.unitMoraleLimits(unit); // Canonical rules/subject admission first.
	if(captured || !unit) return captured;
	return data->legacyMoraleLimits(); // No native engine-settings fallback after release.
}
std::optional<std::pair<int32_t, int32_t>> OwnedUnitEnvironment::unitLuckLimits(const Unit * unit) const
{
	if(!unit) return std::nullopt;
	data->requireSubject(*unit);
	return data->legacyLuckLimits();
}
newHorizonsNecromancy::CombatPassives OwnedUnitEnvironment::unitNecromancyPassives(const Unit * unit) const
{
	return data->queries.unitNecromancyPassives(unit);
}
}
