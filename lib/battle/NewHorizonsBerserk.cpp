/*
 * NewHorizonsBerserk.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsBerserk.h"

#include "CBattleInfoCallback.h"
#include "BattleInfo.h"
#include "NewHorizonsPuppetMaster.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../bonuses/BonusSelector.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr std::string_view FRENZIED_CURSE_SKILL = "new-horizons:chaosMagic";
constexpr std::string_view FRENZIED_CURSE_PERK = "new-horizons:chaosMagic.frenziedCurse";
constexpr std::string_view FRENZIED_CURSE_STACKING = "new-horizons:chaosMagic.frenziedCurse.forced-activation-speed";

SpellID berserkSpellID()
{
	return SpellID(SpellID::BERSERK);
}
}

namespace newHorizonsBerserk
{
bool isFrenziedCurseSpeedBonus(const Bonus * bonus)
{
	return bonus
		&& bonus->duration == BonusDuration::STACK_ACTIVATION
		&& bonus->type == BonusType::STACKS_SPEED
		&& bonus->val == 2
		&& bonus->valType == BonusValueType::ADDITIVE_VALUE
		&& bonus->source == BonusSource::SPELL_EFFECT
		&& bonus->sid == BonusSourceID(berserkSpellID())
		&& bonus->stacking == FRENZIED_CURSE_STACKING
		&& bonus->spellCasterOwner.isValidPlayer();
}

std::optional<Bonus> forcedActivationSpeedBonus(const CBattleInfoCallback & battle,
	const battle::Unit * unit)
{
	if(!unit || !battle.getBattle())
		return std::nullopt;

	const auto & savedRules = battle.getBattle()->getMagicRules();
	const auto berserkSpell = berserkSpellID();
	if(!newHorizonsMagic::berserkUsesSingleCreatureTarget(savedRules)
		|| !newHorizonsMagic::spellAllowedBySavedRoster(savedRules, berserkSpell)
		|| newHorizonsPuppetMaster::hasValidControlMarker(battle, unit))
		return std::nullopt;

	const auto berserkMarkers = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(berserkSpell)).And(Selector::type()(BonusType::ATTACKS_NEAREST_CREATURE)));
	if(!berserkMarkers)
		return std::nullopt;

	for(const auto & marker : *berserkMarkers)
	{
		if(!marker || !marker->spellCasterOwner.isValidPlayer())
			continue;

		const auto casterSide = battle.playerToSide(marker->spellCasterOwner);
		if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
			continue;

		const auto * casterHero = battle.battleGetFightingHero(casterSide);
		if(!casterHero || !casterHero->hasActivePerk(std::string(FRENZIED_CURSE_SKILL),
			std::string(FRENZIED_CURSE_PERK)))
			continue;

		Bonus speed(BonusDuration::STACK_ACTIVATION, BonusType::STACKS_SPEED,
			BonusSource::SPELL_EFFECT, 2, BonusSourceID(berserkSpell));
		speed.stacking = std::string(FRENZIED_CURSE_STACKING);
		speed.spellCasterOwner = marker->spellCasterOwner;
		speed.description.appendRawString("Frenzied Curse");
		return speed;
	}

	return std::nullopt;
}
}
