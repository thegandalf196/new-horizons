/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitEnvironment.h"
#include "OwnedUnitEnvironmentPolicy.h"
#include "OwnedFrameBranchQueries.h"
#include "CommittedElementalEffects.h"
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
	std::optional<BattleSideArray<ProjectedUnitEnvironmentState::SideState>> policyStates;
	ProjectedBonusRoster roster;
	Resistance resistance;
	const ProjectedUnitEnvironmentState::SideInputs & inputs(BattleSide side) const
	{
		return OwnedUnitEnvironment::capturedInputs(facts, side);
	}
	const ProjectedUnitEnvironmentState::SideState & sideState(BattleSide side) const
	{
		if(policyStates) return policyStates->at(side);
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
		for(const auto & entry : facts.warfareStatBonuses)
			if(entry.second < 0) throw std::invalid_argument("Negative owned Warfare stat contribution");
		if(facts.enginePolicy) policyStates = OwnedUnitEnvironment::capturedStates(facts);
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			if(!facts.colors.at(side) || !knownAffiliation(*facts.colors.at(side))
				|| (facts.inputs.at(side) && facts.inputs.at(side)->color != *facts.colors.at(side))
				|| (facts.enginePolicy && inputs(side).color != *facts.colors.at(side)))
				throw std::invalid_argument("Owned side affiliation facts are inconsistent");
	}
	ProjectedUnitEnvironmentState::SideState & state(BattleSide side)
	{
		if(facts.enginePolicy) throw std::logic_error("Opaque production environment has no side-state export");
		if(!facts.states.at(side)) throw std::logic_error("Owned side state facts are unknown");
		return *facts.states.at(side);
	}
	bool legacyTimeStopMatch(const Unit & unit, SpellID spell) const
	{
		requireSubject(unit);
		if(!facts.legacyTimeStop) throw std::logic_error("Owned legacy Time Stop identity domain is unbound");
		const auto found = facts.legacyTimeStop->classified.find(spell);
		if(found == facts.legacyTimeStop->classified.end())
			throw std::invalid_argument("Spell identity is outside the captured owned clock domain");
		return found->second;
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
	newHorizonsElementalAttunement::Profile elementalAttunementProfile(BattleSide side) const override { return inputs(side).elementalAttunementProfile; }
	uint8_t elementalAttunementMask(BattleSide side) const override { return sideState(side).elementalAttunementMask; }
	bool knowsElementalAttunementProfile(BattleSide side) const override { return inputs(side).elementalAttunementProfileKnown; }
	std::optional<newHorizonsElementalAttunement::UnitPassives> committedElementalAttunement(const Unit * unit) const override
	{
		if(!unit) return std::nullopt;
		requireSubject(*unit);
		const auto side = unit->unitSide();
		if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return std::nullopt;
		if(knowsElementalAttunementProfile(side)) return std::nullopt;
		return OwnedUnitEnvironment::capturedElementalUnit(facts, *unit);
	}
	newHorizonsNecromancy::CombatPassives filterNecromancyPassives(
		const newHorizonsNecromancy::CombatPassives & captured, CreatureID creature) const override
	{
		auto result = newHorizonsNecromancy::passivesForFamily(captured,
			newHorizonsNecromancy::isSkeletonFamily(creature, facts.skeletonUpgrades));
		result.luckSource = newHorizonsNecromancy::CombatPassives::LuckSource{facts.necromancyLuckSource};
		return result;
	}
	const JsonNode & magicRules() const override { return rules(facts); }
	int warfareStats(const Unit & unit) const
	{
		requireSubject(unit);
		if(!unit.alive() || unit.isGhost()) return 0;
		const auto override = facts.warfareStatBonuses.find(unit.unitId());
		if(override != facts.warfareStatBonuses.end()) return override->second;
		if(!facts.enginePolicy) return 0;
		const auto found = facts.enginePolicy->warfareStatBonuses.find(unit.unitId());
		if(found == facts.enginePolicy->warfareStatBonuses.end())
			throw std::invalid_argument("Owned Warfare stats require the captured occurrence");
		return found->second;
	}
};
const ProjectedUnitEnvironmentState::SideInputs & OwnedUnitEnvironment::capturedInputs(const Facts & facts, BattleSide side)
{
	if(facts.enginePolicy) return facts.enginePolicy->inputs.at(side);
	if(!facts.inputs.at(side)) throw std::logic_error("Owned side policy facts are unknown");
	return *facts.inputs.at(side);
}
const BattleSideArray<ProjectedUnitEnvironmentState::SideState> & OwnedUnitEnvironment::capturedStates(const Facts & facts)
{
	if(!facts.enginePolicy) throw std::logic_error("Missing opaque engine environment owner");
	return facts.enginePolicy->states;
}
std::optional<newHorizonsElementalAttunement::UnitPassives> OwnedUnitEnvironment::capturedElementalUnit(
	const Facts & facts, const Unit & unit)
{
	// Public Facts/OrderedOccurrences constructors have no permission to reuse
	// numeric observations by ID. Only the sealed engine-owned policy does.
	if(!facts.enginePolicy)
		throw std::invalid_argument("Unknown hero effects require sealed current unit observations");
	if(!facts.enginePolicy->committedElementalEffects)
		throw std::invalid_argument("Missing admitted current elemental transition policy");
	return facts.enginePolicy->committedElementalEffects->evaluate(unit);
}
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
std::optional<bool> OwnedUnitEnvironment::unitLegacyTimeStopSpellMatch(const Unit * unit, SpellID spell) const
{
	if(!unit) throw std::invalid_argument("Owned legacy clock classifier requires its actual subject");
	return data->legacyTimeStopMatch(*unit, spell);
}
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
int OwnedUnitEnvironment::unitWarfareStatBonus(const Unit * unit) const { return unit ? data->warfareStats(*unit) : 0; }
std::shared_ptr<CUnitState> OwnedUnitEnvironment::acquireStateWithWarfareStatBonus(
	const std::shared_ptr<OwnedUnitBonusFrame> & frame, int value)
{
	if(!frame || value < 0) throw std::invalid_argument("Invalid owned Warfare stat loan");
	struct Loan
	{
		std::shared_ptr<OwnedUnitBonusFrame> frozen;
		OwnedUnitBonusFrame::Scratch scratch;
		std::unique_ptr<OwnedUnitEnvironment> environment;
		explicit Loan(const std::shared_ptr<OwnedUnitBonusFrame> & frame) : frozen(frame->fork()), scratch(frozen->scratch()) {}
	};
	auto loan = std::make_shared<Loan>(frame);
	const auto & source = dynamic_cast<const OwnedUnitEnvironment &>(OwnedFrameBranchQueries::environment(*loan->frozen));
	auto facts = source.data->facts;
	facts.warfareStatBonuses[loan->scratch.unit().unitId()] = value;
	// Both the descriptor/provider and roster are retained immutable copies.
	loan->environment = std::unique_ptr<OwnedUnitEnvironment>(new OwnedUnitEnvironment(
		source.data->units, std::move(facts), &loan->scratch.unit()));
	loan->scratch.unit().bindStateEnvironment(loan->environment.get());
	return std::shared_ptr<CUnitState>(loan, &loan->scratch.unit());
}
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

newHorizonsElementalAttunement::UnitPassives battle::OwnedUnitEnvironment::unitElementalAttunement(const Unit * unit) const
{
	return data->queries.unitElementalAttunement(unit);
}
