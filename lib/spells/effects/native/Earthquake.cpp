/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Damage.h"
#include "Catapult.h"
#include "StructureCommon.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsPlacementPower.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../scripting/ScriptTypeDescription.h"

namespace spells::effects::native
{
namespace
{
bool geomancer(const Mechanics & mechanics)
{
	const auto * hero = mechanics.getHeroCaster();
	return hero && hero->hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.geomancer");
}

std::vector<EWallPart> plannedSections(const Mechanics & mechanics, BattleHex center)
{
	const auto & battle = *mechanics.battle();
	if(!center.isValid() || !battle.hasFortifications()) return {};
	const auto selected = battle.battleHexToWallPart(center);
	if(selected == EWallPart::INVALID || !battle.isWallPartAttackable(selected)) return {};
	const auto selectedHex = battle.wallPartToBattleHex(selected);
	if(!selectedHex.isValid()) return {};
	const auto distances = structure::distances(selectedHex);
	std::vector<std::pair<int, EWallPart>> candidates;
	for(const auto part : structure::sections)
		if(battle.isWallPartAttackable(part))
			if(const auto hex = battle.wallPartToBattleHex(part); hex.isValid()) candidates.emplace_back(distances[hex.toInt()], part);
	std::sort(candidates.begin(), candidates.end());
	const auto count = std::max(1, mechanics.getNewHorizonsEarthquakeSectionCount());
	std::vector<EWallPart> result{selected};
	for(const auto & [distance, part] : candidates)
	{
		if(static_cast<int>(result.size()) >= count) break;
		if(part != selected) result.push_back(part);
	}
	return result;
}

class Earthquake final : public Damage
{
	Catapult legacy;
protected:
	void initImpl(JsonNode data) override
	{
		legacy.init(data);
		Damage::initImpl(std::move(data));
	}
public:
	ForecastImplementation forecastImplementation(const Mechanics *) const override { return ForecastImplementation::UNKNOWN; }
	void adjustTargetTypes(std::vector<TargetType> & types, const Mechanics * mechanics) const override
	{
		if(mechanics->usesNewHorizonsEarthquake()) types = {AimType::LOCATION};
		else legacy.adjustTargetTypes(types, mechanics);
	}
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(!mechanics->usesNewHorizonsEarthquake()) return legacy.applicableGeneral(problem, mechanics);
		if(!mechanics->battle()->hasFortifications()) return true;
		for(const auto part : structure::sections) if(mechanics->battle()->isWallPartAttackable(part)) return true;
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		if(!mechanics->usesNewHorizonsEarthquake()) return legacy.applicableTarget(problem, mechanics, target);
		const auto center = structure::firstHex(target);
		if(!center.isAvailable() || (mechanics->battle()->hasFortifications() && plannedSections(*mechanics, center).empty()))
			return status::reject(mechanics, &problem);
		return true;
	}
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		if(!mechanics->usesNewHorizonsEarthquake()) return legacy.transformTarget(mechanics, aim, target);
		auto center = structure::firstHex(aim);
		if(!center.isValid()) center = structure::firstHex(target);
		if(!center.isValid()) return {};
		Target result{battle::Destination(center)};
		if(mechanics->battle()->hasFortifications()) return result;
		const auto area = structure::area(center, mechanics->getNewHorizonsEarthquakeParameter("radius"));
		for(const auto * unit : mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
		{
			if(!unit->alive() || !unit->isValidTarget(false) || unit->hasBonusOfType(BonusType::FLYING) || !Damage::isReceptive(mechanics, unit)) return false;
			for(const auto hex : unit->getHexes()) if(area.contains(hex)) return true;
			return false;
		})) result.emplace_back(unit);
		return result;
	}
	Target filterTarget(const Mechanics * mechanics, const Target & target) const override
	{
		if(!mechanics->usesNewHorizonsEarthquake()) return legacy.filterTarget(mechanics, target);
		Target result;
		bool anchored = false;
		for(const auto & destination : target)
		{
			const auto * unit = destination.unitValue;
			if(!unit)
			{
				if(!anchored && destination.hexValue.isValid())
				{
					result.emplace_back(destination.hexValue);
					anchored = true;
				}
			}
			else if(unit->alive() && unit->isValidTarget(false) && !unit->hasBonusOfType(BonusType::FLYING))
			{
				const auto filtered = Damage::filterTarget(mechanics, {destination});
				result.insert(result.end(), filtered.begin(), filtered.end());
			}
		}
		return result;
	}
	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & target) const override
	{
		if(!mechanics->usesNewHorizonsEarthquake()) return legacy.adjustAffectedHexes(hexes, mechanics, target);
		const auto center = structure::firstHex(target);
		if(!center.isValid()) return;
		if(mechanics->battle()->hasFortifications())
		{
			for(const auto part : plannedSections(*mechanics, center))
				if(const auto hex = mechanics->battle()->wallPartToBattleHex(part); hex.isValid()) hexes.insert(hex);
		}
		else for(const auto hex : structure::area(center, mechanics->getNewHorizonsEarthquakeParameter("radius")))
			if(hex.isAvailable()) hexes.insert(hex);
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(!mechanics->usesNewHorizonsEarthquake()) return legacy.apply(server, mechanics, target);
		const auto center = structure::firstHex(target);
		if(!center.isValid()) return;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		if(mechanics->battle()->hasFortifications())
		{
			const auto damage = mechanics->scaleHeroLuckDamage(mechanics->adjustMetamagicMagnitude(
				newHorizonsMagic::earthquakeStructuralDamage(mechanics->getNewHorizonsEarthquakeParameter("structuralDamage"), geomancer(*mechanics)), nullptr));
			for(const auto part : plannedSections(*mechanics, center)) structure::damageFortification(context, part, damage);
			return;
		}
		const auto affected = structure::area(center, mechanics->getNewHorizonsEarthquakeParameter("radius"));
		Damage::apply(server, mechanics, target);
		const auto duration = mechanics->adjustEffectDuration(mechanics->getNewHorizonsEarthquakeParameter("duration") + (geomancer(*mechanics) ? 1 : 0));
		for(const auto hex : affected)
		{
			if(!hex.isAvailable()) continue;
			auto obstacle = structure::obstacle(*mechanics, hex);
			obstacle.turnsRemaining = duration;
			obstacle.movementCost = mechanics->getNewHorizonsEarthquakeParameter("movementCost");
			obstacle.passable = true;
			obstacle.animation = AnimationPath::builtin(nativeEvents::string(parameters, "animation", "C17SPE1"));
			obstacle.customSize.insert(hex);
			structure::addObstacle(context, std::move(obstacle));
		}
	}
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		if(!mechanics->usesNewHorizonsEarthquake()) return legacy.getHealthChange(mechanics, target);
		Target units;
		for(const auto & destination : target)
			if(const auto * unit = destination.unitValue; unit && unit->alive() && unit->isValidTarget(false) && !unit->hasBonusOfType(BonusType::FLYING)) units.push_back(destination);
		return Damage::getHealthChange(mechanics, units);
	}
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createEarthquakeEffect(const ScriptTypeDescription & description)
{
	if(description.sourcePath == "spells/earthquake") return std::make_shared<spells::effects::native::Earthquake>();
	return {};
}
}
