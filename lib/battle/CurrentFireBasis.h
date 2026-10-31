/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "HeroCommand.h"
#include "Unit.h"
class CGHeroInstance;
class JsonNode;
namespace newHorizonsMagicalAbilityDamage
{
/// Canonical rule response only; caller admits its captured/current rule domain.
DLL_LINKAGE bool usesMultiplicativeMDR(const JsonNode & magicRules);
/// Native or authorized capture interval ONLY; this may use the live Hero registry.
/// An opaque closed branch retains this response only for an explicitly immutable
/// Hero-policy epoch, selecting the CURRENT controlling side on every query.
DLL_LINKAGE int spellwardReductionBasisPoints(const CGHeroInstance * hero);
/// CurrentRound is invoked at the original lazy point, after all unit gates.
/// Neither this pure predicate nor a valid Order grants source/command admission.
template<class CurrentRound>
bool holdTheLineRecipient(const HeroOrderState & state, const battle::Unit * unit, const CurrentRound & currentRound)
{
	if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret()
		|| unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| unit->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
		return false;
	return state.isHoldTheLineRecipient(unit->unitId(), static_cast<int16_t>(unit->getPosition().toInt()), currentRound());
}
/// The engine supplies the same current recipient predicate; missing/stale
/// domains must throw before this call, never masquerade as an absent Order.
template<class Recipient>
int holdTheLineReductionBasisPoints(const HeroOrderState * state, const battle::Unit * unit, const Recipient & recipient)
{
	return state && recipient(*state, unit)
		? (state->crownAndAltarAppliesTo(unit->unitId())
			? state->crownAndAltarHoldReductionBasisPoints : state->holdMagicalReductionBasisPoints) : 0;
}
}
