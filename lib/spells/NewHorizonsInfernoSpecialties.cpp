/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsInfernoSpecialties.h"
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "../battle/IBattleState.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/Unit.h"
#include "../battle/NewHorizonsSpecialtyProcs.h"
#include "../battle/StackEffectRefresh.h"
#include "../bonuses/BonusList.h"
#include "../entities/hero/NewHorizonsInfernoSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/SetStackEffect.h"
#include "../GameLibrary.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"

namespace infernoSpellSpecialties
{
namespace
{
bool fireSpell(const IBattleInfo & battle, SpellID spell)
{
	const auto * definition = spell.hasValue() ? spell.toSpell() : nullptr;
	return definition && definition->getDamageElement() == SpellDamageElement::FIRE
		&& newHorizonsMagic::spellAllowedBySavedRoster(battle.getMagicRules(), spell)
		&& !newHorizonsMagic::isAdventureSpell(battle.getMagicRules(), spell);
}
bool ownHeroFireball(const spells::Mechanics & mechanics)
{
	return mechanics.getSpellId() == SpellID::FIREBALL && mechanics.isGenuineHeroCast()
		&& !mechanics.isRunicReflectionShare() && mechanics.getHeroCaster()
		&& mechanics.battle() && mechanics.battle()->getBattle()
		&& mechanics.battle()->getBattle()->getSideHero(mechanics.getEffectCasterSide()) == mechanics.getHeroCaster();
}
bool friendly(const spells::Mechanics & mechanics, const battle::Unit & unit)
{
	return mechanics.battle()->battleGetOwner(&unit) == mechanics.battle()->sideToPlayer(mechanics.getEffectCasterSide());
}
std::vector<Bonus> newestBuffs(const IBattleInfo & battle, const battle::Unit & unit, int count)
{
	const auto effects = battle.getLocalSpellEffects(unit.unitId());
	std::vector<Bonus> result;
	std::vector<std::pair<BonusSourceID, std::string>> selected;
	// Positive spell refresh packets move refreshed records to the end of this saved order.
	for(auto it = effects.rbegin(); it != effects.rend(); ++it)
	{
		if(!newHorizonsSpecialtyProcs::isPositiveTemporaryMagic(*it)) continue;
		const auto identity = std::pair{it->sid, it->statusIdentity};
		if(std::ranges::find(selected, identity) == selected.end())
		{
			if(static_cast<int>(selected.size()) >= count) continue;
			selected.push_back(identity);
		}
	}
	for(const auto & effect : effects)
		if(std::ranges::find(selected, std::pair{effect.sid, effect.statusIdentity}) != selected.end())
			result.push_back(effect);
	return result;
}
}
void preserveRefreshChronology(const CBattleInfoCallback & callback, SetStackEffect & packet)
{
	const auto * battle = callback.getBattle();
	if(!battle || packet.exchange
		|| (newHorizonsHeroes::burnAwayEffects(battle->getSideHero(BattleSide::ATTACKER)) == 0
			&& newHorizonsHeroes::burnAwayEffects(battle->getSideHero(BattleSide::DEFENDER)) == 0))
		return;
	for(auto & [id, incoming] : packet.toUpdate)
	{
		const auto local = battle->getLocalSpellEffects(id);
		std::vector<Bonus> retained;
		std::vector<Bonus> removed;
		std::vector<Bonus> refreshed;
		for(const auto & bonus : incoming)
		{
			bool found = false;
			for(const auto & previous : local)
			{
				if(!newHorizonsSpecialtyProcs::isPositiveTemporaryMagic(previous)
					|| !stackEffectRefresh::matchesIdentity(bonus, &previous)) continue;
				removed.push_back(previous);
				auto next = previous;
				stackEffectRefresh::refreshFields(next, bonus);
				refreshed.push_back(std::move(next));
				found = true;
			}
			if(!found) retained.push_back(bonus);
		}
		incoming = std::move(retained);
		if(!removed.empty()) packet.toRemove.emplace_back(id, std::move(removed));
		if(!refreshed.empty()) packet.toAdd.emplace_back(id, std::move(refreshed));
	}
	std::erase_if(packet.toUpdate, [](const auto & entry) { return entry.second.empty(); });
}
int kindlingBonusBasisPoints(const IBattleInfo & battle, BattleSide side, SpellID spell)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return 0;
	const auto * hero = battle.getSideHero(side);
	const auto * rules = newHorizonsHeroes::infernoRules(hero, "core:xyron");
	if(!rules || !rules->kindling || !fireSpell(battle, spell)) return 0;
	const auto history = battle.getUsedSpells(side);
	int consecutive = 0;
	for(auto it = history.rbegin(); it != history.rend() && fireSpell(battle, *it); ++it)
		++consecutive;
	const auto & row = *rules->kindling;
	const int cap = std::min(row.maximumPercent * 100, static_cast<int>(hero->level) * row.capPermillePerLevel * 10);
	return static_cast<int>(std::min<int64_t>(cap, static_cast<int64_t>(consecutive) * row.percentPerCast * 100));
}
bool bloodfireProtects(const spells::Mechanics & mechanics, const battle::Unit & unit)
{
	return ownHeroFireball(mechanics) && friendly(mechanics, unit)
		&& newHorizonsHeroes::bloodfireAttack(mechanics.getHeroCaster()) > 0;
}
void applyFireballRider(ServerCallback & server, const spells::Mechanics & mechanics, const battle::Unit & unit)
{
	if(!ownHeroFireball(mechanics)) return;
	SetStackEffect packet;
	packet.battleID = mechanics.battle()->getBattle()->getBattleID();
	if(bloodfireProtects(mechanics, unit))
	{
		const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell", "new-horizons:bloodfireBuff", false);
		if(!id) throw std::runtime_error("Missing Bloodfire effect source");
		Bonus bonus(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT,
			newHorizonsHeroes::bloodfireAttack(mechanics.getHeroCaster()), BonusSourceID(SpellID(*id)),
			BonusSubtypeID(PrimarySkill::ATTACK));
		bonus.turnsRemain = newHorizonsHeroes::bloodfireDuration(mechanics.getHeroCaster());
		bonus.spellCasterOwner = mechanics.getHeroCaster()->getOwner();
		bonus.stacking = "new-horizons:bloodfire";
		std::vector<Bonus> previous;
		for(const auto & existing : mechanics.battle()->getBattle()->getLocalSpellEffects(unit.unitId()))
			if(existing.stacking == bonus.stacking) previous.push_back(existing);
		if(!previous.empty()) packet.toRemove.emplace_back(unit.unitId(), previous);
		packet.toAdd.emplace_back(unit.unitId(), std::vector<Bonus>{bonus});
	}
	else if(!friendly(mechanics, unit))
	{
		const int count = newHorizonsHeroes::burnAwayEffects(mechanics.getHeroCaster());
		if(count > 0)
		{
			auto removed = newestBuffs(*mechanics.battle()->getBattle(), unit, count);
			if(!removed.empty()) packet.toRemove.emplace_back(unit.unitId(), std::move(removed));
		}
	}
	if(!packet.toAdd.empty() || !packet.toRemove.empty()) server.apply(packet);
}
}
