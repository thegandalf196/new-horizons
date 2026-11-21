/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MetamagicDoubleCast.h"
#include "BattleAction.h"
#include "BattleInfo.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../nativeRules/CompiledMetamagicRules.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

void MetamagicDoubleCastState::validate() const
{
	if(usesSpent > MAX_COUNT || usesRegained > usesSpent || completedDoubleCasts > usesSpent)
		throw std::runtime_error("Invalid Double Cast use counters");
	if(!offerOpen())
	{
		if(offerRound != -1 || offerActivationSerial != -1 || firstSpell != SpellID::NONE
			|| !firstSpellAffectedUnits.empty())
			throw std::runtime_error("Closed Double Cast offer retains provenance");
		return;
	}
	if(offerRound < 0 || offerActivationSerial < 0 || !firstSpell.hasValue()
		|| firstSpellAffectedUnits.size() > MAX_AFFECTED_UNITS
		|| !std::is_sorted(firstSpellAffectedUnits.begin(), firstSpellAffectedUnits.end())
		|| std::adjacent_find(firstSpellAffectedUnits.begin(), firstSpellAffectedUnits.end())
			!= firstSpellAffectedUnits.end())
		throw std::runtime_error("Invalid open Double Cast offer");
}

namespace metamagicDoubleCast
{
namespace
{
using Source = Ledger::GrantSource;

void removeGrant(Ledger & ledger, uint32_t grantId)
{
	std::erase_if(ledger.grants, [grantId](const Ledger::Grant & grant)
	{
		return grant.id == grantId && grant.source == Source::METAMAGIC_DOUBLE_CAST;
	});
	ledger.validateShape();
}

void forfeit(SideInBattle & side)
{
	if(!side.metamagicDoubleCast.offerOpen())
		return;
	auto ledger = side.heroActionAllowances;
	removeGrant(ledger, side.metamagicDoubleCast.offerGrantId);
	side.heroActionAllowances = std::move(ledger);
	side.metamagicDoubleCast.closeOffer();
}

const nativeRules::CompiledMetamagicRules * doubleCastRules(const CGHeroInstance * hero)
{
	const auto * rules = hero ? nativeRules::metamagicRules(hero) : nullptr;
	return rules && rules->doubleCastPolicy() ? rules : nullptr;
}

std::vector<uint32_t> canonicalUnits(std::vector<uint32_t> units)
{
	std::sort(units.begin(), units.end());
	units.erase(std::unique(units.begin(), units.end()), units.end());
	if(units.size() > MetamagicDoubleCastState::MAX_AFFECTED_UNITS)
		units.resize(MetamagicDoubleCastState::MAX_AFFECTED_UNITS);
	return units;
}

MetamagicDoubleCastState completeFollowUp(MetamagicDoubleCastState next, const CGHeroInstance * hero,
	SpellID spell, const Ledger::Receipt & receipt)
{
	const auto * rules = doubleCastRules(hero);
	if(!rules || !next.offerOpen() || receipt.grantId != next.offerGrantId
		|| receipt.action != Ledger::ActionKind::SPELL || receipt.allowance != Ledger::AllowanceKind::SPELL)
		throw std::runtime_error("Double Cast follow-up has no matching open offer");
	if(rules->followUpMustDiffer() && spell == next.firstSpell)
		throw std::runtime_error("Double Cast follow-up repeats the first spell");
	if(next.usesSpent >= MetamagicDoubleCastState::MAX_COUNT)
		throw std::runtime_error("Double Cast use counter exhausted");
	++next.usesSpent;
	++next.completedDoubleCasts;
	next.closeOffer();
	// TODO(Metamagic 6, phase 4): completion effects read these counters here or in
	// the authority after the follow-up resolves: Arcane Ward shield, Mana Siphon,
	// Formula Reserve Mana, Cleansing Weave; Crescendo's Spell Power and the
	// Spellstorm cost stage read usesSpent/completedDoubleCasts.
	return next;
}
}

bool offerGranted(const Ledger & ledger)
{
	return std::any_of(ledger.grants.begin(), ledger.grants.end(), [](const Ledger::Grant & grant)
	{
		return grant.source == Source::METAMAGIC_DOUBLE_CAST;
	});
}

int maximumUses(const CGHeroInstance * hero)
{
	const auto * rules = doubleCastRules(hero);
	if(!rules)
		return 0;
	return rules->maxUses(nativeRules::metamagicSkillRank(hero), nativeRules::doubleCastPerks(hero),
		rules->isHalon(hero->getHeroTypeID()));
}

int remainingUses(const CGHeroInstance * hero, const MetamagicDoubleCastState & state)
{
	return std::max(0, maximumUses(hero) - state.netUsesSpent());
}

MetamagicDoubleCastState acceptHeroSpell(const BattleInfo & battle, BattleSide side,
	const CGHeroInstance * hero, SpellID spell, const std::vector<uint32_t> & affectedUnits,
	Ledger & ledger, const Ledger::Receipt & receipt)
{
	auto next = battle.getSide(side).metamagicDoubleCast;
	if(receipt.source == Source::METAMAGIC_DOUBLE_CAST)
		return completeFollowUp(std::move(next), hero, spell, receipt);
	// A cast paid by any other allowance is "something else": the offer lapses for free.
	if(next.offerOpen())
	{
		removeGrant(ledger, next.offerGrantId);
		next.closeOffer();
	}
	const int32_t round = battle.getRound();
	if(receipt.allowance == Ledger::AllowanceKind::HERO && receipt.action == Ledger::ActionKind::SPELL
		&& round >= 0 && ledger.currentRound == round && remainingUses(hero, next) > 0
		&& ledger.nextGrantId != std::numeric_limits<uint32_t>::max())
	{
		next.offerGrantId = ledger.grantAllowance(Ledger::AllowanceKind::SPELL, Source::METAMAGIC_DOUBLE_CAST, round);
		next.offerRound = round;
		next.offerActivationSerial = std::max(0, battle.getActivationSerial());
		next.firstSpell = spell;
		next.firstSpellAffectedUnits = canonicalUnits(affectedUnits);
	}
	next.validate();
	return next;
}

void forfeitOnStartAction(BattleInfo & battle, const BattleAction & action)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(action.side != side || action.actionType != EActionType::HERO_SPELL)
			forfeit(battle.getSide(side));
}

