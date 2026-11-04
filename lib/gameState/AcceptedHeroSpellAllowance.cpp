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
#include "../spells/NewHorizonsMetamagicMarks.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

AcceptedHeroSpellAllowanceResult prepareAndCommitAcceptedHeroSpellAllowance(
	BattleInfo & battle, const CGHeroInstance * hero, const BattleSpellCast & pack)
{
	auto & casterSide = battle.getSide(pack.side);
	const bool passiveMetamagic = hero && (newHorizonsMetamagic::usesPoints(hero->getPerkState().rules)
		|| newHorizonsMetamagic::usesArcaneMarks(hero->getPerkState().rules));
	if(passiveMetamagic && (pack.metamagicFollowup || pack.metamagicGrand || pack.metamagicManaRefund != 0))
		throw std::runtime_error("Passive Metamagic cannot spend a legacy Metamagic Spell Action");
	const bool automaticHeroCast = pack.automaticHeroCast.automatic();
	if(automaticHeroCast && !spells::automaticHeroCastAllowed(battle, hero,
		pack.spellID.toSpell(), pack.automaticHeroCast))
		throw std::runtime_error("Invalid or stale automatic hero spell receipt");
	const int actualMetamagicCapacity = newHorizonsMagic::metamagicCapacity(hero);
	if(!automaticHeroCast && (casterSide.metamagicUsesConsumed > actualMetamagicCapacity
		|| (casterSide.metamagicPendingCount != 0 && casterSide.metamagicSequenceSpells.size() == 1
			&& casterSide.metamagicUsesConsumed >= actualMetamagicCapacity)))
		throw std::runtime_error("Metamagic consumption/reservation exceeds actual fighting hero capacity");
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
	std::optional<HeroSpellAllowanceTransition::Result> spellTransition;
	if(pack.metamagicGrand && !pack.metamagicFollowup)
		throw std::runtime_error("Grand Metamagic metadata requires a follow-up cast");
	if(pack.metamagicManaRefund > 0
		&& (!pack.metamagicFollowup || pack.metamagicGrand || casterSide.metamagicPendingCount != 1
			|| !hero || !newHorizonsMagic::spellPointRulesActive(hero->getMagicRules())
			|| !newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_FORMULA_RESERVE)))
		throw std::runtime_error("Invalid Formula Reserve Metamagic refund");
	if(sharedActionBudget)
	{
		auto nextAllowances = casterSide.heroActionAllowances;
		auto nextMetamagicUsesConsumed = casterSide.metamagicUsesConsumed;
		auto nextMetamagicPendingCount = casterSide.metamagicPendingCount;
		auto nextMetamagicGrandUsed = casterSide.metamagicGrandUsed;
		const auto selected = nextAllowances.eligibleAllowance(
			HeroActionAllowanceState::ActionKind::SPELL, battle.getRound(), spellGrantFilter);
		if(!selected)
			throw std::runtime_error("Accepted Hero spell has no eligible action allowance");
		auto transition = HeroSpellAllowanceTransition::commitAcceptedCast(
			nextAllowances, selected->grantId, battle.getRound(),
			pack.metamagicFollowup, pack.metamagicGrand,
			static_cast<uint8_t>(hero && !passiveMetamagic ? newHorizonsMagic::metamagicRank(hero) : 0),
			hero && newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
			nextMetamagicUsesConsumed, nextMetamagicPendingCount,
			nextMetamagicGrandUsed, casterSide.metamagicSequenceSpells.size(), spellGrantFilter,
			static_cast<uint8_t>(newHorizonsMagic::metamagicCapacity(hero)));
		if(!transition)
			throw std::runtime_error("Accepted Hero spell has forged or inconsistent allowance metadata");
		const auto recipients = isLightSpell && !pack.counterspellNegated
			&& newHorizonsDivineMandate::needsRecipientCapture(hero)
			? newHorizonsDivineMandate::sharedPurposeFriendlyRecipients(battle, pack.side, pack.affectedCres, true)
			: std::vector<uint32_t>{};
		DivineMandateTransition::applyAcceptedAction(nextAllowances, transition->receipt,
			battle.getRound(), isLightSpell, mandate.maximumPairs, recipients);
		casterSide.heroActionAllowances = std::move(nextAllowances);
		casterSide.metamagicUsesConsumed = nextMetamagicUsesConsumed;
		casterSide.metamagicPendingCount = nextMetamagicPendingCount;
		casterSide.metamagicGrandUsed = nextMetamagicGrandUsed;
		spellTransition = std::move(*transition);
	}
	else if(pack.metamagicFollowup && casterSide.metamagicPendingCount == 0)
		throw std::runtime_error("Metamagic follow-up cast without pending sequence");
	const bool spendsHeroAllowance = !automaticHeroCast && (spellTransition
		? spellTransition->receipt.allowance == HeroActionAllowanceState::AllowanceKind::HERO
		: !pack.metamagicFollowup);
	if(pack.automaticHeroCast.kind == spells::AutomaticHeroCastKind::DIVINE_MANDATE)
		casterSide.passiveDivineMandateUses = pack.automaticHeroCast.mandateUseOrdinal;
	return {automaticHeroCast, sharedActionBudget, spendsHeroAllowance, std::move(spellTransition)};
}
