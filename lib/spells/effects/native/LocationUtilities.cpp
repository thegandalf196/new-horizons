/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SpellEffect.h"
#include "StatusHelpers.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsSorcery.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../scripting/ScriptTypeDescription.h"

namespace spells::effects::native
{
namespace
{
int timeStopRadius(const Mechanics & mechanics)
{
	const auto * hero = mechanics.getHeroCaster();
	const bool chronomancer = hero && hero->hasActivePerk(newHorizonsSorcery::SORCERY_MAGIC_SKILL, newHorizonsSorcery::CHRONOMANCER_PERK);
	return newHorizonsSorcery::timeStopRadius(mechanics.getEffectPower(), mechanics.getSpellPowerCoefficientBasisPoints(),
		mechanics.getEmpowerSpellBonusPercent(), chronomancer);
}

BattleHexArray timeStopArea(BattleHex center, int range)
{
	BattleHexArray result;
	std::bitset<GameConstants::BFIELD_SIZE> seen;
	seen.set(center.toInt());
	std::vector<std::pair<BattleHex, int>> queue{{center, 0}};
	for(size_t cursor = 0; cursor < queue.size(); ++cursor)
	{
		const auto [hex, distance] = queue[cursor];
		if(hex.isValid()) result.insert(hex);
		if(distance >= range) continue;
		for(const auto direction : {BattleHex::TOP_LEFT, BattleHex::TOP_RIGHT, BattleHex::RIGHT,
			BattleHex::BOTTOM_RIGHT, BattleHex::BOTTOM_LEFT, BattleHex::LEFT})
		{
			const auto next = hex.cloneInDirection(direction, false);
			if(!next.isValid() || seen[next.toInt()]) continue;
			seen.set(next.toInt());
			queue.emplace_back(next, distance + 1);
		}
	}
	return result;
}

class TimeStop final : public SpellEffect
{
public:
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target &) const override
	{
		if(aim.empty() || !aim.front().hexValue.isValid()) return {};
		const auto area = timeStopArea(aim.front().hexValue, timeStopRadius(*mechanics));
		Target result;
		for(const auto * unit : mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
		{
			if(!unit->alive()) return false;
			for(const auto & hex : unit->getHexes()) if(area.contains(hex)) return true;
			return false;
		})) result.emplace_back(unit);
		return result;
	}
	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & target) const override
	{
		if(!target.empty() && target.front().hexValue.isValid()) hexes.insert(timeStopArea(target.front().hexValue, timeStopRadius(*mechanics)));
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		const int radius = timeStopRadius(*mechanics);
		for(const auto & destination : target)
		{
			const auto * unit = destination.unitValue;
			if(!unit || !unit->alive()) continue;
			for(const auto * type : {"NOT_ACTIVE", "INVINCIBLE", "TIME_STOP"})
			{
				const bool marker = std::string_view(type) == "TIME_STOP";
				auto record = status::bonus(type, marker ? radius : 1, "new-horizons:timeStop", 0, {}, true);
				record.Struct().erase("stacking");
				record["duration"].String() = "ONE_BATTLE";
				record["addInfo"].Integer() = static_cast<int>(mechanics->getCasterSide());
				if(marker) record["description"].String() = "Time Stop stasis";
				else record["hidden"].Bool() = true;
				status::add(context, *unit, std::move(record), true);
			}
		}
	}
};

class Purify final : public SpellEffect
{
public:
	bool applicableTarget(Problem &, const Mechanics *, const Target & target) const override { return target.size() == 1; }
	Target transformTarget(const Mechanics *, const Target & aim, const Target &) const override { return aim; }
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createLocationUtilityEffect(const ScriptTypeDescription & description)
{
	using namespace spells::effects::native;
	if(description.sourcePath == "spells/timeStop") return std::make_shared<TimeStop>();
	if(description.sourcePath == "spells/purify") return std::make_shared<Purify>();
	return {};
}
}
