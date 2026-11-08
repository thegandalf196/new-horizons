/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Damage.h"
#include "../../ISpellMechanics.h"
#include "../../CSpell.h"
#include "../../NewHorizonsImplosion.h"
#include "../../../battle/CUnitState.h"
#include "../../../combatScripts/nativeEvents/NativeEventCommon.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../networkPacks/PacksForClientBattle.h"
#include "../../../texts/MetaString.h"
#include "../../../texts/CGeneralTextHandler.h"
#include "../../../texts/Languages.h"
#include "../../../GameLibrary.h"
#include <vstd/RNG.h>

namespace spells::effects::native
{
namespace
{
void implosionPull(const nativeEvents::Context & context, const Mechanics & mechanics, uint32_t id, BattleHex origin)
{
	if(!context.battle.getBattle() || !origin.isAvailable()) return;
	const auto profile = newHorizonsImplosion::rulesFor(context.battle.getBattle()->getMagicRules(), mechanics.getSpellId());
	if(!profile) return;
	for(const auto pulled : newHorizonsImplosion::pullOrder(context.battle, id, origin, profile->pullRadius))
	{
		const auto * unit = context.battle.battleGetUnitByID(pulled);
		if(!unit) continue;
		const auto destination = newHorizonsImplosion::pullDestination(context.battle, *unit, origin);
		if(!destination) continue;
		BattleStackMoved move;
		move.battleID = context.battle.getBattle()->getBattleID();
		move.stack = pulled;
		move.distance = 0;
		move.teleporting = true;
		move.displacementCause = BattleDisplacementCause::MAGICAL;
		move.tilesToMove.insert(*destination);
		context.server.apply(move);
	}
}

std::pair<int64_t, int64_t> beforeExecution(const Mechanics & mechanics, const battle::Unit & unit)
{
	auto copy = unit.acquireState();
	const auto health = copy->getAvailableHealth();
	const auto count = copy->getCount();
	auto amount = mechanics.adjustEffectValueBeforeExecution(&unit);
	copy->damage(amount);
	return {health - copy->getAvailableHealth(), count - copy->getCount()};
}

std::string plural(const std::string & key, int64_t count)
{
	return Languages::getPluralFormTextID(LIBRARY->generaltexth->getPreferredLanguage(), static_cast<int32_t>(count), key);
}
}

std::pair<int64_t, int64_t> Damage::collateral(const nativeEvents::Context & context, const Mechanics * mechanics,
	const battle::Unit * primary, int64_t actualDamage) const
{
	const auto candidates = mechanics->getHandOfFateCollateralPool(primary);
	if(candidates.empty()) return {};
	const auto draw = [&]() { return candidates[context.server.getRNG()->nextInt(1, static_cast<int>(candidates.size())) - 1].unitValue; };
	const auto * recipient = draw();
	const auto * hero = mechanics->getHeroCaster();
	if(mechanics->usesNewHorizonsMagicV3() && hero && mechanics->getSpell()->getJsonKey() == "new-horizons:handOfFate"
		&& hero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.fateDealer"))
	{
		const auto * second = draw();
		const bool firstHostile = context.battle.battleGetOwner(recipient) != mechanics->getCasterColor();
		const bool secondHostile = context.battle.battleGetOwner(second) != mechanics->getCasterColor();
		if(firstHostile != secondHostile)
		{
			if(!firstHostile) recipient = second;
		}
		else if(context.server.getRNG()->nextInt(1, 2) == 2) recipient = second;
	}
	const int64_t spill = static_cast<int64_t>(std::floor(static_cast<double>(actualDamage) / 2));
	const auto name = nativeEvents::creatureName(recipient, recipient->getCount());
	const auto blocked = [&]()
	{
		MetaString message;
		message.appendTextID("new-horizons.combat.handOfFate.blocked");
		message.replaceTextID(name);
		message.replaceNumber(spill);
		nativeEvents::log(context, message);
	};
	if(!isValidTarget(mechanics, recipient) || !isReceptive(mechanics, recipient))
	{
		blocked();
		return {};
	}
	if(mechanics->wouldResist(recipient))
	{
		mechanics->recordResistanceDamageShare(this, recipient, spill);
		blocked();
		return {};
	}
	context.server.recordResolvedSpellRecipient(recipient->unitId());
	const int64_t adjusted = mechanics->adjustRecipientDamage(recipient, spill);
	const auto id = recipient->unitId();
	const auto clearFrozen = [&]()
	{
		if(mechanics->getSpell()->isMagical() && !indirect)
			if(const auto * current = context.battle.battleGetUnitByID(id)) nativeEvents::clearFrozen(context, *current);
	};
	if(adjusted <= 0)
	{
		clearFrozen();
		blocked();
		return {};
	}
	const auto result = nativeEvents::damage(context, *recipient, adjusted, mechanics->getUnitCaster(), true, nullptr, parameters["destroyRemains"].Bool());
	clearFrozen();
	MetaString message;
	message.appendTextID("new-horizons.combat.handOfFate.collateral");
	message.replaceTextID(name);
	message.replaceNumber(spill);
	message.replaceNumber(adjusted);
	message.replaceNumber(result.first);
	nativeEvents::log(context, message);
	return result;
}

void Damage::apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const
{
	const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
	const bool describeChanges = server->describeChanges();
	const bool daggers = mechanics->getSpell()->getJsonKey() == "new-horizons:stormOfDaggers";
	const bool reaper = mechanics->getSpell()->getJsonKey() == "new-horizons:soulReaper";
	const battle::Unit * first = nullptr;
	bool multiple = false;
	int64_t totalDamage = 0, totalKilled = 0, ordinaryDamage = 0, ordinaryKilled = 0, executedKilled = 0;
	for(size_t index = 0; index < target.size(); ++index)
	{
		const auto * unit = target[index].unitValue;
		if(!unit || !unit->alive()) continue;
		const auto id = unit->unitId();
		server->recordResolvedSpellRecipient(id);
		const auto origin = unit->getPosition();
		const auto amount = damageForTarget(index, mechanics, unit);
		const auto baseline = describeChanges && reaper ? beforeExecution(*mechanics, *unit) : std::pair<int64_t, int64_t>{};
		const auto [damage, killed] = nativeEvents::damage(context, *unit, amount, mechanics->getUnitCaster(), true, nullptr, parameters["destroyRemains"].Bool());
		if(mechanics->getSpell()->isMagical() && !indirect)
		{
			if(const auto * current = context.battle.battleGetUnitByID(id)) nativeEvents::clearFrozen(context, *current);
			implosionPull(context, *mechanics, id, origin);
		}
		const auto extra = parameters["handOfFate"].Bool() && !mechanics->isRunicReflectionShare()
			? collateral(context, mechanics, unit, damage) : std::pair<int64_t, int64_t>{};
		if(!describeChanges) continue;
		if(daggers)
		{
			MetaString message;
			message.appendRawString("Storm of Daggers deals %d damage to %s.");
			message.replaceTextID(nativeEvents::creatureName(unit, unit->getCount()));
			message.replaceNumber(damage);
			nativeEvents::log(context, message);
			nativeEvents::perish(context, unit, killed);
		}
		if(first) multiple = true;
		else first = unit;
		totalDamage += damage + extra.first;
		totalKilled += killed + extra.second;
		if(extra.first > 0) multiple = true;
		if(reaper && killed > baseline.second)
		{
			ordinaryDamage += baseline.first;
			ordinaryKilled += baseline.second;
			executedKilled += killed - baseline.second;
		}
	}
	if(describeChanges && !daggers && first && totalDamage > 0)
		describe(context, mechanics, first, totalKilled, totalDamage, multiple, ordinaryKilled, ordinaryDamage, executedKilled);
}

void Damage::describe(const nativeEvents::Context & context, const Mechanics * mechanics, const battle::Unit * first,
	int64_t kills, int64_t damage, bool multiple, int64_t ordinaryKills, int64_t ordinaryDamage, int64_t executedKills) const
{
	const auto * spell = mechanics->getSpellId().toSpell();
	const auto key = spell->getJsonKey();
	MetaString message;
	if(key.find("deathStare") != std::string::npos && !multiple)
	{
		message.appendTextID(kills > 1 ? "core.genrltxt.119" : "core.genrltxt.118");
		message.replaceTextID(nativeEvents::creatureName(first, kills > 1 ? 0 : 1));
		message.replaceTextID(mechanics->getCasterNameTextID());
		if(kills > 1) message.replaceNumber(kills);
	}
	else if(key.find("accurateShot") != std::string::npos && !multiple)
	{
		message.appendTextID(plural("vcmi.battleWindow.accurateShot.resultDescription", kills));
		message.replaceTextID(nativeEvents::creatureName(first, static_cast<int>(kills)));
		message.replaceNumber(kills);
	}
	else if(key.find("thunderbolt") != std::string::npos && !multiple)
	{
		message.appendTextID("core.genrltxt.367");
		message.replaceTextID(nativeEvents::creatureName(first, 0));
		nativeEvents::log(context, message);
		message = MetaString();
		message.appendTextID("core.genrltxt.343");
		message.replaceNumber(damage);
	}
	else if(key == "new-horizons:soulReaper" && executedKills > 0 && !multiple)
	{
		nativeEvents::spellDamageLog(context, *spell, first, ordinaryDamage, ordinaryKills);
		message.appendTextID(plural("new-horizons.combat.soulReaper.execute", executedKills));
		message.replaceTextID(nativeEvents::creatureName(first, static_cast<int>(executedKills)));
		message.replaceNumber(executedKills);
	}
	else
	{
		nativeEvents::spellDamageLog(context, *spell, multiple ? nullptr : first, damage, kills);
		return;
	}
	nativeEvents::log(context, message);
}
}
