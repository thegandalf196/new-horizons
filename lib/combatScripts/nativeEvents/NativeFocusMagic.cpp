/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeEventCommon.h"
#include "../NativeJsonParameters.h"
#include "../../bonuses/BonusFilter.h"
#include "../../bonuses/BonusList.h"
#include "../../bonuses/BonusParameters.h"
#include "../../texts/MetaString.h"
#include <set>

namespace nativeEvents
{
namespace
{
BonusList marks(const battle::Unit & target)
{
	static const auto filter = []
	{
		BonusFilter value;
		value.type = "COMBAT_EVENT_TRIGGER";
		value.subtype = "core:arcaneBreach";
		return value.compile();
	}();
	BonusList result;
	const auto bonuses = target.getBonuses(filter.first, filter.second);
	for(const auto & bonus : *bonuses)
		if(bonus->source == BonusSource::SPELL_EFFECT && bonus->sid.toString() == "new-horizons:arcaneBreach")
			result.push_back(bonus);
	return result;
}

void logMark(const Context & context, const battle::Unit & target, size_t count, int basisPoints,
	BattleSide side, bool refreshed)
{
	MetaString message;
	message.appendTextID(refreshed ? "new-horizons.combat.arcaneBreach.refreshed" : "new-horizons.combat.arcaneBreach.applied");
	message.replaceTextID(creatureName(&target, target.getCount()));
	message.replaceTextID(side == BattleSide::ATTACKER ? "new-horizons.combat.arcaneBreach.side.attacker" : "new-horizons.combat.arcaneBreach.side.defender");
	message.replaceNumber(count);
	message.replaceNumber(2);
	message.replaceNumber(basisPoints / 100);
	message.replaceNumber((basisPoints / 10) % 10);
	message.replaceNumber(basisPoints % 10);
	log(context, message);
}

void applyMark(const Context & context, const battle::Unit & target, int basisPoints, BattleSide side, int applications)
{
	if(!target.alive()) return;
	const auto previous = marks(target).size();
	Bonus mark(BonusDuration::N_TURNS, BonusType::COMBAT_EVENT_TRIGGER, BonusSource::SPELL_EFFECT,
		basisPoints, BonusSourceID(SpellID(SpellID::decode("new-horizons:arcaneBreach"))),
		BonusSubtypeID(ScriptID(ScriptID::decode("core:arcaneBreach"))));
	mark.turnsRemain = 2;
	JsonNode parameters;
	parameters["beneficiarySide"].Integer() = static_cast<int>(side);
	mark.parameters = std::make_shared<BonusParameters>(nativeRules::capturedParameters(parameters));
	if(previous > 0) addBonus(context, target, mark, false);
	const int additions = applications > 1 && previous == 0 ? applications : 1;
	for(int index = 1; index <= std::min(additions, std::max(0, 3 - static_cast<int>(previous))); ++index)
		addBonus(context, target, mark, previous > 0 || index > 1);
	if(context.server.describeChanges())
	{
		const auto current = marks(target);
		int combined = 0, valid = 0;
		for(const auto & bonus : current)
		{
			if(!bonus->parameters) continue;
			const auto & data = bonus->parameters->toCustom<JsonNode>();
			if(data["beneficiarySide"].isNumber() && data["beneficiarySide"].Float() == static_cast<int>(side) && bonus->val > 0)
			{
				combined = std::min(combined + std::min(bonus->val, 2000), 6000);
				if(++valid >= 3) break;
			}
		}
		logMark(context, target, current.size(), combined, side, previous >= 3);
	}
}

void focusMagic(const Context & context)
{
	if(!context.payload.ranged) return;
	if(!context.parameters["beneficiarySide"].isNumber()) return;
	const auto side = static_cast<BattleSide>(static_cast<int>(number(context.parameters, "beneficiarySide")));
	if(context.battle.playerToSide(context.battle.battleGetOwner(context.self)) != side) return;
	std::set<uint32_t> marked;
	for(const auto & target : context.payload.targets)
	{
		const auto * victim = target.unit;
		if(!victim || !victim->alive() || target.damage <= 0
			|| context.battle.playerToSide(context.battle.battleGetOwner(victim)) == side) continue;
		if(!marked.insert(victim->unitId()).second) continue;
		const auto count = marks(*victim).size();
		const bool acquisition = context.parameters["arcaneAcquisition"].isBool() && context.parameters["arcaneAcquisition"].Bool();
		applyMark(context, *victim, static_cast<int>(number(context.parameters, "val")), side, acquisition && count == 0 ? 2 : 1);
	}
}
}

std::shared_ptr<ICombatEventScript> focusMagicEvent(const std::string & path)
{
	return path == "combat/focusMagic"
		? event(focusMagic, {CombatEventType::AFTER_ATTACK}, CombatEventForecastImplementation::CANONICAL_FOCUS_MAGIC)
		: nullptr;
}
}
