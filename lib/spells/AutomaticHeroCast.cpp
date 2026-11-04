/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AutomaticHeroCast.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/PassiveDivineMandate.h"
#include "../mapObjects/CGHeroInstance.h"

#include <algorithm>
#include <stdexcept>

namespace spells
{
void AutomaticHeroCastReceipt::validateShape() const
{
	switch(kind)
	{
	case AutomaticHeroCastKind::NONE:
	case AutomaticHeroCastKind::DIVINE_FURY:
		if(mandateUseOrdinal != 0 || issuingOrder != HeroCommand::NONE
			|| issuingRound != 0 || inspiringDuration != 0)
			throw std::runtime_error("Unexpected automatic Divine Mandate activation ordinal");
		return;
	case AutomaticHeroCastKind::DIVINE_MANDATE:
		if(mandateUseOrdinal == 0 || mandateUseOrdinal > 5
			|| !heroCommands::isActive(issuingOrder) || issuingRound < 1 || inspiringDuration > 3)
			throw std::runtime_error("Invalid automatic Divine Mandate activation ordinal");
		return;
	}
	throw std::runtime_error("Unknown automatic hero spell provenance");
}

void AutomaticHeroCastReceipt::validateEnvelope(bool castByHero, bool activeCast,
	int32_t casterStack, BattleSide side, int32_t paidHeroManaCost) const
{
	validateShape();
	if(automatic() && (!castByHero || !activeCast || casterStack != -1
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| paidHeroManaCost != 0))
		throw std::runtime_error("Invalid automatic hero spell envelope");
}

bool automaticHeroCastAllowed(const CBattleInfoCallback & battle, const CGHeroInstance * hero,
	const CSpell * spell, AutomaticHeroCastReceipt receipt)
{
	receipt.validateShape();
	if(!receipt.automatic() || !hero || !spell || !battle.getBattle())
		return false;
	const auto side = battle.playerToSide(hero->getCasterOwner());
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| battle.battleGetFightingHero(side) != hero
		|| !spell->isCombat() || !newHorizonsMagic::spellAllowedByBattleRoster(battle, spell->getId()))
		return false;
	const auto policy = newHorizonsPassiveDivineMandate::capturePolicy(hero);
	if(policy.rank == 0)
		return false;
	if(receipt.kind == AutomaticHeroCastKind::DIVINE_FURY)
		return policy.has(newHorizonsPassiveDivineMandate::Perk::DIVINE_FURY)
			&& spell->getJsonKey() == "new-horizons:holyWrath";
	const auto order = battle.getBattle()->getHeroOrderState(side, receipt.issuingOrder);
	if(!order || order->issuedRound != receipt.issuingRound
		|| receipt.issuingRound != battle.battleGetRound()
		|| order->passiveDivineMandate != std::optional{policy})
		return false;
	const int scheduledDuration = order->revision && order->command == HeroCommand::VANGUARD
		? order->revision->expiresAfterRound - order->issuedRound + 1 : 1;
	const int expectedInspiringDuration = policy.has(newHorizonsPassiveDivineMandate::Perk::INSPIRING_LEADERSHIP)
		? std::clamp(scheduledDuration, 1, 3) : 0;
	if(receipt.inspiringDuration != expectedInspiringDuration)
		return false;
	const auto next = newHorizonsPassiveDivineMandate::afterAcceptedCast(policy,
		{battle.getBattle()->getPassiveDivineMandateUses(side)});
	if(!next || next->completedCasts != receipt.mandateUseOrdinal
		|| !hero->hasSpellbook()
		|| !hero->getSpellsInSpellbook().contains(spell->getId())
		|| !hero->canCastThisSpell(spell)
		|| !newHorizonsMagic::hasSchoolProficiency(hero, spell->getId())
		|| !spell->isPositive())
		return false;
	const auto schools = newHorizonsMagic::spellSchools(hero->getMagicRules(), spell->getId());
	const int light = SpellSchool::decode("new-horizons:light");
	if(light < 0 || std::find(schools.begin(), schools.end(), SpellSchool(light)) == schools.end())
		return false;
	const auto family = newHorizonsMagic::spellVariantBase(hero->getMagicRules(), spell->getId());
	return family != SpellID(SpellID::RESURRECTION) && family != SpellID(SpellID::ANIMATE_DEAD)
		&& family != SpellID(SpellID::SACRIFICE);
}
}
