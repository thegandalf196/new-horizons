/*
 * StackSpellStatusPresentation.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "NewHorizonsBattleStatus.h"
#include "../../lib/spells/NewHorizonsTheChosen.h"
#include "../../lib/spells/CSpell.h"

namespace newHorizonsStackSpellPresentation
{
struct ChosenStatus
{
	int remainingRounds = 0;
	bool doubleStrike = false;
	bool unlimitedRetaliations = false;
	bool flying = false;
	int joustingPercent = 0;
};

template<typename BonusRange>
inline ChosenStatus chosenStatus(const BonusRange & bonuses)
{
	ChosenStatus result;
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->source != BonusSource::SPELL_EFFECT
			|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0)
			continue;
		try
		{
			if(bonus->sid.toString() != newHorizonsTheChosen::SPELL_KEY)
				continue;
		}
		catch(const std::exception &)
		{
			continue;
		}
		switch(bonus->type)
		{
			case BonusType::ADDITIONAL_ATTACK: result.doubleStrike |= bonus->val > 0; break;
			case BonusType::UNLIMITED_RETALIATIONS: result.unlimitedRetaliations = true; break;
			case BonusType::FLYING: result.flying = true; break;
			case BonusType::JOUSTING: result.joustingPercent = std::max(result.joustingPercent, bonus->val); break;
			default: continue;
		}
		result.remainingRounds = result.remainingRounds == 0 ? bonus->turnsRemain
			: std::min(result.remainingRounds, static_cast<int>(bonus->turnsRemain));
	}
	return result;
}

inline std::string chosenTooltip(std::string_view description, const ChosenStatus & status)
{
	std::string result(description);
	result += "\n\nCurrent The Chosen abilities:";
	result += status.doubleStrike ? "\nDouble Strike: active (offensive attacks)." : "\nDouble Strike: inactive.";
	result += status.unlimitedRetaliations ? "\nUnlimited Retaliations: active." : "\nUnlimited Retaliations: inactive.";
	result += status.flying ? "\nFlying: active." : "\nFlying: inactive.";
	result += "\nJousting: +" + std::to_string(status.joustingPercent) + "% damage per hex moved.";
	result += "\n" + newHorizonsBattleStatus::roundsRemaining(status.remainingRounds);
	result += "\nOnly one stack per army may be The Chosen. A successful recast replaces that army's previous recipient.";
	return result;
}

inline newHorizonsBattleStatus::StackStatusIconKind statusIconKind(SpellID effect)
{
	const auto spellKey = effect.toSpell()->getJsonKey();
	if(newHorizonsBattleStatus::isConfusion(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::CONFUSION;
	if(newHorizonsBattleStatus::isTimeStop(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::TIME_STOP;
	if(newHorizonsBattleStatus::isEntangle(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::ENTANGLE;
	if(newHorizonsBattleStatus::isSpellLock(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::SPELL_LOCK;
	if(newHorizonsBattleStatus::isDoom(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::DOOM;
	if(newHorizonsBattleStatus::isDivineRetribution(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::DIVINE_RETRIBUTION;
	if(newHorizonsBattleStatus::isGuardianSpirit(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::GUARDIAN_SPIRIT;
	if(newHorizonsBattleStatus::isHeavenlyGale(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::HEAVENLY_GALE;
	if(spellKey == newHorizonsTheChosen::SPELL_KEY)
		return newHorizonsBattleStatus::StackStatusIconKind::THE_CHOSEN;
	if(newHorizonsBattleStatus::isCrusade(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::CRUSADE;
	if(newHorizonsBattleStatus::isRegeneration(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::REGENERATION;
	if(newHorizonsBattleStatus::isShadowGift(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::SHADOW_GIFT_BUFF;
	if(newHorizonsBattleStatus::isVampirism(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::VAMPIRISM;
	if(newHorizonsBattleStatus::isFocusMagic(spellKey) || newHorizonsBattleStatus::isArcaneBreach(spellKey))
		return newHorizonsBattleStatus::StackStatusIconKind::FOCUS_OR_ARCANE;
	return newHorizonsBattleStatus::StackStatusIconKind::ORDINARY;
}

}
