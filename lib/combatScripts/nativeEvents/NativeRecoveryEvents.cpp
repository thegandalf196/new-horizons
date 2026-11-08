/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeEventCommon.h"
#include "../../texts/MetaString.h"
#include <cmath>

namespace nativeEvents
{
namespace
{
void lifeDrain(const Context & context)
{
	const auto & actor = *context.self;
	if(actor.getTotalHealth() == actor.getAvailableHealth()) return;
	double total = 0;
	for(const auto & target : context.payload.targets)
		if(target.unit && target.unit->isLiving() && target.unit->getPhantomInitialIntegrity() <= 0)
			total += target.damage;
	const double requested = std::floor(total * number(context.parameters, "val") / 100);
	if(requested <= 0) return;
	const int count = actor.getCount();
	const auto result = heal(context, actor, static_cast<int64_t>(requested), EHealLevel::RESURRECT, EHealPower::PERMANENT);
	if(result.first <= 0) return;
	animation(context, actor, "SP06_", "DRAINLIF", 0.5);
	lifeDrainedLog(context, result.first, result.second, count);
}

void soulSteal(const Context & context)
{
	double souls = 0;
	for(const auto & target : context.payload.targets)
		if(target.unit && target.unit->isLiving() && target.unit->getPhantomInitialIntegrity() <= 0)
			souls += target.killed;
	const double gained = souls * number(context.parameters, "val");
	if(gained <= 0) return;
	const auto & actor = *context.self;
	const int count = actor.getCount();
	const auto power = truthy(context.parameters["permanent"]) ? EHealPower::PERMANENT : EHealPower::ONE_BATTLE;
	const auto result = heal(context, actor, static_cast<int64_t>(gained * actor.getMaxHealth()), EHealLevel::OVERHEAL, power);
	if(result.first <= 0) return;
	animation(context, actor, "SP06_", "DRAINLIF", 0.5);
	lifeDrainedLog(context, result.first, result.second, count);
}

void vampirism(const Context & context)
{
	const auto * actor = context.self;
	if(!actor || !actor->alive() || actor->getAvailableHealth() >= actor->getTotalHealth()) return;
	double total = 0;
	for(const auto & target : context.payload.targets)
		if(target.unit)
			total += std::min(std::max(0.0, static_cast<double>(target.damage)), std::max(0.0, static_cast<double>(target.healthBeforeAttack)));
	const double requested = std::floor(total * std::max(0.0, number(context.parameters, "val")) / 10000);
	if(requested <= 0) return;
	const auto result = heal(context, *actor, static_cast<int64_t>(requested), EHealLevel::HEAL, EHealPower::PERMANENT);
	if(result.first <= 0) return;
	MetaString message;
	message.appendTextID("new-horizons.combat.vampirism.healed");
	message.replaceTextID(creatureName(actor, actor->getCount()));
	message.replaceNumber(result.first);
	log(context, message);
}
}

std::shared_ptr<ICombatEventScript> recoveryEvent(const std::string & path)
{
	if(path == "combat/lifeDrain") return event(lifeDrain, {CombatEventType::AFTER_ATTACK});
	if(path == "combat/soulSteal") return event(soulSteal, {CombatEventType::AFTER_ATTACK});
	if(path == "combat/vampirism") return event(vampirism, {CombatEventType::AFTER_ATTACK});
	return nullptr;
}
}
