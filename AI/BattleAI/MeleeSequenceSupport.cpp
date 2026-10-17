/*
 * MeleeSequenceSupport.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "MeleeSequenceSupport.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "StackWithBonuses.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"

#include <set>
#include <tuple>

namespace
{
bool unambiguousStacking(const battle::Unit & unit, const HypotheticBattle & battle)
{
	// The legacy projected merge does not restack parent/local overlays. Do not
	// claim a native-equivalent sequence when that unresolved difference matters.
	std::set<const Bonus *> identities;
	std::set<std::tuple<std::string, BonusType, BonusSubtypeID, BonusValueType>> groups;
	// Reject ambiguity in a legacy merge unless its exact native stacking route
	// is attested below. Native stacking need not erase all repeated pointers.
	const auto bonuses = unit.getAllBonuses(Selector::all);
	if(bonuses->size() > 8192)
		return false; // Bounds this scan, not the legacy collector's allocation.
	if(std::ranges::any_of(*bonuses, [](const auto & bonus) { return !bonus; }))
		return false;
	const auto subject = battle.initialExportSubject();
	const auto * projected = dynamic_cast<const StackWithBonuses *>(&unit);
	// Exact owned-origin queries use the SAME collector and stackBonuses as
	// native. Preserve even native duplicate multiplicity; this heuristic only
	// proves the legacy merge, never rewrites an authoritative query result.
	if(subject && projected && projected->usesCanonicalMovementQuery(battle)
		&& !subject->world().hasUnresolvedRecipientOverlays())
		return true;
	for(const auto & bonus : *bonuses)
	{
		if(bonus->stacking.empty())
		{
			if(!identities.insert(bonus.get()).second)
				return false;
		}
		else if(bonus->stacking != "ALWAYS"
			&& !groups.emplace(bonus->stacking, bonus->type, bonus->subtype, bonus->valType).second)
			return false;
	}
	return true;
}

bool supportedHero(const CGHeroInstance & hero)
{
	const int armorer = SecondarySkill::decode(newHorizonsArmorer::SKILL_ID);
	for(const auto & [skill, rank] : hero.secSkills)
		if(rank != 0 && skill.getNum() != armorer)
			return false;
	for(const auto & selected : hero.getPerkState().selected)
	{
		if(!hero.hasActivePerk(selected.skillId, selected.perkId))
			continue;
		if(selected.skillId != newHorizonsArmorer::SKILL_ID
			|| (selected.perkId != "new-horizons:armorer.pavise"
				&& selected.perkId != "new-horizons:armorer.countercharge"
				&& selected.perkId != "new-horizons:armorer.veteran"
				&& selected.perkId != "new-horizons:armorer.bastion"
				&& selected.perkId != newHorizonsArmorer::LAST_STAND_PERK_ID))
			return false;
	}
	return true;
}

bool supportedUnit(const battle::Unit & unit, const HypotheticBattle & battle)
{
	const auto state = unit.acquireState();
	if(!state || !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&unit)
		|| unit.isShooter() // Native melee hits also stamp shooter Crossfire history.
		|| unit.unitSlot() == SlotID::WAR_MACHINES_SLOT
		|| unit.isClone() || state->cloneID >= 0 || state->summoned || state->natureSummoned || unit.isTimeStopped()
		|| unit.getPhantomInitialIntegrity() > 0 || unit.getGuardianSpiritHitPoints() > 0
		|| newHorizonsFrozen::isFrozen(unit)
		|| newHorizonsFrozen::isFreezingTouchAttacker(unit, battle.getMagicRules()))
		return false;
	if(!unambiguousStacking(unit, battle))
		return false;
	return !unit.hasBonus(CSelector([](const Bonus * bonus)
	{
		return bonus->source == BonusSource::SPELL_EFFECT
			|| bonus->type == BonusType::COMBAT_EVENT_TRIGGER || bonus->type == BonusType::ON_COMBAT_EVENT
			|| bonus->type == BonusType::SPELL_BEFORE_ATTACK || bonus->type == BonusType::SPELL_AFTER_ATTACK
			|| bonus->type == BonusType::DOUBLE_DAMAGE_CHANCE || bonus->type == BonusType::FEROCITY
			|| bonus->type == BonusType::RETURN_AFTER_STRIKE;
	}));
}

bool singleTarget(const HypotheticBattle & battle, const battle::Unit & actor, const battle::Unit & victim,
	BattleHex selectedTargetHex)
{
	const auto targets = battle.getAttackedBattleUnits(&actor, &victim, selectedTargetHex, false,
		actor.getPosition(), victim.getPosition());
	return std::ranges::all_of(targets, [&victim](const auto * unit) { return unit->unitId() == victim.unitId(); });
}
}

namespace meleeSequenceSupport
{
std::optional<heroOrderValue::MeleeSequenceStop> unavailablePolicies(const CBattleInfoCallback & battle)
{
	using Stop = heroOrderValue::MeleeSequenceStop;
	if(!battle.getBattle())
		return Stop::INPUT_OR_MOVEMENT_UNAVAILABLE;
	// nullptr from an enemy-view query is NOT evidence of no enemy hero/policy.
	if(battle.battleGetMySide() != BattleSide::ALL_KNOWING)
		return Stop::HIDDEN_HERO_POLICY;
	if(battle.getBattle()->getCrisisCommandState().meaningful())
		return Stop::UNSUPPORTED_REACTION_POLICY;
	const auto roster = battle.battleGetUnitsIf([](const battle::Unit *) { return true; });
	if(roster.size() > 256 || std::ranges::any_of(roster, [](const battle::Unit * unit)
	{
		return !unit || unit->hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER)
			|| unit->hasBonusOfType(BonusType::ON_COMBAT_EVENT);
	}))
		return Stop::UNSUPPORTED_REACTION_POLICY;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		// Captured battle profiles need not equal today's hero skill selection.
		if(battle.getBattle()->getBloodrageRank(side) != 0)
			return Stop::UNSUPPORTED_REACTION_POLICY;
		const auto * hero = battle.battleGetFightingHero(side);
		if(hero && (hero->hasActivePerk("new-horizons:offense", "new-horizons:offense.noQuarter")
			|| hero->hasActivePerk(newHorizonsArmorer::SKILL_ID, "new-horizons:armorer.defiant")))
			return Stop::NO_QUARTER_OR_DEFIANT_PENDING;
		if(hero && (!supportedHero(*hero) || hero->hasBonusOfType(BonusType::BONUS_DAMAGE_CHANCE)))
			return Stop::UNSUPPORTED_REACTION_POLICY;
		for(const auto & order : battle.getBattle()->getHeroOrderStates(side))
			if(order.command != HeroCommand::BRACE)
				return Stop::UNSUPPORTED_REACTION_POLICY;
	}
	return std::nullopt;
}

std::optional<heroOrderValue::MeleeSequenceStop> unavailable(HypotheticBattle & battle,
	const battle::Unit & actor, const battle::Unit & victim, BattleHex selectedTargetHex)
{
	using Stop = heroOrderValue::MeleeSequenceStop;
	if(const auto policy = unavailablePolicies(battle))
		return policy;
	if(battle.hasReadyNativeRebirth(&actor) || battle.hasReadyNativeRebirth(&victim)
		|| battle.captureElementalRebirthSource(actor) || battle.captureElementalRebirthSource(victim))
		return Stop::REBIRTH_PENDING;
	if(!supportedUnit(actor, battle) || !supportedUnit(victim, battle)
		|| !singleTarget(battle, actor, victim, selectedTargetHex)
		|| !singleTarget(battle, victim, actor, actor.getPosition()))
		return Stop::UNSUPPORTED_REACTION_POLICY;
	return std::nullopt; // No completed-stage capability or utility is manufactured here.
}
}
