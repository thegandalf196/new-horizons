/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ScopedStackEffectQueryData.h"
#include "BattleInfo.h"
#include "BattleAccessibilityQuery.h"
#include "LiveBattleAccessibilityContext.h"
#include "UnitMagicResistanceQuery.h"
#include "../spells/NewHorizonsMagic.h"
#include "StackFormSourceIdentity.h"
#include "IBattleState.h"
#include "../CStack.h"
#include <algorithm>
#include <stdexcept>

namespace battle
{
struct ScopedStackEffectQueries::Data::Context final : UnitEnvironmentContext
{
	const Data & data;
	LiveUnitEnvironmentContext live;
	Context(const Data & data) : data(data), live(data.battle) {}
	bool available() const override { return live.available(); }
	void requireSubject(const Unit & unit) const override
	{
		if(data.roster->contains(unit)) return;
		if(const auto * derived = dynamic_cast<const CUnitStateDetached *>(&unit))
			for(const auto & [native, row] : data.rows)
				if(derived->hasUnitInfoAncestor(row->state) && derived->hasBonusProviderAncestor(row->state))
					return; // Exact retained acquireState ancestry, never ID-only admission.
		throw std::invalid_argument("Scoped environment subject is outside its retained ancestry");
	}
	const UnitOwnershipQuery & ownership() const override { return live.ownership(); }
	Units unitsIf(const UnitFilter & predicate) const override
	{
		Units result;
		for(const auto * unit : data.roster->orderedUnits())
			if(predicate(unit)) result.push_back(unit);
		return result;
	}
	bool sideHasAmmoArtifact(BattleSide side) const override { return live.sideHasAmmoArtifact(side); }
	bool sideHasVeteranCohesion(BattleSide side) const override { return live.sideHasVeteranCohesion(side); }
	SylvanLuckState fortune(BattleSide side) const override { return live.fortune(side); }
	int bloodrageDamage(BattleSide side) const override { return live.bloodrageDamage(side); }
	int bloodrageRank(BattleSide side) const override { return live.bloodrageRank(side); }
	int bloodrageCap(BattleSide side) const override { return live.bloodrageCap(side); }
	int bloodrageSpeed(BattleSide side) const override { return live.bloodrageSpeed(side); }
	int bloodrageRetaliations(BattleSide side) const override { return live.bloodrageRetaliations(side); }
	int bloodragePain(BattleSide side) const override { return live.bloodragePain(side); }
	int magicResistance(const Unit * unit) const override
	{
		struct Query final : UnitMagicResistanceQuery
		{
			const Context & context;
			explicit Query(const Context & context) : context(context) {}
			void requireSubject(const Unit & subject) const override { context.requireSubject(subject); }
			Units adjacentUnits(const Unit & subject) const override
			{
				return ScopedStackEffectQueries::adjacentGeometry(*context.data.roster, subject);
			}
			bool newHorizonsRulesActive() const override { return newHorizonsMagic::rulesActive(context.magicRules()); }
		};
		return Query(*this).query(unit);
	}
	newHorizonsNecromancy::CombatPassives necromancyPassives(BattleSide side) const override { return live.necromancyPassives(side); }
	const JsonNode & magicRules() const override { return live.magicRules(); }
};

ScopedStackEffectQueries::Data::Row::Row(Data & data, const CStack & source)
	: source(source), view(data, *this), state(&source, &view)
{
	const auto receipt = stackFormSourceIdentity(source);
	baseCreature = receipt.baseCreature;
	attached = receipt.attached;
	state = static_cast<const CUnitState &>(source);
	initializing = false;
}

ScopedStackEffectQueries::Data::Data(const BattleInfo & battle, const std::vector<const CStack *> & requested)
	: battle(battle)
{
	const auto nativeUnits = battle.getUnitsIf([](const Unit *) { return true; });
	std::vector<const Unit *> projected;
	for(const auto * unit : nativeUnits)
	{
		const auto * source = dynamic_cast<const CStack *>(unit);
		if(!source || source->getBattle() != &battle)
			throw std::invalid_argument("Scoped effects require exact native battle occurrences");
		auto [entry, inserted] = rows.try_emplace(source);
		if(inserted) entry->second = std::make_unique<Row>(*this, *source);
		projected.push_back(&entry->second->state);
	}
	for(const auto * source : requested)
	{
		const auto found = rows.find(source);
		if(found == rows.end())
			throw std::invalid_argument("Foreign scoped stack-effect target");
		auto [entry, inserted] = targets.try_emplace(source->unitId(), found->second.get());
		if(!inserted && entry->second != found->second.get())
			throw std::invalid_argument("Ambiguous scoped stack-effect target occurrence");
	}
	roster = std::make_unique<ProjectedBonusRoster>(std::move(projected), ProjectedBonusRoster::Identity::OrderedOccurrences);
	context = std::make_unique<Context>(*this);
	environment = std::make_unique<UnitEnvironmentQueries>(*context);
	for(const auto & [source, row] : rows)
		row->state.bindStateEnvironment(environment.get());
}
ScopedStackEffectQueries::Data::~Data() = default;

Units ScopedStackEffectQueries::adjacentGeometry(const ProjectedBonusRoster & roster, const Unit & geometry)
{
	const auto adjacent = roster.adjacentUnitsAt(geometry);
	return Units(adjacent.begin(), adjacent.end());
}

AccessibilityInfo ScopedStackEffectQueries::Data::accessibility(uint32_t id) const
{
	struct Context final : BattleAccessibilityContext
	{
		const Data & data;
		LiveBattleAccessibilityContext live;
		explicit Context(const Data & data) : data(data), live(data.battle) {}
		BattleHexArray impassableTiles() const override { return live.impassableTiles(); }
		bool hasWalls() const override { return live.hasWalls(); }
		EGateState gateState() const override { return live.gateState(); }
		Units aliveUnits() const override
		{
			Units result;
			for(const auto * unit : data.roster->orderedUnits())
				if(unit->isValidTarget(false)) result.push_back(unit);
			return result;
		}
		std::vector<BattleHexArray> blockedObstacles() const override { return live.blockedObstacles(); }
		EWallState wallState(EWallPart part) const override { return live.wallState(part); }
		std::vector<PendingDemonicGateFootprint> pendingGates(BattleSide side) const override { return live.pendingGates(side); }
	};
	return withAccessibleHexes(buildBattleAccessibility(Context(*this)), target(id).state.getHexes());
}
}
