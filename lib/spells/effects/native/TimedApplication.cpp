/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../../nativeRules/PreparedPilotRules.h"
#include "Timed.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsSpellAvailability.h"
#include "../../NewHorizonsTimedStatusPlan.h"
#include "../../NewHorizonsSorceryRevision.h"
#include "../../NewHorizonsDivineRetribution.h"
#include "../../../combatScripts/nativeEvents/NativeEventCommon.h"
#include "../../../combatScripts/NativeJsonParameters.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../bonuses/BonusList.h"
#include "../../../json/JsonBonus.h"
#include "../../../nativeRules/NativeRulePilots.h"
#include "../../../battle/NewHorizonsSpecialtyProcs.h"
#include <vcmi/spells/Spell.h>

namespace spells::effects::native
{
namespace
{

void removeFamily(const nativeEvents::Context & context, const battle::Unit & unit, const std::string & key,
	const CSelector & selector, const std::string & cacheKey)
{
	const auto bonuses = unit.getBonuses(selector, cacheKey);
	BonusList previous;
	for(const auto & bonus : *bonuses)
		if(bonus->source == BonusSource::SPELL_EFFECT && bonus->sid.toString() == key) previous.push_back(bonus);
	if(!previous.empty()) nativeEvents::removeBonuses(context, unit, previous);
}


void applyBundle(const nativeEvents::Context & context, const battle::Unit & unit,
	const JsonMap & buffer, std::optional<Bonus> prepared, std::string_view preparedKey, bool cumulative)
{
	for(const auto & [name, data] : buffer)
	{
		// Merge the prepared operation at its former canonical JsonMap position.
		if(prepared && std::string_view(name) >= preparedKey)
		{
			nativeEvents::addBonus(context, unit, *prepared, cumulative);
			prepared.reset();
		}
		if(preparedKey.empty() || std::string_view(name) != preparedKey)
			nativeEvents::addBonus(context, unit, nativeRules::bonusFromDescriptor(data), cumulative);
	}
	if(prepared)
		nativeEvents::addBonus(context, unit, *prepared, cumulative);
}


void refresh(const nativeEvents::Context & context, const Mechanics & mechanics, const battle::Unit & unit, const std::string & key)
{
	if(key == "core:slow" && mechanics.usesNewHorizonsMagicV3())
	{
		const bool speed = mechanics.battle() && mechanics.battle()->getBattle()
			&& newHorizonsSorceryRevision::slowAffectsSpeed(mechanics.battle()->getBattle()->getMagicRules());
		const auto bonuses = unit.getBonuses(Selector::all, "all");
		BonusList previous;
		for(const auto & bonus : *bonuses)
			if(bonus->source == BonusSource::SPELL_EFFECT && bonus->sid.toString() == key
				&& (bonus->type == BonusType::STACKS_INITIATIVE || (speed && bonus->type == BonusType::STACKS_SPEED)))
				previous.push_back(bonus);
		if(!previous.empty()) nativeEvents::removeBonuses(context, unit, previous);
	}
	std::optional<BonusType> type;
	if(key == "new-horizons:divineRetribution") type = BonusType::DIVINE_RETRIBUTION;
	else if(key == "core:bless" && mechanics.usesNewHorizonsMagicV3()) type = BonusType::ALWAYS_MAXIMUM_DAMAGE;
	else if(key == "new-horizons:regeneration" && mechanics.usesNewHorizonsMagicV3()) type = BonusType::HP_REGENERATION;
	if(type) removeFamily(context, unit, key, Selector::type()(*type), "type_" + std::to_string(static_cast<int>(*type)));
}
}

void Timed::apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const
{
	const auto cast = prepareCast(mechanics);
	if(!cast.allowed) return;
	const auto & key = cast.key;
	const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
	bool described = false;
	for(const auto & destination : target)
	{
		const auto * unit = destination.unitValue;
		if(!unit || !unit->alive()) continue;
		if(key == "new-horizons:sanctuary") removeFamily(context, *unit, key, Selector::type()(BonusType::MORALE),
			"type_" + std::to_string(static_cast<int>(BonusType::MORALE)));
		auto plan = prepareRecipient(mechanics, unit, cast);
		auto & buffer = plan.buffer;
		if(server->describeChanges())
		{
			const bool once = key == "new-horizons:crusade" || key == "new-horizons:shieldOfChaos";
			if(!once || !described) describe(context, unit, key, buffer);
			if(once) described = true;
		}
		refresh(context, *mechanics, *unit, key);
		if(key == "core:poison")
		{
			std::vector<Bonus> incoming;
			for(const auto & [name, descriptor] : buffer) incoming.push_back(nativeRules::bonusFromDescriptor(descriptor));
			const auto plan = newHorizonsSpecialtyProcs::poisonRefreshPlan(*unit, std::move(incoming));
			BonusList removed;
			for(const auto & bonus : plan.remove) removed.push_back(std::make_shared<Bonus>(bonus));
			if(!removed.empty()) nativeEvents::removeBonuses(context, *unit, removed);
			for(const auto & bonus : plan.add) nativeEvents::addBonus(context, *unit, bonus, false);
			continue;
		}
		// Fixed shipped Timed families replay in canonical JsonMap key order.
		// Former Lua pairs order was not a stable serialization contract. The
		// fixed-family gameplay contract permits different packet/vector order
		// and Disease applicationOrder stamps (including later clock offsets),
		// while preserving existing saved stamps, values, lifetimes and outcomes.
		// This equivalence does not extend to arbitrary authored sibling bundles.
		applyBundle(context, *unit, buffer, std::move(plan.prepared), plan.preparedKey, parameters["cumulative"].Bool());
	}
}
}
