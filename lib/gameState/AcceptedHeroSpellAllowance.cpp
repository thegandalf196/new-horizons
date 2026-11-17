/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AcceptedHeroSpellAllowance.h"
#include "../battle/BattleInfo.h"
#include "../battle/NewHorizonsDivineMandate.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../spells/AutomaticHeroCast.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSpellAvailability.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

AcceptedHeroSpellAllowanceResult prepareAndCommitAcceptedHeroSpellAllowance(
	BattleInfo & battle, const CGHeroInstance * hero, const BattleSpellCast & pack)
{
	auto & casterSide = battle.getSide(pack.side);
	const bool automaticHeroCast = pack.automaticHeroCast.automatic();
	if(automaticHeroCast && !spells::automaticHeroCastAllowed(battle, hero,
		pack.spellID.toSpell(), pack.automaticHeroCast))
		throw std::runtime_error("Invalid or stale automatic hero spell receipt");
	const bool sharedActionBudget = !automaticHeroCast && heroCommands::supportedByRules(
		battle.getHeroCommandRules(), HeroCommand::CHARGE);
	const auto lightSchoolNumber = sharedActionBudget ? SpellSchool::decode("new-horizons:light") : -1;
	const auto & savedMagicRules = battle.getMagicRules();
	const auto spellSchools = newHorizonsMagic::spellSchools(savedMagicRules, pack.spellID);
	const bool isLightSpell = newHorizonsMagic::spellAllowedByHeroRoster(savedMagicRules, pack.spellID)
		&& lightSchoolNumber >= 0
		&& std::find(spellSchools.begin(), spellSchools.end(), SpellSchool(lightSchoolNumber)) != spellSchools.end();
	const auto mandate = battle.battleGetDivineMandateStatus(pack.side);
	const bool mandateAvailable = mandate.active && mandate.completedPairs < mandate.maximumPairs;
	const auto spellGrantFilter = [isLightSpell, mandateAvailable](const HeroActionAllowanceState::Grant & grant)
	{
		return HeroActionAllowanceState::grantAllowedForSpell(grant, isLightSpell)
			&& (grant.source != HeroActionAllowanceState::GrantSource::DIVINE_MANDATE || mandateAvailable);
	};
	std::optional<HeroActionAllowanceState::Receipt> spellTransition;
	if(sharedActionBudget)
	{
		auto nextAllowances = casterSide.heroActionAllowances;
		const auto selected = nextAllowances.eligibleAllowance(
			HeroActionAllowanceState::ActionKind::SPELL, battle.getRound(), spellGrantFilter);
		if(!selected)
			throw std::runtime_error("Accepted Hero spell has no eligible action allowance");
		auto transition = nextAllowances.consumeAllowance(selected->grantId,
			HeroActionAllowanceState::ActionKind::SPELL, battle.getRound(), spellGrantFilter);
		if(!transition)
			throw std::runtime_error("Accepted Hero spell has forged or inconsistent allowance metadata");
		const auto recipients = isLightSpell
			&& newHorizonsDivineMandate::needsRecipientCapture(hero)
			? newHorizonsDivineMandate::sharedPurposeFriendlyRecipients(battle, pack.side, pack.affectedCres, true)
			: std::vector<uint32_t>{};
		DivineMandateTransition::applyAcceptedAction(nextAllowances, *transition,
			battle.getRound(), isLightSpell, mandate.maximumPairs, recipients);
		casterSide.heroActionAllowances = std::move(nextAllowances);
		spellTransition = std::move(*transition);
	}
	const bool spendsHeroAllowance = !automaticHeroCast && (spellTransition
		? spellTransition->allowance == HeroActionAllowanceState::AllowanceKind::HERO
		: true);
	if(pack.automaticHeroCast.kind == spells::AutomaticHeroCastKind::DIVINE_MANDATE)
		casterSide.passiveDivineMandateUses = pack.automaticHeroCast.mandateUseOrdinal;
	return {automaticHeroCast, sharedActionBudget, spendsHeroAllowance, std::move(spellTransition)};
}
