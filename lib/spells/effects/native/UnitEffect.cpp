/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitEffect.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsMagic.h"
#include "../../../battle/CBattleInfoCallback.h"
#include "../../../battle/Unit.h"

namespace spells::effects::native
{
bool UnitEffect::isValidTarget(const Mechanics *, const battle::Unit * unit) const
{
	return unit->isValidTarget(false);
}

bool UnitEffect::isReceptive(const Mechanics * mechanics, const battle::Unit * unit) const
{
	if(unit->isInvincible() && mechanics->isNegativeSpell()) return false;
	if(parameters["ignoreImmunity"].Bool()) return !unit->hasAbsoluteImmunity(mechanics->getSpellId());
	return mechanics->isReceptive(unit);
}

bool UnitEffect::applicableGeneral(Problem & problem, const Mechanics * mechanics) const
{
	const auto found = mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
	{
		return isValidTarget(mechanics, unit) && isReceptive(mechanics, unit)
			&& (!mechanics->isSmart() || mechanics->ownerMatches(unit));
	});
	if(!found.empty()) return true;
	mechanics->adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	return false;
}

bool UnitEffect::applicableTarget(Problem &, const Mechanics * mechanics, const Target & target) const
{
	for(const auto & destination : target)
		if(destination.unitValue && isValidTarget(mechanics, destination.unitValue)
			&& (!mechanics->isSmart() || mechanics->ownerMatches(destination.unitValue)))
			return true;
	return false;
}

void UnitEffect::adjustAffectedHexes(BattleHexArray & hexes, const Mechanics *, const Target & target) const
{
	for(const auto & destination : target)
		if(destination.hexValue.isValid()) hexes.insert(destination.hexValue);
}

Target UnitEffect::filterTarget(const Mechanics * mechanics, const Target & target) const
{
	Target result;
	for(const auto & destination : target)
		if(destination.unitValue && isValidTarget(mechanics, destination.unitValue) && isReceptive(mechanics, destination.unitValue))
			result.push_back(destination);
	return result;
}

Target UnitEffect::transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const
{
	const int chainLength = mechanics->getEffectiveChainLength(static_cast<int>(parameters["chainLength"].Integer()));
	return chainLength > 1 ? transformByChain(mechanics, aim, target, chainLength) : transformByRange(mechanics, aim, target);
}

Target UnitEffect::transformByRange(const Mechanics * mechanics, const Target & aim, const Target & target) const
{
	const auto protectedCenter = [&](const battle::Unit * unit)
	{
		return !aim.empty() && newHorizonsMagic::isProtectedAreaCenter(*mechanics->battle(), mechanics->getHeroCaster(),
			mechanics->getSpellId(), *unit, aim.front().hexValue, mechanics->getCasterSide());
	};
	const auto eligible = [&](const battle::Unit * unit)
	{
		return !protectedCenter(unit) && isValidTarget(mechanics, unit) && isReceptive(mechanics, unit)
			&& (!mechanics->isSmart() || mechanics->ownerMatches(unit));
	};
	Target result;
	std::unordered_set<uint32_t> seen;
	const auto add = [&](const battle::Unit * unit)
	{
		if(seen.insert(unit->unitId()).second) result.emplace_back(unit);
	};
	if(mechanics->isMassive())
	{
		for(const auto * unit : mechanics->battle()->battleGetUnitsIf(eligible)) add(unit);
	}
	else
	{
		const auto & effective = !aim.empty() && target.empty() ? Target{aim.front()} : target;
		for(const auto & destination : effective)
		{
			if(destination.unitValue)
			{
				if(eligible(destination.unitValue)) add(destination.unitValue);
			}
			else if(destination.hexValue.isValid())
			{
				const auto onTile = mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
				{
					return unit->coversPos(destination.hexValue) && eligible(unit);
				});
				const auto living = std::find_if(onTile.begin(), onTile.end(), [](const battle::Unit * unit) { return unit->alive(); });
				if(living != onTile.end()) add(*living);
				else if(!onTile.empty()) add(onTile.front());
			}
		}
	}
	if(mechanics->alwaysHitFirstTarget() && !aim.empty() && aim.front().unitValue && !protectedCenter(aim.front().unitValue))
		add(aim.front().unitValue);
	return result;
}

Target UnitEffect::transformByChain(const Mechanics * mechanics, const Target & aim, const Target & target, int chainLength) const
{
	const auto byRange = transformByRange(mechanics, aim, target);
	if(byRange.empty() || !byRange.front().hexValue.isValid()) return {};
	Target result;
	std::unordered_set<uint32_t> processed;
	const auto eligible = [&](const battle::Unit * unit) { return isReceptive(mechanics, unit) && isValidTarget(mechanics, unit); };
	const auto possibleHexes = [&]() -> std::optional<BattleHexArray>
	{
		const auto units = mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
		{
			return eligible(unit) && !processed.contains(unit->unitId());
		});
		if(units.empty()) return std::nullopt;
		BattleHexArray hexes = units.front()->getHexes();
		for(size_t i = 1; i < units.size(); ++i) hexes.insert(units[i]->getHexes());
		return hexes;
	};
	BattleHex destination = byRange.front().hexValue;
	const bool alwaysFirst = mechanics->alwaysHitFirstTarget();
	int index = 0;
	while(index < chainLength)
	{
		const auto * unit = mechanics->battle()->battleGetUnitByPos(destination, true);
		if(!unit) break;
		const bool resists = mechanics->wouldResist(unit);
		if(resists) mechanics->recordResistanceShare(this, unit, static_cast<size_t>(index));
		if(alwaysFirst && index == 0) result.emplace_back(unit);
		if(resists && index == 0)
		{
			if(!alwaysFirst) result.emplace_back(unit);
			break;
		}
		bool advances = true;
		if(eligible(unit) && !resists)
		{
			if(!(alwaysFirst && index == 0)) result.emplace_back(unit);
		}
		else if(eligible(unit) && resists) advances = false;
		else result.emplace_back();
		processed.insert(unit->unitId());
		const auto hexes = possibleHexes();
		if(!hexes) break;
		destination = BattleHex::getClosestTile(unit->unitSide(), destination, *hexes);
		if(advances) ++index;
	}
	return result;
}
}
