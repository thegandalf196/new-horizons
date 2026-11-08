/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SummonHelpers.h"
#include "../../NewHorizonsBlink.h"
#include "../../../battle/AccessibilityInfo.h"
#include "../../../texts/MetaString.h"
#include <vstd/RNG.h>

namespace spells::effects::native
{
namespace
{
class Clone final : public UnitEffect
{
	bool isValidTarget(const Mechanics *, const battle::Unit * unit) const override
	{ return !unit->isClone() && !unit->hasClone() && unit->isValidTarget(false); }
	bool isReceptive(const Mechanics * mechanics, const battle::Unit * unit) const override
	{ return summon::creature(unit)->getLevel() <= parameters["maxTier"].Float() && UnitEffect::isReceptive(mechanics, unit); }
public:
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		for(const auto & destination : target)
		{
			const auto * unit = destination.unitValue;
			if(!unit || unit->getCount() < 1) continue;
			const auto hex = context.battle.getAvailableHex(summon::creature(unit), mechanics->getCasterSide(), unit->getPosition());
			if(!hex.isValid()) break;
			battle::UnitInfo info;
			info.count = unit->getCount(); info.type = unit->creatureId(); info.side = mechanics->getCasterSide();
			info.position = hex; info.summoned = true;
			const auto * clone = nativeEvents::addUnit(context, info);
			if(!clone) break;
			auto cloneState = clone->acquireState();
			cloneState->cloned = true;
			nativeEvents::changeUnit(context, *cloneState);
			auto original = unit->acquireState();
			original->cloneID = clone->unitId();
			nativeEvents::changeUnit(context, *original);
			Bonus bonus(BonusDuration::N_TURNS, BonusType::NONE, BonusSource::SPELL_EFFECT, 0, BonusSourceID(mechanics->getSpellId()));
			bonus.turnsRemain = mechanics->adjustMetamagicDuration(mechanics->getEffectDuration(), unit);
			nativeEvents::addBonus(context, *clone, bonus, true);
		}
	}
};

class Teleport final : public UnitEffect
{
	bool ignoresBarriers(const Mechanics * mechanics) const
	{
		const auto * battle = mechanics->battle()->getBattle();
		return battle && mechanics->getSpellId() == SpellID::TELEPORT && mechanics->usesNewHorizonsMagicV3()
			&& battle->getMagicRules()["spells"]["core:teleport"]["ignoreInterveningBarriers"].Bool();
	}
public:
	void adjustTargetTypes(std::vector<TargetType> & types, const Mechanics *) const override
	{
		if(types.empty()) return;
		if(types.front() != AimType::CREATURE) { types.clear(); return; }
		if(types.size() == 1) types.push_back(AimType::LOCATION);
		else if(types[1] != AimType::LOCATION) types.clear();
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		if(target.size() <= 1) return UnitEffect::applicableTarget(problem, mechanics, target);
		const auto * unit = target.front().unitValue;
		const auto from = target.front().hexValue, to = target[1].hexValue;
		if(unit && to.isValid() && mechanics->battle()->getAccessibility(unit).accessible(to, unit)
			&& (ignoresBarriers(mechanics) || !mechanics->battle()->battleHasPenaltyOnLine(from, to,
				!nativeEvents::truthy(parameters["isWallPassable"]), !nativeEvents::truthy(parameters["isMoatPassable"])))) return true;
		mechanics->adaptProblem(ESpellCastProblem::WRONG_SPELL_TARGET, problem);
		return false;
	}
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		auto result = UnitEffect::transformTarget(mechanics, aim, target);
		if(!result.empty() && aim.size() >= 2)
		{
			if(result.size() == 1) result.push_back(aim[1]); else result[1] = aim[1];
		}
		return result;
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const auto * unit = target.at(0).unitValue;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		nativeEvents::moveUnit(context, *unit, target.at(1).hexValue, true);
		if(!summon::perk(mechanics, "new-horizons:sorceryMagic", "new-horizons:sorceryMagic.teleporter")) return;
		Bonus bonus(BonusDuration::STACK_ACTIVATION, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::TELEPORT)));
		bonus.stacking = "new-horizons:teleporter";
		nativeEvents::addBonus(context, *unit, bonus, false);
	}
};

class Blink final : public UnitEffect
{
	const battle::Unit * selected(const Mechanics * mechanics, const Target & target) const
	{
		if(target.size() != 1) return nullptr;
		if(target.front().unitValue) return target.front().unitValue;
		return target.front().hexValue.isValid() ? mechanics->battle()->battleGetUnitByPos(target.front().hexValue, true) : nullptr;
	}
	bool legal(const Mechanics * mechanics, const battle::Unit * unit) const
	{
		if(!unit || !unit->isValidTarget(false) || (mechanics->isSmart() && !mechanics->ownerMatches(unit))) return false;
		// Lua explicitly invokes the base prototype, whose ignoreImmunity is unset.
		if((unit->isInvincible() && mechanics->isNegativeSpell()) || !mechanics->isReceptive(unit)) return false;
		const auto preview = newHorizonsBlink::preview(*mechanics, unit);
		return preview && !preview->legalDestinations.empty();
	}
public:
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		if(mechanics->usesNewHorizonsMagicV3() && !mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
			{ return legal(mechanics, unit); }).empty()) return true;
		return status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override
	{
		if(legal(mechanics, selected(mechanics, target))) return true;
		mechanics->adaptProblem(ESpellCastProblem::WRONG_SPELL_TARGET, problem);
		return false;
	}
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		auto transformed = UnitEffect::transformTarget(mechanics, aim, target);
		if(!transformed.empty()) return transformed;
		const auto * unit = selected(mechanics, aim);
		return legal(mechanics, unit) ? Target{Destination(unit)} : Target{};
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const auto * unit = selected(mechanics, target);
		const auto preview = unit ? newHorizonsBlink::preview(*mechanics, unit) : std::nullopt;
		if(!preview || preview->legalDestinations.empty()) return;
		const auto & destinations = preview->legalDestinations;
		const auto first = destinations[server->getRNG()->nextInt(1, static_cast<int>(destinations.size())) - 1];
		auto destination = first;
		if(preview->blinkmaster)
		{
			const auto second = destinations[server->getRNG()->nextInt(1, static_cast<int>(destinations.size())) - 1];
			const auto current = newHorizonsBlink::preview(*mechanics, unit);
			if(!current || std::find(current->legalDestinations.begin(), current->legalDestinations.end(), first) == current->legalDestinations.end()
				|| std::find(current->legalDestinations.begin(), current->legalDestinations.end(), second) == current->legalDestinations.end()) return;
			destination = newHorizonsBlink::fartherDestination(unit->getPosition(), first, second);
			if(!destination.isValid()) return;
		}
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		nativeEvents::moveUnit(context, *unit, destination, true);
	}
};
}
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createRelocationEffect(const std::string & path)
{
	using namespace spells::effects::native;
	if(path == "spells/clone") return std::make_shared<Clone>();
	if(path == "spells/teleport") return std::make_shared<Teleport>();
	if(path == "spells/blink") return std::make_shared<Blink>();
	return {};
}
}
