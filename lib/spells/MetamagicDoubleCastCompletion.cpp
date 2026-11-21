/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MetamagicDoubleCastCompletion.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsMetamagicResolution.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/CUnitState.h"
#include "../battle/IBattleState.h"
#include "../battle/MetamagicDoubleCast.h"
#include "../battle/NewHorizonsSwiftRebirth.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../nativeRules/CompiledMetamagicRules.h"
#include "../networkPacks/PacksForClient.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../networkPacks/SetStackEffect.h"
#include <vcmi/ServerCallback.h>
#include <algorithm>
#include <map>
#include <set>

namespace metamagicDoubleCast
{
namespace
{
using nativeRules::DoubleCastPerkSet;
using Rules = nativeRules::CompiledMetamagicRules;

struct Completion
{
	ServerCallback & server;
	const CBattleInfoCallback & battle;
	BattleSide side;
	const CGHeroInstance & hero;
	const Rules & rules;
	DoubleCastPerkSet perks;
	BattleLogMessage log;
};

battle::Units friendlyStacks(const CBattleInfoCallback & battle, BattleSide side)
{
	return battle.battleGetUnitsIf([side](const battle::Unit * unit)
	{
		return unit && unit->unitSide() == side && unit->alive() && !unit->isGhost() && !unit->isTurret();
	});
}

/// One localized log line: "%s" is the hero name, "%d" the amount.
void line(Completion & completion, const char * textID, int64_t amount)
{
	MetaString entry = MetaString::createFromTextID(textID);
	entry.replaceTextID(completion.hero.getNameTextID());
	entry.replaceNumber(amount);
	completion.log.lines.push_back(std::move(entry));
}

/// Bonuses whose refresh is not the ordinary timed max-duration update are left alone.
bool extendable(const Bonus & bonus, const std::set<SpellID> & spells)
{
	return bonus.source == BonusSource::SPELL_EFFECT && spells.contains(bonus.sid.as<SpellID>())
		&& (bonus.duration & BonusDuration::N_TURNS) != 0 && bonus.turnsRemain > 0
		&& bonus.type != BonusType::GUARDIAN_SPIRIT && bonus.type != BonusType::PHYSICAL_AFFLICTION
		&& bonus.type != BonusType::CONFUSION_PENDING && !newHorizonsSwiftRebirth::isLifecycleMarker(bonus);
}

/// Afterglow: each timed effect of either spell is extended once, even when both
/// spells share a variant family or a unit received both.
void afterglow(Completion & completion, const MetamagicDoubleCastState & before, SpellID followUp,
	const std::vector<uint32_t> & followUpUnits)
{
	const int rounds = completion.rules.afterglowAdditionalRounds(completion.perks);
	if(rounds <= 0)
		return;
	const auto & magic = completion.battle.getBattle()->getMagicRules();
	std::map<uint32_t, std::set<SpellID>> identities;
	const auto add = [&](SpellID spell, const std::vector<uint32_t> & units)
	{
		for(const auto id : units)
		{
			identities[id].insert(spell);
			identities[id].insert(newHorizonsMagic::spellVariantBase(magic, spell));
		}
	};
	add(before.firstSpell, before.firstSpellAffectedUnits);
	add(followUp, followUpUnits);
	SetStackEffect packet;
	packet.battleID = completion.battle.getBattle()->getBattleID();
	int64_t extended = 0;
	for(const auto & [id, spells] : identities)
	{
		const auto * unit = completion.battle.battleGetUnitByID(id);
		if(!unit || !unit->alive() || unit->isGhost())
			continue;
		std::vector<Bonus> updates;
		for(auto bonus : completion.battle.getBattle()->getLocalSpellEffects(id))
			if(extendable(bonus, spells))
			{
				bonus.turnsRemain += rounds;
				updates.push_back(std::move(bonus));
			}
		extended += static_cast<int64_t>(updates.size());
		if(!updates.empty())
			packet.toUpdate.emplace_back(id, std::move(updates));
	}
	if(packet.toUpdate.empty())
		return;
	completion.server.apply(packet);
	line(completion, "new-horizons.logs.doubleCast.afterglow", extended);
}

/// Cleansing Weave: the existing Metamagic restoration classification (hostile,
/// temporary, magical common hero spells); the most expensive effects go first.
void cleansingWeave(Completion & completion)
{
	const int limit = completion.rules.cleansingWeaveEffects(completion.perks);
	if(limit <= 0)
		return;
	const auto energy = nativeRules::captureMetamagicEnergy(*completion.battle.getBattle());
	const auto resolver = [&energy](SpellID id) -> std::optional<newHorizonsMetamagic::EnergyDefinition>
	{
		const auto * definition = energy->fuelDefinition(id);
		return definition ? std::optional(*definition) : std::nullopt;
	};
	SetStackEffect packet;
	packet.battleID = completion.battle.getBattle()->getBattleID();
	int64_t removed = 0;
	for(const auto * unit : friendlyStacks(completion.battle, completion.side))
	{
		battle::BattleEffectSnapshot snapshot;
		snapshot.effects = completion.battle.getBattle()->getLocalSpellEffects(unit->unitId());
		auto candidates = newHorizonsMetamagic::collectRemovalCandidates(unit->unitId(),
			completion.battle.battleGetOwner(unit), snapshot, newHorizonsMetamagic::ResolutionKind::RESTORATION, resolver);
		std::stable_sort(candidates.begin(), candidates.end(), [](const auto & lhs, const auto & rhs)
		{
			return lhs.listedMana != rhs.listedMana ? lhs.listedMana > rhs.listedMana
				: lhs.family.getNum() < rhs.family.getNum();
		});
		std::vector<Bonus> bonuses;
		for(size_t index = 0; index < candidates.size() && index < static_cast<size_t>(limit); ++index)
			bonuses.insert(bonuses.end(), candidates[index].bonuses.begin(), candidates[index].bonuses.end());
		removed += std::min<int64_t>(limit, static_cast<int64_t>(candidates.size()));
		if(!bonuses.empty())
			packet.toRemove.emplace_back(unit->unitId(), std::move(bonuses));
	}
	if(packet.toRemove.empty())
		return;
	completion.server.apply(packet);
	line(completion, "new-horizons.logs.doubleCast.cleansingWeave", removed);
}

/// Arcane Ward: non-stacking; each friendly stack keeps the larger of its remaining
/// ward and the new one. The unit state carries it; round start clears it.
void arcaneWard(Completion & completion)
{
	const auto power = int64_t{completion.hero.getPrimSkillLevel(PrimarySkill::SPELL_POWER)}
		+ crescendoSpellPower(*completion.battle.getBattle(), completion.side, &completion.hero);
	const int amount = completion.rules.arcaneWardShield(static_cast<int>(std::clamp<int64_t>(power, 0,
		std::numeric_limits<int>::max())), completion.perks);
	if(amount <= 0)
		return;
	BattleUnitsChanged changes;
	changes.battleID = completion.battle.getBattle()->getBattleID();
	for(const auto * unit : friendlyStacks(completion.battle, completion.side))
	{
		auto state = unit->acquireState();
		if(!state || !state->arcaneWard.refresh(amount))
			continue;
		UnitChanges update(unit->unitId(), UnitChanges::EOperation::UPDATE);
		update.data = state->save();
		changes.changedStacks.push_back(std::move(update));
	}
	if(changes.changedStacks.empty())
		return;
	completion.server.apply(changes);
	line(completion, "new-horizons.logs.doubleCast.arcaneWard", amount);
}

/// Restores Normal Mana under the ordinary cap and returns what was actually gained.
int32_t restore(Completion & completion, int32_t amount)
{
	if(amount <= 0)
		return 0;
	const auto before = completion.hero.getNormalSpellPoints();
	completion.hero.spendMana(&completion.server, -amount);
	return std::max(0, completion.hero.getNormalSpellPoints() - before);
}

/// Mana Siphon drains only what the enemy hero can pay; Formula Reserve is a flat refund.
void mana(Completion & completion)
{
	const auto * enemy = completion.battle.battleGetFightingHero(completion.battle.otherSide(completion.side));
	const auto siphon = enemy ? static_cast<int32_t>(std::clamp<int64_t>(enemy->getManaAvailable(), 0,
		completion.rules.manaSiphonMana(completion.perks))) : 0;
	if(siphon > 0)
	{
		enemy->spendMana(&completion.server, siphon);
		line(completion, "new-horizons.logs.doubleCast.manaSiphon", siphon);
		restore(completion, siphon);
	}
	if(const auto reserve = restore(completion, completion.rules.formulaReserveMana(completion.perks)))
		line(completion, "new-horizons.logs.doubleCast.formulaReserve", reserve);
}
}

void applyCompletionEffects(ServerCallback & server, const CBattleInfoCallback & battle,
	BattleSide side, const MetamagicDoubleCastState & beforeFollowUp, SpellID followUp,
	const std::vector<uint32_t> & followUpUnits)
{
	const auto * info = battle.getBattle();
	if(!info || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| info->getMetamagicDoubleCastState(side).completedDoubleCasts <= beforeFollowUp.completedDoubleCasts
		|| !beforeFollowUp.firstSpell.hasValue())
		return;
	const auto * hero = battle.battleGetFightingHero(side);
	const auto * rules = hero ? nativeRules::metamagicRules(hero) : nullptr;
	if(!rules || !rules->doubleCastPolicy())
		return;
	Completion completion{server, battle, side, *hero, *rules, nativeRules::doubleCastPerks(hero), {}};
	completion.log.battleID = info->getBattleID();
	afterglow(completion, beforeFollowUp, followUp, followUpUnits);
	cleansingWeave(completion);
	arcaneWard(completion);
	mana(completion);
	if(!completion.log.lines.empty())
		server.apply(completion.log);
}
}
