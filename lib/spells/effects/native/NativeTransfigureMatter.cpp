/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"
#include "StructureCommon.h"
#include "../../NewHorizonsPlacementPower.h"
#include <cmath>

namespace spells::effects::native
{
namespace
{
class TransfigureMatter final : public SummonEffect
{
	const Creature * type() const { return nativeEvents::creature(parameters["id"].String()); }
	static bool valid(const std::shared_ptr<const CObstacleInstance> & obstacle)
	{ return obstacle && obstacle->obstacleType == CObstacleInstance::USUAL && !obstacle->getAffectedTiles().empty(); }
	structure::Obstacles obstacles(const Mechanics * mechanics, const Target & target, bool all) const
	{
		structure::Obstacles result;
		const auto add = [&](const auto & obstacle)
		{ if(valid(obstacle) && std::find(result.begin(), result.end(), obstacle) == result.end()) result.push_back(obstacle); };
		if(all)
			for(const auto & obstacle : mechanics->battle()->battleGetAllObstacles()) add(obstacle);
		else
			for(const auto & destination : target)
				if(destination.hexValue.isValid())
					for(const auto & obstacle : mechanics->battle()->battleGetAllObstacles())
						if(valid(obstacle) && obstacle->getAffectedTiles().contains(destination.hexValue)) add(obstacle);
		return result;
	}
	bool clear(const Mechanics * mechanics, const CObstacleInstance & selected, BattleHex hex) const
	{
		const auto & battle = *mechanics->battle();
		if(!hex.isAvailable() || battle.battleGetUnitByPos(hex, true)) return false;
		for(const auto & other : battle.battleGetAllObstacles())
		{
			if(other->obstacleType == CObstacleInstance::MOAT)
			{ if(other->pos == hex) return false; }
			else if(other->obstacleType == CObstacleInstance::USUAL || other->obstacleType == CObstacleInstance::ABSOLUTE_OBSTACLE
				|| other->obstacleType == CObstacleInstance::SPELL_CREATED)
			{
				const bool isSelected = other->pos == selected.pos && other->obstacleType == selected.obstacleType;
				if(other->getAffectedTiles().contains(hex) && !isSelected) return false;
			}
		}
		return structure::clearOfWalls(battle, hex);
	}
	BattleHex placement(const Mechanics * mechanics, const CObstacleInstance & obstacle) const
	{
		for(const auto front : obstacle.getAffectedTiles())
		{
			const auto rear = mechanics->getCasterSide() == BattleSide::ATTACKER ? front.copyToWest() : front.copyToEast();
			if(clear(mechanics, obstacle, front) && (!type()->isDoubleWide() || clear(mechanics, obstacle, rear))) return front;
		}
		return BattleHex::INVALID;
	}
	int64_t pool(const Mechanics * mechanics, const CObstacleInstance & obstacle) const
	{
		return mechanics->adjustMetamagicMagnitude(newHorizonsMagic::transfigureHitPoints(mechanics->getEffectPower(),
			mechanics->getSpellPowerCoefficientBasisPoints(), mechanics->getEmpowerSpellBonusPercent(), static_cast<int32_t>(obstacle.getAffectedTiles().size()),
			summon::perk(mechanics, "new-horizons:sorceryMagic", "new-horizons:sorceryMagic.matterShaper")), nullptr);
	}
	int64_t maximum(const Mechanics * mechanics) const
	{
		double health = type()->getMaxHealth();
		if(const auto * hero = mechanics->getHeroCaster()) health += hero->valOfBonuses(BonusType::STACK_HEALTH);
		return static_cast<int64_t>(std::max(1.0, health));
	}
	int64_t count(const Mechanics * mechanics, int64_t health) const
	{ return static_cast<int64_t>(std::ceil(static_cast<double>(health) / maximum(mechanics))); }
public:
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		for(const auto & obstacle : obstacles(mechanics, {}, true)) if(placement(mechanics, *obstacle).isValid()) return true;
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		for(const auto & obstacle : obstacles(mechanics, target, false)) if(placement(mechanics, *obstacle).isValid()) return true;
		return status::reject(mechanics, &problem);
	}
	Target transformTarget(const Mechanics * mechanics, const Target &, const Target & target) const override
	{
		Target result;
		for(const auto & obstacle : obstacles(mechanics, target, false))
		{
			const auto position = placement(mechanics, *obstacle);
			if(position.isValid()) result.emplace_back(position);
		}
		return result;
	}
	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & target) const override
	{
		for(const auto & obstacle : obstacles(mechanics, target, false))
			for(const auto hex : obstacle->getAffectedTiles()) hexes.insert(hex);
	}
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		const auto candidates = obstacles(mechanics, target, false);
		if(candidates.empty()) return {};
		const auto health = pool(mechanics, *candidates.front());
		return summon::value(health, count(mechanics, health), type());
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const auto candidates = obstacles(mechanics, target, false);
		if(candidates.empty()) return;
		const auto & obstacle = candidates.front();
		const auto position = placement(mechanics, *obstacle);
		if(!position.isValid()) return;
		const auto health = pool(mechanics, *obstacle), amount = count(mechanics, health);
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		structure::removeObstacle(context, obstacle);
		battle::UnitInfo info;
		info.count = amount; info.type = type()->getId(); info.side = mechanics->getCasterSide(); info.position = position; info.summoned = true;
		if(const auto * unit = nativeEvents::addUnit(context, info))
		{
			auto excess = unit->getAvailableHealth() - health;
			if(excess <= 0) return;
			auto copy = unit->acquireState(); copy->damage(excess);
			nativeEvents::changeUnit(context, *copy);
		}
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createTransfigureMatterEffect(const std::string & path)
{
	if(path == "spells/transfigureMatter") return std::make_shared<spells::effects::native::TransfigureMatter>();
	return {};
}
}
