/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../battle/NewHorizonsSylvanTacticsV2.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "NewHorizonsSpellAvailability.h"
#include "effects/native/StatusHelpers.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/Unit.h"
#include "../CCreatureHandler.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/SetStackEffect.h"
#include <limits>
namespace newHorizonsSylvanTacticsV2
{
namespace
{
bool rampart(const battle::Unit * unit)
{
	return unit && unit->creatureId().toCreature()->getFactionID() == FactionID::RAMPART;
}
int dwarfCap(const spells::Mechanics & cast, const battle::Unit * recipient)
{
	return cast.battle() && cast.isNegativeSpell() && family(recipient, Family::Dwarf)
		? perkValue(commandingHero(*cast.battle(), recipient), "forestSFavor", "maximumNegativeSpellRounds") : 0;
}
int evergreen(const spells::Mechanics & cast, const battle::Unit * recipient)
{
	return cast.isGenuineHeroCast() && cast.isPositiveSpell() && rampart(recipient)
		&& cast.ownerMatches(recipient, true)
		? perkValue(cast.getHeroCaster(), "sharedFortune", "beneficialDurationBonusRounds") : 0;
}
void adjustBonus(const spells::Mechanics & cast, const battle::Unit * recipient, Bonus & bonus)
{
	if(bonus.source != BonusSource::SPELL_EFFECT || !spells::effects::native::status::isTemporary(bonus)) return;
	const auto spell = bonus.sid.as<SpellID>();
	const bool negative = spell != SpellID::NONE && spell.toSpell()->isNegative();
	const int maximum = cast.battle() && negative && family(recipient, Family::Dwarf)
		? perkValue(commandingHero(*cast.battle(), recipient), "forestSFavor", "maximumNegativeSpellRounds") : 0;
	if(maximum > 0)
	{
		// Add a round bound while retaining every native early-break condition.
		const bool rounded = bonus.duration & BonusDuration::N_TURNS;
		bonus.duration |= BonusDuration::N_TURNS;
		bonus.turnsRemain = rounded ? std::min<int>(bonus.turnsRemain, maximum) : maximum;
	}
	else if((bonus.duration & BonusDuration::N_TURNS) && bonus.turnsRemain > 0
		&& (bonus.sid == BonusSourceID(cast.getSpellId()) || bonus.sid == BonusSourceID(newHorizonsMagic::effectSpellForCast(cast)->getId())))
		bonus.turnsRemain = static_cast<int16_t>(std::min<int>(std::numeric_limits<int16_t>::max(),
			bonus.turnsRemain + evergreen(cast, recipient)));
}
}
int adjustedTimedRounds(const spells::Mechanics & cast, const battle::Unit * recipient, int rounds)
{
	if(rounds <= 0 || !recipient) return rounds;
	if(const int maximum = dwarfCap(cast, recipient)) return std::min(rounds, maximum);
	return static_cast<int>(std::min<int64_t>(std::numeric_limits<int>::max(),
		static_cast<int64_t>(rounds) + evergreen(cast, recipient)));
}
void adjustAppliedBonuses(const spells::Mechanics & cast, SetStackEffect & packet)
{
	if(!cast.battle()) return;
	for(auto * changes : {&packet.toAdd, &packet.toUpdate})
		for(auto & [id, bonuses] : *changes)
		{
			const auto * recipient = cast.battle()->battleGetUnitByID(id);
			if(!recipient) continue;
			for(auto & bonus : bonuses) adjustBonus(cast, recipient, bonus);
		}
}
bool preservesBuffFromDispel(const spells::Mechanics & cast, const battle::Unit * recipient, const Bonus & bonus)
{
	if(!cast.battle() || !recipient || cast.ownerMatches(recipient, true)
		|| bonus.source != BonusSource::SPELL_EFFECT || !spells::effects::native::status::isTemporary(bonus)) return false;
	const auto spell = bonus.sid.as<SpellID>();
	if(spell == SpellID::NONE || !spell.toSpell()->isPositive()) return false;
	for(const auto * provider : cast.battle()->battleAdjacentUnits(recipient))
		if(family(provider, Family::Unicorn) && provider->alive() && !provider->isGhost()
			&& cast.battle()->battleGetOwner(provider) == cast.battle()->battleGetOwner(recipient)
			&& provider->hasBonusOfType(BonusType::SPELL_RESISTANCE_AURA)
			&& hasPerk(commandingHero(*cast.battle(), provider), "fortunateAim")) return true;
	return false;
}
bool ignoresNativeLevelImmunity(const spells::Mechanics & cast, const battle::Unit * recipient, const Bonus & bonus)
{
	return cast.isGenuineHeroCast() && cast.isPositiveSpell() && recipient && cast.ownerMatches(recipient, true)
		&& rampart(recipient) && hasPerk(cast.getHeroCaster(), "cascadingFortune")
		&& bonus.type == BonusType::LEVEL_SPELL_IMMUNITY && bonus.source == BonusSource::CREATURE_ABILITY
		&& bonus.sid == BonusSourceID(recipient->creatureId());
}
bool levelImmunityAllows(const spells::Mechanics & cast, const battle::Unit * recipient, bool absolute)
{
	if(!cast.isMagicalEffect() || cast.getSpellLevel() <= 0) return true;
	BonusList remaining;
	for(const auto bonusList = recipient->getBonusesOfType(BonusType::LEVEL_SPELL_IMMUNITY); const auto & bonus : *bonusList)
		if(!ignoresNativeLevelImmunity(cast, recipient, *bonus)) remaining.push_back(bonus);
	if(absolute && std::ranges::none_of(remaining, [](const auto & bonus)
		{ return bonus->parameters && bonus->parameters->toNumber() == 1; })) return true;
	return remaining.empty() || remaining.totalValue() < cast.getSpellLevel();
}
}
