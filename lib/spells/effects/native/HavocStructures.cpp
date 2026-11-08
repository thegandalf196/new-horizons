/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Damage.h"
#include "Catapult.h"
#include "StructureCommon.h"
#include "../../ISpellMechanics.h"
#include "../../CSpell.h"
#include "../../../GameLibrary.h"
#include "../../../scripting/ScriptTypeDescription.h"
#include "../../../texts/MetaString.h"

namespace spells::effects::native
{
namespace
{
BattleHexArray affectedHexes(const Target & target)
{
	BattleHexArray result;
	for(const auto & destination : target)
		if(!destination.unitValue && destination.hexValue.isValid()) result.insert(destination.hexValue);
	return result;
}

std::pair<structure::Obstacles, std::vector<EWallPart>> structuralTargets(const Mechanics & mechanics, const Target & target, bool all)
{
	const auto affected = affectedHexes(target);
	std::pair<structure::Obstacles, std::vector<EWallPart>> result;
	const auto & battle = *mechanics.battle();
	const bool magical = mechanics.destroysNewHorizonsHavocMagicalObstacles();
	for(const auto & obstacle : battle.battleGetAllObstacles())
	{
		bool ordinary = obstacle->obstacleType == CObstacleInstance::USUAL;
		if(magical && obstacle->obstacleType == CObstacleInstance::SPELL_CREATED)
		{
			const auto * creator = SpellID(obstacle->ID).toEntity(LIBRARY);
			ordinary = creator && creator->isCombat() && !creator->isSpecial() && !creator->isCreatureAbility();
		}
		if(!ordinary) continue;
		bool intersects = all;
		if(!intersects) for(const auto hex : obstacle->getAffectedTiles()) if(affected.contains(hex)) { intersects = true; break; }
		if(intersects) result.first.push_back(obstacle);
	}
	if(!battle.hasFortifications() || mechanics.getNewHorizonsHavocStructuralDamage() <= 0) return result;
	for(const auto part : structure::sections)
		if(battle.isWallPartAttackable(part) && battle.getWallStructuralHP(part) > 0)
			if(const auto hex = battle.wallPartToBattleHex(part); hex.isValid() && (all || affected.contains(hex))) result.second.push_back(part);
	return result;
}

class HavocStructures final : public Damage
{
public:
	ForecastImplementation forecastImplementation(const Mechanics *) const override { return ForecastImplementation::UNKNOWN; }
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(mechanics->usesNewHorizonsHavocStructures())
		{
			const auto [obstacles, sections] = structuralTargets(*mechanics, {}, true);
			if(!obstacles.empty() || !sections.empty()) return true;
		}
		return Damage::applicableGeneral(problem, mechanics);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		if(!mechanics->usesNewHorizonsHavocStructures()) return Damage::applicableTarget(problem, mechanics, target);
		if(Damage::applicableTarget(problem, mechanics, target)) return true;
		const auto [obstacles, sections] = structuralTargets(*mechanics, target, mechanics->getSpell()->getJsonKey() == "core:armageddon");
		return !obstacles.empty() || !sections.empty();
	}
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		auto result = Damage::transformTarget(mechanics, aim, target);
		if(!mechanics->usesNewHorizonsHavocStructures() || mechanics->getSpell()->getJsonKey() != "core:meteorShower") return result;
		auto seen = affectedHexes(result);
		const auto add = [&](const Target & destinations)
		{
			for(const auto & destination : destinations)
				if(!destination.unitValue && destination.hexValue.isValid() && !seen.contains(destination.hexValue))
				{
					seen.insert(destination.hexValue);
					result.emplace_back(destination.hexValue);
				}
		};
		add(aim);
		add(target);
		return result;
	}
	Target filterTarget(const Mechanics * mechanics, const Target & target) const override
	{
		if(!mechanics->usesNewHorizonsHavocStructures()) return Damage::filterTarget(mechanics, target);
		Target result;
		for(const auto & destination : target)
		{
			if(destination.unitValue)
			{
				if(isValidTarget(mechanics, destination.unitValue) && isReceptive(mechanics, destination.unitValue)) result.push_back(destination);
			}
			else if(destination.hexValue.isValid()) result.push_back(destination);
		}
		return result;
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		Damage::apply(server, mechanics, target);
		if(!mechanics->usesNewHorizonsHavocStructures()) return;
		const bool all = mechanics->getSpell()->getJsonKey() == "core:armageddon";
		const auto [obstacles, sections] = structuralTargets(*mechanics, target, all);
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		for(const auto & obstacle : obstacles) structure::removeObstacle(context, obstacle);
		int damagedSections = 0;
		int64_t totalDamage = 0;
		const auto damage = mechanics->getNewHorizonsHavocStructuralDamage();
		for(const auto part : sections)
		{
			const auto before = context.battle.getWallStructuralHP(part);
			structure::damageFortification(context, part, damage);
			const auto actual = std::max(0, before - context.battle.getWallStructuralHP(part));
			if(actual > 0) { ++damagedSections; totalDamage += actual; }
		}
		if(server->describeChanges() && (!obstacles.empty() || totalDamage > 0))
		{
			MetaString message;
			message.appendRawString(std::string(all ? "Armageddon" : "Meteor Shower") + " destroyed " + std::to_string(obstacles.size())
				+ " ordinary physical or magical battlefield obstacle(s) and dealt " + std::to_string(totalDamage)
				+ " structural damage across " + std::to_string(damagedSections) + " fortification section(s).");
			nativeEvents::log(context, message);
		}
	}
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createEarthquakeEffect(const ScriptTypeDescription &);
std::shared_ptr<spells::effects::Effect> createStructureEffect(const ScriptTypeDescription & description)
{
	using namespace spells::effects::native;
	if(description.sourcePath == "spells/catapult") return std::make_shared<Catapult>();
	if(description.sourcePath == "spells/havocStructures") return std::make_shared<HavocStructures>();
	return createEarthquakeEffect(description);
}
}
