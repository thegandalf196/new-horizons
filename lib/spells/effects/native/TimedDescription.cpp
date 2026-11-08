/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Timed.h"
#include "../../../combatScripts/nativeEvents/NativeEventCommon.h"
#include "../../../texts/MetaString.h"
#include <cmath>
#include <iomanip>

namespace spells::effects::native
{
namespace
{
int64_t field(const JsonMap & bonuses, const std::string & name, const std::string & key, int64_t fallback = 0)
{
	const auto entry = bonuses.find(name);
	return entry == bonuses.end() || entry->second[key].isNull() ? fallback : static_cast<int64_t>(entry->second[key].Float());
}

std::string percent(int64_t basis)
{
	std::ostringstream out;
	out << static_cast<int64_t>(std::floor(basis / 100.0)) << '.' << std::setfill('0') << std::setw(2) << basis % 100;
	return out.str();
}

void specialDescription(MetaString & message, const battle::Unit * unit, const std::string & key, const JsonMap & bonuses)
{
	std::ostringstream out;
	if(key == "new-horizons:crusade")
		out << "Crusade! grants +" << field(bonuses, "attack", "val") << " Attack, +" << field(bonuses, "defense", "val")
			<< " Defense, +" << field(bonuses, "initiative", "val") << " flat Initiative, and "
			<< percent(field(bonuses, "magicalDamageReduction", "val")) << "% independent Magical Damage Reduction to the friendly army for "
			<< field(bonuses, "attack", "turns", 3) << " rounds; affected creatures cannot suffer negative Morale.";
	else if(key == "new-horizons:shieldOfChaos")
		out << "Shield of Chaos grants " << percent(field(bonuses, "physicalDamageReduction", "val"))
			<< "% physical damage reduction before caps (the global physical cap is 80%) and "
			<< percent(field(bonuses, "magicalDamageReduction", "val")) << "% magical damage reduction for "
			<< field(bonuses, "physicalDamageReduction", "turns", 2) << " rounds; it imposes -10 Morale and -10 Luck on %s.";
	else if(key == "new-horizons:entangle")
	{
		int64_t turns = 1;
		for(const auto & [name, bonus] : bonuses)
			if(bonus["type"].String() == "BIND_EFFECT") { turns = bonus["turns"].isNull() ? turns : bonus["turns"].Integer(); break; }
		out << "Entangle roots a stack for " << turns << " round(s). It prevents voluntary movement only; attacks, retaliation, shooting, Wait, Defend, and nonmovement abilities remain available.";
	}
	else if(key == "new-horizons:vengefulVines")
	{
		int64_t turns = 2, speed = 2;
		for(const auto & [name, bonus] : bonuses)
			if(bonus["type"].String() == "STACKS_MOVEMENT_RANGE")
			{
				turns = bonus["turns"].isNull() ? turns : static_cast<int64_t>(bonus["turns"].Float());
				speed = bonus["val"].isNull() ? speed : static_cast<int64_t>(std::abs(bonus["val"].Float()));
				break;
			}
		message.appendRawString("Vengeful Vines reduces the movement Speed of %s by %d for %d rounds; Initiative is unchanged.");
		message.replaceTextID(nativeEvents::creatureName(unit, unit->getCount()));
		message.replaceNumber(speed);
		message.replaceNumber(turns);
		return;
	}
	message.appendRawString(out.str());
	if(key == "new-horizons:shieldOfChaos") message.replaceTextID(nativeEvents::creatureName(unit, unit->getCount()));
}
}

void Timed::describe(const nativeEvents::Context & context, const battle::Unit * unit, const std::string & key, const JsonMap & bonuses) const
{
	MetaString message;
	if(key == "new-horizons:crusade" || key == "new-horizons:shieldOfChaos" || key == "new-horizons:entangle" || key == "new-horizons:vengefulVines")
	{
		specialDescription(message, unit, key, bonuses);
		nativeEvents::log(context, message);
		return;
	}
	for(const auto & [name, bonus] : bonuses)
		if(bonus["type"].String() == "STACK_HEALTH" && bonus["val"].Float() < 0)
		{
			const auto health = unit->getMaxHealth();
			const auto lost = health - static_cast<int64_t>(std::floor(health * (100 + bonus["val"].Float()) / 100));
			message.appendTextID(parameters[unit->getCount() == 1 ? "battleLogSingular" : "battleLogPlural"].String());
			message.replaceTextID(nativeEvents::creatureName(unit, unit->getCount()));
			message.replaceNumber(lost);
			nativeEvents::log(context, message);
			return;
		}
	if(parameters["battleLogPlural"].String().empty()) return;
	const bool singular = !parameters["battleLogSingular"].String().empty() && unit->getCount() == 1;
	message.appendTextID(parameters[singular ? "battleLogSingular" : "battleLogPlural"].String());
	message.replaceTextID(nativeEvents::creatureName(unit, unit->getCount()));
	nativeEvents::log(context, message);
}
}
