/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../battle/NewHorizonsPassiveSpecialties.h"
#include "../battle/NewHorizonsCommandResources.h"
#include "NewHorizonsMetamagicPoints.h"
#include "NewHorizonsMetamagicEntry.h"
#include "../CStack.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/CUnitState.h"
#include "../battle/Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../networkPacks/SetStackEffect.h"
#include <vcmi/ServerCallback.h>
#include <set>
#include <algorithm>
#include <limits>

namespace newHorizonsMetamagic
{
namespace
{
constexpr const char * ATTUNEMENT = "new-horizons:metamagic.arcaneAttunement";

void echo(ServerCallback & server, const CBattleInfoCallback & callback, SpellID spell,
	std::span<const BattleStackAttacked> injuries)
{
	// The original packets already contain mitigation, chain falloff and caps.
	// Apply a quarter of each actual target's total without running any spell
	// selection, resistance or caster modifiers again. Native damage consequences
	// (including Rebirth) still follow the ordinary damage pipeline.
	std::vector<std::pair<uint32_t, int64_t>> originals;
	for(const auto & injury : injuries)
	{
		if(injury.damageAmount <= 0)
			continue;
		auto found = std::ranges::find(originals, injury.stackAttacked, &std::pair<uint32_t, int64_t>::first);
		if(found == originals.end())
			found = originals.emplace(originals.end(), injury.stackAttacked, 0);
		auto & total = found->second;
		total = injury.damageAmount > std::numeric_limits<int64_t>::max() - total
			? std::numeric_limits<int64_t>::max() : total + injury.damageAmount;
	}
	for(const auto & [id, original] : originals)
	{
		const auto * unit = callback.battleGetUnitByID(id);
		if(!unit || !unit->alive() || unit->isGhost() || echoDamage(original) == 0
			|| newHorizonsPassiveSpecialties::magicalDamageImmune(*unit))
			continue;
		auto state = unit->acquireState();
		BattleStackAttacked hit;
		hit.stackAttacked = id;
		hit.attackerID = std::numeric_limits<uint32_t>::max();
		hit.spellID = spell;
		hit.flags = BattleStackAttacked::SPELL_EFFECT | BattleStackAttacked::SECONDARY;
		hit.damageAmount = newHorizonsCommand::mitigateIncomingDamage(callback, unit, echoDamage(original));
		CStack::prepareAttacked(hit, *server.getRNG(), state, false, false, battle::DamageProvenance::SPELL);
		StacksInjured packet;
		packet.battleID = callback.getBattle()->getBattleID();
		packet.stacks.push_back(std::move(hit));
		server.apply(packet);
		if(server.describeChanges())
		{
			BattleLogMessage log;
			log.battleID = packet.battleID;
			MetaString line;
			line.appendRawString("Arcane Echo deals ");
			line.appendNumber(packet.stacks.front().damageAmount);
			line.appendRawString(" damage to ");
			line.appendNamePlural(unit->creatureId());
			line.appendRawString(".");
			log.lines.push_back(std::move(line));
			server.apply(log);
		}
	}
}

void fracture(ServerCallback & server, const CBattleInfoCallback & callback, BattleSide side,
	std::span<const uint32_t> resolved)
{
	SetStackEffect packet;
	packet.battleID = callback.getBattle()->getBattleID();
	std::set<uint32_t> seen;
	for(const auto id : resolved)
	{
		const auto * unit = callback.battleGetUnitByID(id);
		if(!unit || !unit->alive() || unit->isGhost() || !seen.insert(id).second
			|| callback.battleGetOwner(unit) == callback.sideToPlayer(side))
			continue;
		for(const auto & bonus : callback.getBattle()->getLocalSpellEffects(id))
		{
			if(bonus.source != BonusSource::SPELL_EFFECT || (bonus.duration & BonusDuration::N_TURNS) == 0
				|| bonus.turnsRemain <= 0 || !bonus.sid.as<SpellID>().hasValue())
				continue;
			const auto * spell = bonus.sid.as<SpellID>().toSpell();
			if(!spell || !spell->isMagical() || !spell->isPositive())
				continue;
			packet.toRemove.emplace_back(id, std::vector<Bonus>{bonus});
			if(bonus.turnsRemain > 1)
			{
				auto shortened = bonus;
				--shortened.turnsRemain;
				packet.toAdd.emplace_back(id, std::vector<Bonus>{std::move(shortened)});
			}
		}
	}
	if(!packet.toRemove.empty())
		server.apply(packet);
}
}

void refreshAttunement(ServerCallback & server, const CBattleInfoCallback & callback, BattleSide side)
{
	const auto facts = profile(callback.battleGetFightingHero(side));
	if(!facts.pointPolicy || facts.rank == 0)
		return;
	SetStackEffect packet;
	packet.battleID = callback.getBattle()->getBattleID();
	const int32_t amount = attunementInitiative(facts, callback.getBattle()->getMetamagicPointState(side).points);
	for(const auto * unit : callback.battleGetAllUnits(false))
	{
		if(!unit || unit->isGhost())
			continue;
		const auto oldEffects = unit->getBonuses(Selector::type()(BonusType::STACKS_INITIATIVE_FLAT));
		for(const auto & old : *oldEffects)
			if(old && old->stacking == ATTUNEMENT && old->spellCasterOwner == callback.sideToPlayer(side))
				packet.toRemove.emplace_back(unit->unitId(), std::vector<Bonus>{*old});
		if(amount == 0 || !unit->alive() || callback.battleGetOwner(unit) != callback.sideToPlayer(side))
			continue;
		if(const auto initiative = entryAttunement(callback, *unit))
			packet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{*initiative});
	}
	if(!packet.toRemove.empty() || !packet.toAdd.empty())
		server.apply(packet);
}