void expireOnActivation(BattleInfo & battle)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		forfeit(battle.getSide(side));
}

void startRound(BattleInfo & battle, int32_t round)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & state = battle.getSide(side);
		forfeit(state);
		const auto * hero = battle.getSideHero(side);
		const auto * rules = doubleCastRules(hero);
		auto & doubleCast = state.metamagicDoubleCast;
		if(rules && doubleCast.netUsesSpent() > 0
			&& rules->arcaneCycleRestores(round, nativeRules::doubleCastPerks(hero)))
			++doubleCast.usesRegained;
	}
}

int crescendoSpellPower(const IBattleInfo & battle, BattleSide side, const CGHeroInstance * hero)
{
	const auto * rules = doubleCastRules(hero);
	const auto perks = rules ? nativeRules::doubleCastPerks(hero) : 0;
	// The perk check precedes any battle-state query: other rules never read it.
	if(!rules || rules->crescendoSpellPower(1, perks) == 0
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return 0;
	return rules->crescendoSpellPower(battle.getMetamagicDoubleCastState(side).usesSpent, perks);
}

namespace
{
std::optional<BattleSide> fightingSide(const CGHeroInstance & hero, const CBattleInfoCallback & battle)
{
	const auto * info = battle.getBattle();
	const auto side = battle.playerToSide(hero.getOwner());
	if(!info || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) || info->getSideHero(side) != &hero)
		return std::nullopt;
	return side;
}
}

int crescendoSpellPower(const CGHeroInstance & hero, const CBattleInfoCallback & battle)
{
	if(!doubleCastRules(&hero))
		return 0;
	const auto side = fightingSide(hero, battle);
	return side ? crescendoSpellPower(*battle.getBattle(), *side, &hero) : 0;
}

int spellstormReductionPercent(const CBattleInfoCallback & battle, const CGHeroInstance & hero)
{
	const auto * rules = doubleCastRules(&hero);
	const auto perks = rules ? nativeRules::doubleCastPerks(&hero) : 0;
	if(!rules || rules->spellstormManaReductionPercent(MetamagicDoubleCastState::MAX_COUNT, perks) == 0)
		return 0;
	const auto side = fightingSide(hero, battle);
	return side ? rules->spellstormManaReductionPercent(
		battle.getBattle()->getMetamagicDoubleCastState(*side).completedDoubleCasts, perks) : 0;
}

bool followUpRepeatsFirstSpell(const CBattleInfoCallback & battle, BattleSide side, SpellID spell)
{
	const auto * info = battle.getBattle();
	if(!info || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !offerGranted(info->getHeroActionAllowances(side)))
		return false;
	const auto & offer = info->getMetamagicDoubleCastState(side);
	const auto * rules = doubleCastRules(info->getSideHero(side));
	if(!offer.offerOpen() || spell != offer.firstSpell || !rules || !rules->followUpMustDiffer())
		return false;
	const auto selected = battle.battleGetSpellActionAllowance(side, spell);
	return selected && selected->source == Source::METAMAGIC_DOUBLE_CAST;
}

std::optional<std::string> actionRefusal(const CBattleInfoCallback & battle, const BattleAction & action)
{
	if((action.side != BattleSide::ATTACKER && action.side != BattleSide::DEFENDER) || !battle.getBattle())
		return std::nullopt;
	if(action.actionType == EActionType::HERO_COMMAND)
	{
		const auto selected = battle.battleGetOrderActionAllowance(action.side);
		if(selected && selected->source == Source::METAMAGIC_DOUBLE_CAST)
			return std::string("A Double Cast follow-up cannot issue an Order");
		return std::nullopt;
	}
	if(action.actionType != EActionType::HERO_SPELL
		|| !offerGranted(battle.getBattle()->getHeroActionAllowances(action.side)))
		return std::nullopt;
	const auto selected = battle.battleGetSpellActionAllowance(action.side, action.spell);
	if(!selected || selected->source != Source::METAMAGIC_DOUBLE_CAST)
		return std::nullopt;
	const auto * info = dynamic_cast<const BattleInfo *>(battle.getBattle());
	if(!info)
		return std::string("Double Cast follow-up cannot be verified outside the authoritative battle");
	const auto & offer = info->getSide(action.side).metamagicDoubleCast;
	if(!offer.offerOpen() || offer.offerGrantId != selected->grantId || offer.offerRound != info->getRound()
		|| offer.offerActivationSerial != info->getActivationSerial())
		return std::string("Double Cast follow-up has no open offer for this side and activation");
	const auto * rules = doubleCastRules(info->getSideHero(action.side));
	if(!rules)
		return std::string("Double Cast follow-up requires the hero's captured Double Cast rules");
	if(rules->followUpMustDiffer() && action.spell == offer.firstSpell)
		return std::string("The Double Cast follow-up must be a different spell");
	return std::nullopt;
}
}
