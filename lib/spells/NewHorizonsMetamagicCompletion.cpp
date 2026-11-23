/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../nativeRules/CompiledMetamagicRules.h"
#include "NewHorizonsMetamagicCompletion.h"
#include "NewHorizonsMetamagicAmmunition.h"
#include "NewHorizonsMagicalMemory.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "CurePhysicalAffliction.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/CUnitState.h"
#include "../battle/NewHorizonsAttackControllers.h"
#include "../battle/NewHorizonsCommandResources.h"
#include "../battle/NewHorizonsFrozen.h"
#include "../battle/NewHorizonsPassiveSpecialties.h"
#include "../battle/NewHorizonsStun.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../combatScripts/nativeEvents/NativeEventCommon.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/CHero.h"
#include "../networkPacks/PacksForClient.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../networkPacks/SetStackEffect.h"
#include <set>
#include <vstd/RNG.h>

namespace newHorizonsMetamagic
{
namespace
{
using nativeRules::MetamagicPerk;
// Metamagic compares source-local effect records, not exchange HP/sidecars.
battle::BattleEffectSnapshot effectSnapshot(const CBattleInfoCallback & callback, uint32_t id)
{
	battle::BattleEffectSnapshot result;
	result.effects = callback.getBattle()->getLocalSpellEffects(id);
	return result;
}
bool perk(const CGHeroInstance * hero, MetamagicPerk name)
{
	return nativeRules::hasMetamagicPerk(hero, name);
}

nativeEvents::Context context(ServerCallback & server, const CBattleInfoCallback & battle)
{
	static const JsonNode parameters;
	static const CombatEventPayload payload;
	return {server, battle, nullptr, nullptr, parameters, payload};
}

void remove(ServerCallback & server, const CBattleInfoCallback & battle, uint32_t id, const std::vector<Bonus> & bonuses)
{
	if(bonuses.empty())
		return;
	SetStackEffect packet;
	packet.battleID = battle.getBattle()->getBattleID();
	packet.toRemove.emplace_back(id, bonuses);
	server.apply(packet);
}

void purge(ServerCallback & server, const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	// Provisional deterministic priority: activation denial before mobility and attack penalties.
	for(const auto & bonuses : {newHorizonsFrozen::removalPlan(unit), newHorizonsStun::removalPlan(unit),
		newHorizonsAttackControllers::crippleRemoval(unit), spells::curePhysical::graveDustRemoval(unit)})
		if(!bonuses.empty())
		{
			remove(server, battle, unit.unitId(), bonuses);
			break;
		}
}

void discharge(ServerCallback & server, const CBattleInfoCallback & battle, const spells::Mechanics & mechanics,
	const battle::Unit & unit, int64_t mana, const nativeRules::CompiledMetamagicRules & policy)
{
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	if(!spell || !unit.alive() || mana <= 0)
		return;
	int64_t amount = policy.discharge(mana);
	if(!perk(mechanics.getHeroCaster(), MetamagicPerk::ArcaneRupture))
	{
		if(newHorizonsPassiveSpecialties::magicalDamageImmune(unit))
			return;
		// Target defenses only; no Spell Power, Spellcraft or other caster scaling.
		amount = spell->adjustRawDamage(nullptr, &unit, amount, 0, 0, 100, true, true, false);
		amount = newHorizonsCommand::mitigateIncomingDamage(battle, &unit, amount);
	}
	if(amount > 0)
	{
		const auto damageContext = mechanics.spellDamageContext(unit);
		nativeEvents::damage(context(server, battle), unit, amount, nullptr, true, spell, false, true, nullptr, &damageContext);
	}
}

void restore(ServerCallback & server, const CBattleInfoCallback & battle, const battle::Unit & unit, int64_t mana,
	const nativeRules::CompiledMetamagicRules & policy)
{
	if(!unit.alive() || unit.isGhost() || mana <= 0)
		return;
	// The native casualty ledger caps original composition and excludes unusable
	// remains. Temporary summons can regain only temporary bodies, never recruits.
	const auto power = unit.isSummoned() ? EHealPower::ONE_BATTLE : EHealPower::PERMANENT;
	nativeEvents::heal(context(server, battle), unit, policy.restoration(mana), EHealLevel::RESURRECT, power);
}

void disrupt(ServerCallback & server, const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	auto state = unit.acquireState();
	if(!state || !unit.alive() || !unit.defended() || state->armorerLastStandDefending)
		return;
	std::vector<Bonus> bonuses;
	const auto activeBonuses = unit.getBonuses(Selector::all, "all");
	for(const auto & bonus : *activeBonuses)
		if(bonus->source == BonusSource::OTHER && bonus->sid == BonusSourceID()
			&& bonus->duration == BonusDuration::STACK_GETS_TURN
			&& (bonus->type == BonusType::UNIT_DEFENDING
				|| (bonus->type == BonusType::PRIMARY_SKILL && bonus->subtype == BonusSubtypeID(PrimarySkill::DEFENSE))))
			bonuses.push_back(*bonus);
	remove(server, battle, unit.unitId(), bonuses);
	state = unit.acquireState();
	state->defending = false;
	state->defensiveStanceMeleeBonus = 0;
	state->defensiveStanceRangedBonus = 0;
	state->battlecraftDefendMasteryDoubled = false;
	nativeEvents::changeUnit(context(server, battle), *state);
}
}

void cancelDefend(ServerCallback & server, const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	disrupt(server, battle, unit);
}

RecipientSnapshots captureRecipients(const CBattleInfoCallback & battle, const spells::Mechanics & mechanics)
{
	RecipientSnapshots result;
	const auto * hero = mechanics.getHeroCaster();
	if(!battle.getBattle() || !mechanics.isGenuineHeroCast() || !hero || nativeRules::metamagicSkillRank(hero) <= 0
		|| (!nativeRules::metamagicRules(hero) || !nativeRules::metamagicRules(hero)->policy()))
		return result;
	if(nativeRules::metamagicRules(hero)->policy()->optionalRemoval
		&& !perk(hero, MetamagicPerk::UnravelingMagic) && !perk(hero, MetamagicPerk::ReconstitutingMagic)) return result;
	for(const auto * unit : battle.getBattle()->getUnitsIf([](const battle::Unit *) { return true; }))
		if(unit && unit->alive() && !unit->isGhost())
			result.emplace(unit->unitId(), RecipientSnapshot{
				effectSnapshot(battle, unit->unitId()), battle.battleGetOwner(unit)});
	return result;
}

bool singleTargetFormula(const CSpell & spell, int32_t mastery)
{
	const auto & level = spell.getLevelInfo(mastery);
	if(spell.getTargetType() != spells::AimType::CREATURE || level.range != std::vector<int>{0})
		return false;
	const std::function<bool(const JsonNode &)> hasChain = [&](const JsonNode & node)
	{
		if(node.isStruct())
			for(const auto & [key, child] : node.Struct())
				if(((key == "chainLength" && child.isNumber() && child.Float() > 1)
					|| (key == "handOfFate" && child.isBool() && child.Bool())) || hasChain(child))
					return true;
		if(node.isVector())
			for(const auto & child : node.Vector())
				if(hasChain(child))
					return true;
		return false;
	};
	return !hasChain(level.battleEffects);
}

CompletionReceipt completeCast(ServerCallback & server, const CBattleInfoCallback & battle,
	const spells::Mechanics & mechanics, const RecipientSnapshots & snapshots,
	std::span<const uint32_t> resolved, std::span<const BattleStackAttacked> injuries)
{
	CompletionReceipt receipt;
	const auto * hero = mechanics.getHeroCaster();
	const auto * spell = mechanics.getSpell();
	if(!mechanics.isGenuineHeroCast() || !hero || !spell || !battle.getBattle())
		return receipt;
	const auto rank = nativeRules::metamagicSkillRank(hero);
	const auto * policy = nativeRules::metamagicRules(hero);
	if(!policy || !policy->policy())
		return receipt;
	const int chance = policy->removalChance(rank, hero->getHeroTypeID());
	if(chance == 0)
		return receipt;
	ResolutionLedger ledger;
	const auto energy = nativeRules::captureMetamagicEnergy(*battle.getBattle());
	const auto resolver = [&](SpellID id) -> std::optional<EnergyDefinition>
	{
		const auto * definition = energy->fuelDefinition(id);
		return definition ? std::optional<EnergyDefinition>(*definition) : std::nullopt;
	};
	std::set<uint32_t> seen;
	for(const auto id : resolved)
	{
		const auto * unit = battle.battleGetUnitByID(id);
		const auto snapshot = snapshots.find(id);
		if(!unit || unit->isGhost() || snapshot == snapshots.end() || !seen.insert(id).second)
			continue;
		const auto owner = snapshot->second.originalOwner;
		if(owner == PlayerColor::CANNOT_DETERMINE || mechanics.getCasterColor() == PlayerColor::CANNOT_DETERMINE)
			continue;
		const bool friendly = owner == mechanics.getCasterColor();
		const bool restoration = friendly && spell->isPositive() && unit->alive()
			&& (!policy->policy()->optionalRemoval || perk(hero, MetamagicPerk::ReconstitutingMagic));
		const bool damagingHit = spell->isDamage() && std::any_of(injuries.begin(), injuries.end(),
			[id](const BattleStackAttacked & hit) { return hit.stackAttacked == id; });
		const bool offense = !friendly && (damagingHit || (!spell->isDamage() && spell->isNegative() && perk(hero, MetamagicPerk::HexWeaving)))
			&& (!policy->policy()->optionalRemoval || perk(hero, MetamagicPerk::UnravelingMagic));
		if(!restoration && !offense)
			continue;
		const auto kind = restoration ? ResolutionKind::RESTORATION : ResolutionKind::OFFENSE;
		for(const auto & candidate : collectRemovalCandidates(id, owner, snapshot->second.effects, kind, resolver))
		{
			const auto before = effectSnapshot(battle, id);
			if(!std::all_of(candidate.bonuses.begin(), candidate.bonuses.end(), [&](const Bonus & original)
				{ return std::any_of(before.effects.begin(), before.effects.end(),
					[&](const Bonus & current) { return battle::exactBattleEffectEqual(original, current); }); }))
				continue;
			if(!ledger.attemptPercent(candidate, chance, [&](int percent)
				{ return server.getRNG()->nextInt(0, 99) < percent; }))
				continue;
			remove(server, battle, id, candidate.bonuses);
			if(ledger.confirmRemoval(candidate, before, effectSnapshot(battle, id)))
			{
				receipt.removedEffects.push_back({id, candidate.family, kind});
				if(perk(hero, MetamagicPerk::MagicalMemory))
					rememberRemoval(server, battle, id, candidate.family, kind);
			}
		}
		if(restoration)
			restore(server, battle, *unit, ledger.removedMana(id), *policy);
		else
			discharge(server, battle, mechanics, *unit, ledger.removedMana(id), *policy);
	}
	receipt.removedMana = ledger.removedMana();
	return receipt;
}

void applyGuaranteedCompletion(ServerCallback & server, const CBattleInfoCallback & battle,
	const spells::Mechanics & mechanics, std::span<const uint32_t> resolved,
	std::span<const BattleStackAttacked> injuries)
{
	const auto * hero = mechanics.getHeroCaster();
	const auto * spell = mechanics.getSpell();
	if(!mechanics.isGenuineHeroCast() || !hero || !spell || !battle.getBattle()
		|| nativeRules::metamagicSkillRank(hero) <= 0 || (!nativeRules::metamagicRules(hero) || !nativeRules::metamagicRules(hero)->policy()))
		return;
	std::set<uint32_t> seen;
	for(const auto id : resolved)
	{
		const auto * unit = battle.battleGetUnitByID(id);
		if(!unit || !unit->alive() || !seen.insert(id).second)
			continue;
		if(perk(hero, MetamagicPerk::DisruptiveCasting)
			&& battle.battleGetOwner(unit) != mechanics.getCasterColor()
			&& std::any_of(injuries.begin(), injuries.end(),
				[id](const BattleStackAttacked & hit) { return hit.stackAttacked == id && hit.damageAmount > 0; }))
		{
			if(nativeRules::metamagicRules(hero)->policy()->optionalRemoval) destroyShots(server, battle, *unit);
			else disrupt(server, battle, *unit);
		}
		if(spell->isPositive() && battle.battleGetOwner(unit) == mechanics.getCasterColor()
			&& perk(hero, MetamagicPerk::PurgingFormula))
			purge(server, battle, *unit);
	}
}

int32_t refundCast(ServerCallback & server, const CBattleInfoCallback & battle, const spells::Mechanics & mechanics,
	const CompletionReceipt & receipt, const ManaPayment & actualPaid, const ManaPayment & alreadyRefunded,
	bool validSingleTarget, bool completelyResisted)
{
	const auto * hero = mechanics.getHeroCaster();
	if(!mechanics.isGenuineHeroCast() || !hero || !battle.getBattle() || nativeRules::metamagicSkillRank(hero) <= 0
		|| (!nativeRules::metamagicRules(hero) || !nativeRules::metamagicRules(hero)->policy()))
		return 0;
	if(actualPaid.normal < 0 || actualPaid.permanentBuffer < 0 || actualPaid.temporaryBuffer < 0
		|| alreadyRefunded.normal < 0 || alreadyRefunded.permanentBuffer < 0 || alreadyRefunded.temporaryBuffer < 0
		|| alreadyRefunded.normal > actualPaid.normal || alreadyRefunded.permanentBuffer > actualPaid.permanentBuffer
		|| alreadyRefunded.temporaryBuffer > actualPaid.temporaryBuffer || actualPaid.total() > std::numeric_limits<int32_t>::max())
		throw std::runtime_error("Metamagic refund requires valid actual payment provenance");
	const auto normalRoom = std::max(0, hero->manaLimit() - hero->getNormalSpellPoints());
	const ManaPayment remaining{actualPaid.normal - alreadyRefunded.normal,
		actualPaid.permanentBuffer - alreadyRefunded.permanentBuffer, actualPaid.temporaryBuffer - alreadyRefunded.temporaryBuffer};
	const auto room = static_cast<int32_t>(std::min<int64_t>(std::numeric_limits<int32_t>::max(),
		static_cast<int64_t>(std::min(normalRoom, remaining.normal)) + remaining.permanentBuffer + remaining.temporaryBuffer));
	const auto paid = static_cast<int32_t>(actualPaid.total());
	const auto refunded = static_cast<int32_t>(alreadyRefunded.total());
	const auto & policy = *nativeRules::metamagicRules(hero);
	int32_t amount = policy.conservedRefund(perk(hero, MetamagicPerk::ConservedFormula),
		paid, refunded, room, validSingleTarget, completelyResisted);
	amount += policy.recyclingRefund(perk(hero, MetamagicPerk::ManaRecycling),
		receipt.removedMana, paid, refunded + amount, room - amount);
	if(amount > 0)
	{
		const auto normal = std::min({amount, remaining.normal, normalRoom});
		const auto permanent = std::min(amount - normal, remaining.permanentBuffer);
		const auto temporary = amount - normal - permanent;
		SetMana packet;
		packet.hid = hero->id;
		packet.setOperation(SetMana::Operation::REFUND_HERO_LUCK, normal, permanent, temporary);
		server.apply(packet);
	}
	return amount;
}
}