void publishPointCompletion(ServerCallback & server, const CBattleInfoCallback & callback,
	const spells::Mechanics & mechanics, std::span<const uint32_t> resolved,
	std::span<const BattleStackAttacked> injuries, std::optional<BattleSide> acceptedManualCaster)
{
	if(!callback.getBattle() || (!acceptedManualCaster && !mechanics.isGenuineHeroCast())
		|| mechanics.getAutomaticHeroCast().automatic())
		return;
	// Single-target Magic Mirror changes execution mode permanently. Completion
	// belongs to the accepted original manual caster, not the reflected effect.
	const auto casterSide = acceptedManualCaster.value_or(mechanics.getCasterSide());
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	if((casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER) || !spell)
		return;
	const bool damaging = spell->isDamage();
	const auto casterFacts = profile(callback.battleGetFightingHero(casterSide));
	if(damaging && casterFacts.pointPolicy && casterFacts.arcaneEcho)
	{
		auto state = callback.getBattle()->getMetamagicPointState(casterSide);
		if(!state.echoUsedThisRound)
		{
			state.echoUsedThisRound = true;
			SetMetamagicPointState consumed;
			consumed.battleID = callback.getBattle()->getBattleID();
			consumed.side = casterSide;
			consumed.round = callback.battleGetRound();
			consumed.state = state;
			server.apply(consumed);
			echo(server, callback, mechanics.getSpellId(), injuries);
		}
	}
	if(damaging && casterFacts.pointPolicy && casterFacts.spellFracture)
		fracture(server, callback, casterSide, resolved);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto facts = profile(callback.battleGetFightingHero(side));
		if(!facts.pointPolicy || facts.rank == 0)
			continue;
		const auto previous = callback.getBattle()->getMetamagicPointState(side);
		auto next = previous;
		next.points = pointsAfterResolvedSpell(facts, previous.points, side == casterSide, true);
		if(facts.arcaneSingularity && next.points >= 10)
			next.singularityReached = true;
		if(next == previous)
			continue;
		SetMetamagicPointState packet;
		packet.battleID = callback.getBattle()->getBattleID();
		packet.side = side;
		packet.round = callback.battleGetRound();
		packet.state = next;
		server.apply(packet);
		refreshAttunement(server, callback, side);
	}
}

void publishRoundEffects(ServerCallback & server, const CBattleInfoCallback & callback)
{
	if(!callback.getBattle())
		return;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = callback.battleGetFightingHero(side);
		const auto facts = profile(hero);
		if(!facts.pointPolicy || facts.rank == 0)
			continue;
		const auto points = callback.getBattle()->getMetamagicPointState(side).points;
		const auto recovery = manaConduitRecovery(facts, points);
		if(recovery > 0)
			hero->spendMana(&server, -recovery);
		BattleUnitsChanged shields;
		shields.battleID = callback.getBattle()->getBattleID();
		for(const auto * unit : callback.battleGetAllUnits(false))
		{
			if(!unit || !unit->alive() || unit->isGhost()
				|| callback.battleGetOwner(unit) != callback.sideToPlayer(side))
				continue;
			const auto amount = arcaneShieldCapacity(facts, points, battle::getMaximumHealth(*unit));
			if(amount == 0)
				continue;
			auto state = unit->acquireState();
			state->arcaneShieldHitPoints = amount;
			UnitChanges change(unit->unitId(), UnitChanges::EOperation::UPDATE);
			change.data = state->save();
			shields.changedStacks.push_back(std::move(change));
		}
		if(!shields.changedStacks.empty())
			server.apply(shields);
		refreshAttunement(server, callback, side);
	}
}
}
