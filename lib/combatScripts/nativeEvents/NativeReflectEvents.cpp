/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeEventCommon.h"
#include "../../bonuses/BonusFilter.h"
#include "../../bonuses/BonusList.h"
#include "../../spells/CSpell.h"
#include <cmath>

namespace nativeEvents
{
namespace
{
bool fireImmune(const battle::Unit & actor)
{
	static const auto filters = []
	{
		std::array<std::pair<CSelector, std::string>, 3> values;
		const std::array<std::string, 3> names{"SPELL_SCHOOL_IMMUNITY", "NEGATIVE_EFFECTS_IMMUNITY", "SPELL_DAMAGE_REDUCTION"};
		for(size_t i = 0; i < names.size(); ++i)
		{
			BonusFilter filter;
			filter.type = names[i];
			values[i] = filter.compile();
		}
		return values;
	}();
	for(size_t i = 0; i < filters.size(); ++i)
	{
		const auto records = actor.getBonuses(filters[i].first, filters[i].second);
		if(std::any_of(records->begin(), records->end(), [i](const auto & bonus)
			{ return bonus->subtype.toString() == "fire" && (i != 2 || bonus->val >= 100); }))
			return true;
	}
	return false;
}

void fireShield(const Context & context)
{
	const auto * attacker = context.other;
	const auto & bearer = *context.self;
	if(context.payload.ranged || !attacker || !attacker->alive() || bearer.isClone()) return;
	if(!context.battle.isMeleeAttackPossible(attacker, &bearer) || fireImmune(*attacker)) return;
	const auto entry = std::find_if(context.payload.targets.begin(), context.payload.targets.end(), [&](const auto & target)
		{ return target.unit && target.unit->unitId() == bearer.unitId(); });
	if(entry == context.payload.targets.end()) return;
	const double reflectable = std::min(static_cast<double>(entry->healthBeforeAttack), static_cast<double>(entry->damageBeforeDefense));
	const auto * sourceSpell = spell("core:fireShield");
	if(!sourceSpell) throw std::runtime_error("Unknown Fire Shield spell");
	const auto amount = adjustDamage(context, *sourceSpell, bearer, *attacker,
		static_cast<int64_t>(std::floor(reflectable * number(context.parameters, "val") / 100)));
	if(amount <= 0) return;
	animation(context, bearer, "C05SPF0", "FIRESHIE", 1.0, true);
	const auto result = damage(context, *attacker, amount, &bearer, true);
	spellDamageLog(context, *sourceSpell, attacker, result.first, result.second);
}

const battle::Unit * sourceOnSide(const Context & context, bool includeBearer)
{
	if(context.parameters["casterSide"].isNull()) return nullptr;
	const auto side = static_cast<BattleSide>(static_cast<int>(number(context.parameters, "casterSide")));
	if(includeBearer && context.self && context.self->unitSide() == side) return context.self;
	if(context.other && context.other->unitSide() == side) return context.other;
	const auto candidates = context.battle.battleGetUnitsIf([&](const battle::Unit * unit) { return unit->unitSide() == side; });
	return candidates.empty() ? nullptr : candidates.front();
}

double actualDamage(const AttackedTarget & target)
{
	if(!target.unit) return 0;
	return std::min(std::max(0.0, static_cast<double>(target.damage)), std::max(0.0, static_cast<double>(target.healthBeforeAttack)));
}

void hexOfPain(const Context & context)
{
	if(!context.self || !context.self->alive()) return;
	const auto * source = sourceOnSide(context, false);
	if(!source) return;
	const auto * sourceSpell = spell("new-horizons:hexOfPain");
	if(!sourceSpell) throw std::runtime_error("Unknown Hex of Pain spell");
	double inflicted = 0;
	for(const auto & target : context.payload.targets) inflicted += actualDamage(target);
	const double share = std::floor(inflicted * number(context.parameters, "damageSharePercent") / 100);
	const auto amount = adjustDamage(context, *sourceSpell, *source, *context.self,
		static_cast<int64_t>(number(context.parameters, "val") + share));
	if(amount <= 0) return;
	const auto result = damage(context, *context.self, amount, source, true);
	spellDamageLog(context, *sourceSpell, context.self, result.first, result.second);
}

void shadowGift(const Context & context)
{
	if(!context.self || !context.self->alive()) return;
	const auto * source = sourceOnSide(context, true);
	if(!source) return;
	const auto * sourceSpell = spell("new-horizons:shadowGift");
	if(!sourceSpell) throw std::runtime_error("Unknown Shadow Gift spell");
	for(const auto & target : context.payload.targets)
	{
		const double inflicted = actualDamage(target);
		if(!target.unit || target.unit == context.self || inflicted <= 0) continue;
		const double raw = std::floor(inflicted * number(context.parameters, "val") / 10000);
		if(raw <= 0) continue;
		const auto amount = adjustDamage(context, *sourceSpell, *source, *target.unit, static_cast<int64_t>(raw));
		if(amount <= 0) continue;
		const auto result = damage(context, *target.unit, amount, source, true, sourceSpell);
		spellDamageLog(context, *sourceSpell, target.unit, result.first, result.second);
	}
}
}

std::shared_ptr<ICombatEventScript> reflectEvent(const std::string & path)
{
	if(path == "combat/fireShield") return event(fireShield, {CombatEventType::AFTER_ATTACKED});
	if(path == "combat/hexOfPain") return event(hexOfPain, {CombatEventType::AFTER_ATTACK}, CombatEventForecastImplementation::CANONICAL_HEX_OF_PAIN);
	if(path == "combat/shadowGift") return event(shadowGift, {CombatEventType::AFTER_ATTACK});
	return nullptr;
}
}
