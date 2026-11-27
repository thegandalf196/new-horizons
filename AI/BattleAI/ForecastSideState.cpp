/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastSideState.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/LiveUnitEnvironmentContext.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/NewHorizonsNecropolisCombatSpecialties.h"
#include "../../lib/nativeRules/CapturedRuleSet.h"
#include "../../lib/nativeRules/CapturedPerkRules.h"
#include "../../lib/nativeRules/CompiledMetamagicRules.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/constants/NumericConstants.h"
namespace forecast
{
bool SideState::knowsSpell(SpellID spell) const
{
	return magicOwner && magicOwner->metamagicEnergy() && magicOwner->metamagicEnergy()->identity(spell);
}
void SideState::validate() const
{
	if(knowledge != VisibleHeroPolicy::Knowledge::UNAVAILABLE && knowledge != VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN
		&& knowledge != VisibleHeroPolicy::Knowledge::KNOWN_ABSENT && knowledge != VisibleHeroPolicy::Knowledge::VISIBLE_HERO)
		throw std::invalid_argument("Invalid forecast Hero knowledge state");
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) || round < 0 || !magicOwner
		|| magicOwner->kind() != nativeRules::CapturedRuleKind::WorldMagic || nextCastToken == 0
		|| (attunementMask & ~newHorizonsElementalAttunement::ALL_ELEMENTS) != 0)
		throw std::invalid_argument("Invalid compact forecast side identity");
	if(hasVisibleHero() != bool(hero) || hasVisibleHero() != bool(mana) || hasVisibleHero() != bool(ledger)
		|| (!hasVisibleHero() && (perkOwner || !castCharges.empty())))
		throw std::invalid_argument("Unknown or absent forecast hero cannot supply a resource ledger");
	if(mana) mana->validate();
	if(ledger)
	{
		ledger->allowances.validateShape(); ledger->wisdom.validate(); ledger->response.validate();
		if(ledger->allowances.currentRound >= 0 && ledger->allowances.currentRound != round)
			throw std::invalid_argument("Compact action ledger belongs to a different round");
		ledger->overwhelming.validateShape();
		if(ledger->residualLifeDamage < 0 || ledger->residualLifeThreshold < 0 || ledger->residualLifeLimit < 0)
			throw std::invalid_argument("Invalid Residual Life forecast state");
		if(ledger->completedLevelMask >= (1u << GameConstants::SPELL_LEVELS)
			|| (ledger->completedCastCount && *ledger->completedCastCount < 0))
			throw std::invalid_argument("Invalid compact forecast cast completion state");
	}
	if(attunementKnown) attunement.validate();
	uint64_t prior = 0;
	for(const auto & charge : castCharges)
	{
		charge.paid.validate(); charge.refunded.validate();
		if(charge.token <= prior || charge.token >= nextCastToken || !knowsSpell(charge.spell)
			|| charge.refunded.normal > charge.paid.normal
			|| charge.refunded.permanentBuffer > charge.paid.permanentBuffer
			|| charge.refunded.temporaryBuffer > charge.paid.temporaryBuffer)
			throw std::invalid_argument("Invalid compact forecast cast payment history");
		prior = charge.token;
	}
}
std::optional<SideState> SideState::capture(const CBattleInfoCallback & source, BattleSide side)
{
	if(!source.getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)) return std::nullopt;
	try
	{
		SideState result;
		result.side = side; result.controller = source.getBattle()->getSidePlayer(side);
		result.round = source.battleGetRound();
		result.typedActions = heroCommands::supportedByRules(source.getBattle()->getHeroCommandRules(), HeroCommand::CHARGE);
		const auto & rawMagic = source.getBattle()->getMagicRules();
		result.magicOwner = source.getBattle()->getCapturedMagicRules();
		if(!result.magicOwner || result.magicOwner->kind() != nativeRules::CapturedRuleKind::WorldMagic
			|| &result.magicOwner->document() != &rawMagic)
			result.magicOwner = nativeRules::CapturedRuleDocument::compile(nativeRules::CapturedRuleKind::WorldMagic, rawMagic);
		const auto visible = source.battleGetVisibleHeroPolicy(side);
		result.knowledge = visible.kind();
		if(result.hasVisibleHero())
		{
			const auto * hero = visible.legacyHero();
			if(!hero) return std::nullopt;
			result.hero = hero->id; result.perkOwner = hero->getPerkState().capturedOwner();
			battle::spellPoints::State mana;
			mana.twoPools = newHorizonsMagic::spellPointRulesActive(hero->getMagicRules());
			mana.normalCapacity = hero->manaLimit();
			mana.pools.restoreSnapshot(hero->getNormalSpellPoints(), hero->getBufferSpellPoints(), std::numeric_limits<int32_t>::max());
			mana.temporaryBufferRemaining = source.getBattle()->getTemporaryBufferRemaining(side);
			result.mana = mana;
			HeroLedger ledger;
			ledger.residualLifeDamage = source.getBattle()->getResidualLifeDamage(side);
			ledger.residualLifeThreshold = newHorizonsHeroes::residualLifeThreshold(hero);
			ledger.residualLifeLimit = newHorizonsHeroes::residualLifeLimit(hero);
			ledger.allowances = source.getBattle()->getHeroActionAllowances(side);
			ledger.wisdom = source.getBattle()->getWisdomCombatState(side);
			ledger.response = source.getBattle()->getSpellResponseState(side);
			ledger.overwhelming = source.getBattle()->getOverwhelmingFormulaState(side);
			ledger.completedHeroCast = source.getBattle()->hasCompletedHeroSpellCast(side);
			for(int level = 1; level <= GameConstants::SPELL_LEVELS; ++level)
				if(source.getBattle()->hasCompletedHeroSpellLevel(side, level)) ledger.completedLevelMask |= 1u << (level - 1);
			ledger.completedCastCount = source.getBattle()->getCastSpells(side);
			result.ledger = std::move(ledger);
		}
		const battle::LiveUnitEnvironmentContext context(source);
		// Certified absence supplies a known empty profile, without inspecting
		// the opposing side's raw profile or borrowing native occurrence facts.
		result.attunementKnown = result.knowledge == VisibleHeroPolicy::Knowledge::KNOWN_ABSENT
			|| context.knowsElementalAttunementProfile(side);
		if(result.attunementKnown && result.knowledge != VisibleHeroPolicy::Knowledge::KNOWN_ABSENT)
			result.attunement = source.getBattle()->getElementalAttunementProfile(side);
		result.attunementMask = source.getBattle()->getElementalAttunementMask(side);
		result.validate();
		return result;
	}
	catch(const std::exception &) { return std::nullopt; }
}
}
