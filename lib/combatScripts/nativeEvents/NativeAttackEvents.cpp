/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeEventCommon.h"
#include "../../GameLibrary.h"
#include "../../bonuses/BonusFilter.h"
#include "../../spells/CSpell.h"
#include <vstd/RNG.h>
#include <cmath>

namespace nativeEvents
{
namespace
{
std::string situation(const Context & context)
{
	if(!context.payload.ranged) return "melee";
	const bool distance = context.battle.battleHasDistancePenalty(context.self, context.self->getPosition(), context.other->getPosition());
	const bool wall = context.battle.battleHasWallPenalty(context.self, context.self->getPosition(), context.other->getPosition());
	if(distance && wall) return "rangedDistanceAndWallPenalty";
	if(distance) return "rangedDistancePenalty";
	if(wall) return "rangedWallPenalty";
	return "ranged";
}

void deathStare(const Context & context)
{
	const auto & actor = *context.self;
	const auto * target = context.other;
	if(!actor.alive() || !target || !target->alive()) return;
	double killed;
	if(string(context.parameters, "situation") == "commander")
	{
		if(target->unitLevel() <= 0) return;
		killed = std::floor(actor.unitLevel() * number(context.parameters, "val") / target->unitLevel());
	}
	else
	{
		if(string(context.parameters, "situation", "melee") != situation(context)) return;
		const auto * sourceSpell = spell(string(context.parameters, "spell", "core:deathStare"));
		if(!sourceSpell) throw std::runtime_error("Unknown death stare spell");
		const int chance = actor.favorableCreatureAbilityChanceBasisPoints(static_cast<int>(number(context.parameters, "val")));
		if(chance <= 0) return;
		const int count = actor.getCount();
		const double probability = chance / 10000.0;
		const int cap = static_cast<int>(std::ceil(count * probability));
		killed = receptive(context, actor, *sourceSpell, *target)
			? hostileBinomial(context, actor, *target, count, probability, cap)
			: context.server.getRNG()->nextBinomialInt(count, probability);
		killed = std::min(killed, static_cast<double>(cap));
	}
	if(killed <= 0) return;
	const auto * sourceSpell = spell(string(context.parameters, "spell", "core:deathStare"));
	if(!sourceSpell) throw std::runtime_error("Unknown death stare spell");
	cast(context, actor, *sourceSpell, {target}, static_cast<int64_t>(killed));
}

void destruction(const Context & context)
{
	const auto & actor = *context.self;
	const auto * target = context.other;
	if(!actor.alive() || !target || !target->alive()) return;
	const auto kind = string(context.parameters, "killBy");
	double kills = 0;
	if(kind == "percentage") kills = std::floor(target->getCount() * number(context.parameters, "amount") / 100);
	else if(kind == "count") kills = number(context.parameters, "amount");
	const int chance = static_cast<int>(number(context.parameters, "val"));
	if(kills <= 0)
	{
		context.server.rollCombatAbility(context.battle, actor, chance);
		return;
	}
	if(!context.server.rollHostileCombatAbility(context.battle, actor, *target, chance)) return;
	animation(context, *target, "C13SPW0", "SLAYER", 1.0, true);
	const auto result = damage(context, *target, static_cast<int64_t>(kills * target->getMaxHealth()), &actor);
	perish(context, target, result.second);
}

void transmutation(const Context & context)
{
	const auto & actor = *context.self;
	const auto * target = context.other;
	if(!actor.alive() || actor.getPhantomInitialIntegrity() > 0 || !target || !target->alive()
		|| !target->isLiving() || target->getPhantomInitialIntegrity() > 0) return;
	static const auto immunity = [] { BonusFilter f; f.type = "TRANSMUTATION_IMMUNITY"; return f.compile(); }();
	if(target->hasBonus(immunity.first, immunity.second)) return;
	const auto * result = truthy(context.parameters["creature"])
		? creature(string(context.parameters, "creature")) : actor.creatureId().toEntity(LIBRARY);
	if(!result) throw std::runtime_error("Unknown transmutation creature");
	const int chance = static_cast<int>(number(context.parameters, "val"));
	if(target->creatureId().toEntity(LIBRARY)->getJsonKey() == result->getJsonKey())
	{
		context.server.rollCombatAbility(context.battle, actor, chance);
		return;
	}
	std::optional<double> count;
	const auto mode = string(context.parameters, "transmuteBy");
	if(mode == "health") count = std::max(std::floor(static_cast<double>(target->getCount()) * target->getMaxHealth() / result->getMaxHealth()), 1.0);
	else if(mode == "count") count = target->getCount();
	if(!count)
	{
		context.server.rollCombatAbility(context.battle, actor, chance);
		return;
	}
	if(!context.server.rollHostileCombatAbility(context.battle, actor, *target, chance)) return;
	battle::UnitInfo replacement;
	replacement.count = static_cast<TQuantity>(*count);
	replacement.type = result->getId();
	replacement.side = target->unitSide();
	replacement.position = target->getPosition();
	replacement.summoned = false;
	removeUnit(context, *target);
	addUnit(context, replacement);
	refreshUnits(context);
}
}

std::shared_ptr<ICombatEventScript> attackEvent(const std::string & path)
{
	if(path == "combat/deathStare") return event(deathStare, {CombatEventType::AFTER_ATTACK});
	if(path == "combat/destruction") return event(destruction, {CombatEventType::AFTER_ATTACK});
	if(path == "combat/transmutation") return event(transmutation, {CombatEventType::AFTER_ATTACK});
	return nullptr;
}
}
