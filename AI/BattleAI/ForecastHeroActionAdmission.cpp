/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ForecastHeroActionAdmission.h"
#include "../../lib/spells/NewHorizonsMetamagicOverflow.h"
#include "ForecastEffectState.h"
#include "HypotheticBattle.h"
#include "PossibleSpellcast.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/battle/NewHorizonsFocusMagic.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsStun.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"
#include "../../lib/spells/CurePhysicalAffliction.h"
#include "../../lib/battle/PassiveDivineMandate.h"
#include "../../lib/battle/ConsecratedArms.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/nativeRules/CompiledMetamagicRules.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMetamagicArea.h"
#include "../../lib/spells/NewHorizonsImplosion.h"
#include "../../lib/spells/NewHorizonsSorceryRevision.h"
#include "../../lib/spells/NewHorizonsSorcerySpells.h"
#include "../../lib/spells/effects/native/Damage.h"
#include "../../lib/spells/effects/native/Timed.h"
#include <typeinfo>

namespace forecast::heroAction
{
namespace
{
using Effect = spells::effects::Effect;
using DamageEffect = spells::effects::native::Damage;
using TimedEffect = spells::effects::native::Timed;
Decision refusal(Reason reason, EventMask required = 0)
{
	return {Route::CurrentNativeReference, reason, required};
}
bool ordinaryOrder(HeroCommand order)
{
	switch(order)
	{
	case HeroCommand::CHARGE: case HeroCommand::HOLD_THE_LINE:
	case HeroCommand::FOCUS_FIRE: case HeroCommand::RIPOSTE:
	case HeroCommand::BRACE: case HeroCommand::FEINT: return true;
	default: return false;
	}
}
bool eventsSupported(const State & state, EventMask required)
{
	// Side v3 owns resources separately from unit event recipes. Reaction is a
	// closure obligation, whose mutations must appear in the other event bits.
	for(unsigned index = 0; index < static_cast<unsigned>(Event::Count); ++index)
	{
		const auto event = static_cast<Event>(index);
		if(event == Event::HeroResource || event == Event::Reaction) continue;
		if((required & eventBit(event)) && !state.supports(event)) return false;
	}
	return true;
}
bool rowsSupported(const std::vector<Bonus> & rows, const spells::Mechanics & mechanics,
	bool focus)
{
	forecastEffects::Admission admission;
	admission.round = mechanics.battle()->battleGetRound();
	if(focus) admission.focusMagic = mechanics.getSpellId();
	std::vector<forecastEffects::RawRecord> raw;
	for(const auto & row : rows)
	{
		const auto record = std::make_shared<const Bonus>(row);
		raw.push_back({record, {0, raw.size()}, Bonus::NTurns(record.get()), true});
	}
	try { (void)forecastEffects::State::capture(raw, admission); return true; }
	catch(const std::invalid_argument &) { return false; }
}
Reason inspectSupportedEffectsImpl(const spells::Mechanics & mechanics, const spells::Target & target,
	EventMask & required)
{
	bool seen = false;
	Reason reason = Reason::None;
	mechanics.forEachEffect([&](const Effect & effect)
	{
		seen = true;
		if(!supportedImplementation(effect)) reason = Reason::UnsupportedEffect;
		else if(!static_cast<const spells::effects::native::UnitEffect &>(effect).supportsFiniteForecastTargeting(mechanics))
			reason = Reason::UnsafeTargeting;
		else if(typeid(effect) == typeid(DamageEffect)
			&& !static_cast<const DamageEffect &>(effect).supportsFiniteForecast()) reason = Reason::UnsupportedEffect;
		else if(typeid(effect) == typeid(TimedEffect)
			&& !static_cast<const TimedEffect &>(effect).supportsFiniteForecastPayload()) reason = Reason::UnprovenTimedPayload;
		return reason != Reason::None;
	});
	if(!seen) return Reason::UnsupportedEffect;
	if(reason != Reason::None) return reason;
	// ALL loaded implementations and descriptors are attested before the first
	// transform. Empty recipients are not proof of a supported Timed payload.
	const auto targets = mechanics.getAffectedStacks(target);
	if(targets.empty()) return Reason::InvalidTarget;
	mechanics.forEachEffect([&](const Effect & effect)
	{
		if(typeid(effect) == typeid(DamageEffect))
		{
			required |= eventBit(Event::Damage) | eventBit(Event::Effect);
			if(newHorizonsImplosion::rulesFor(mechanics.battle()->getBattle()->getMagicRules(), mechanics.getSpellId()))
			{ reason = Reason::UnsupportedEffect; return true; }
			return false;
		}
		const bool focus = nativeSpells::isFocusEnchantmentEffect(effect);
		required |= eventBit(Event::Effect);
		for(const auto * native : targets)
		{
			if(!native) { reason = Reason::InvalidTarget; return true; }
			const auto * unit = mechanics.battle()->battleGetUnitByID(native->unitId());
			if(!unit) { reason = Reason::InvalidTarget; return true; }
			std::optional<std::vector<Bonus>> rows;
			if(focus)
				rows = newHorizonsFocusMagic::enchantment(*unit, mechanics.getSpellId(),
					newHorizonsSorceryRevision::focusAdditionalRangedPercent(mechanics.getHeroCaster(), mechanics.getSpellId()));
			else rows = static_cast<const TimedEffect &>(effect).preparedRecipientBonuses(&mechanics, unit);
			if(!rows || !rowsSupported(*rows, mechanics, focus))
			{ reason = Reason::UnprovenTimedPayload; return true; }
		}
		return false;
	});
	return reason;
}
Reason metamagicClosure(const CBattleInfoCallback & source, const spells::Mechanics & mechanics,
	const spells::Target & target, EventMask & required)
{
	const auto * hero = mechanics.getHeroCaster();
	const auto * policy = nativeRules::metamagicRules(hero);
	if(nativeRules::metamagicSkillRank(hero) <= 0) return Reason::None;
	if(!policy || !policy->policy()) return Reason::UnsupportedCompletion;
	using Perk = nativeRules::MetamagicPerk;
	const auto has = [&](Perk perk) { return nativeRules::hasMetamagicPerk(hero, perk); };
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	if(!spell) return Reason::CustomMechanics;
	// Prove absence of the extra guaranteed mutation rather than refusing
	// every caster who owns these perks. Existing root effect/Side proofs stay.
	for(const auto * native : mechanics.getAffectedStacks(target))
	{
		if(!native) return Reason::InvalidTarget;
		const auto * unit = source.battleGetUnitByID(native->unitId());
		if(!unit) return Reason::InvalidTarget;
		if(has(Perk::DisruptiveCasting) && spell->isDamage()
			&& (policy->policy()->optionalRemoval || (unit->defended()
				&& !unit->acquireState()->armorerLastStandDefending))) return Reason::UnsupportedCompletion;
		if(has(Perk::PurgingFormula) && spell->isPositive()
			&& source.battleGetOwner(unit) == mechanics.getCasterColor()
			&& (!newHorizonsFrozen::removalPlan(*unit).empty() || !newHorizonsStun::removalPlan(*unit).empty()
				|| !newHorizonsAttackControllers::crippleRemoval(*unit).empty()
				|| !spells::curePhysical::graveDustRemoval(*unit).empty())) return Reason::UnsupportedCompletion;
	}
	if(has(Perk::Matterbreaker) && spell->isDamage() && !mechanics.isMassive()
		&& newHorizonsMetamagic::conventionalArea(*spell, mechanics.getRangeLevel())
		&& !source.battleGetAllObstacles().empty()) return Reason::UnsupportedCompletion;
	// Zero eligible fuel means no stochastic completion or auxiliary mutation.
	// Do not reject all Metamagic merely because the caster has the Skill.
	if(policy->removalChance(nativeRules::metamagicSkillRank(hero), hero->getHeroTypeID()) == 0)
		return Reason::None;
	const auto energy = nativeRules::captureMetamagicEnergy(*source.getBattle());
	if(!energy) return Reason::UnsupportedCompletion;
	const auto resolver = [&](SpellID id) -> std::optional<newHorizonsMetamagic::EnergyDefinition>
	{
		const auto * definition = energy->fuelDefinition(id);
		return definition ? std::optional(*definition) : std::nullopt;
	};
	for(const auto * native : mechanics.getAffectedStacks(target))
	{
		if(!native) return Reason::InvalidTarget;
		const auto * unit = source.battleGetUnitByID(native->unitId());
		if(!unit) return Reason::InvalidTarget;
		const auto owner = source.battleGetOwner(unit);
		const bool friendly = owner == mechanics.getCasterColor();
		const bool restoration = friendly && spell->isPositive()
			&& (!policy->policy()->optionalRemoval || has(Perk::ReconstitutingMagic));
		const bool offense = !friendly && (spell->isDamage() || (spell->isNegative() && has(Perk::HexWeaving)))
			&& (!policy->policy()->optionalRemoval || has(Perk::UnravelingMagic));
		if(!restoration && !offense) continue;
		battle::BattleEffectSnapshot snapshot;
		snapshot.effects = source.getBattle()->getLocalSpellEffects(unit->unitId());
		const auto kind = restoration ? newHorizonsMetamagic::ResolutionKind::RESTORATION
			: newHorizonsMetamagic::ResolutionKind::OFFENSE;
		if(!newHorizonsMetamagic::collectRemovalCandidates(unit->unitId(), owner, snapshot, kind, resolver).empty())
		{
			required |= eventBit(Event::Reaction);
			return Reason::StochasticCompletion;
		}
	}
	return Reason::None;
}
Decision classifySpell(const CBattleInfoCallback & source, BattleSide side,
	const PossibleSpellcast & candidate, EventMask required)
{
	if(!candidate.spell || typeid(*candidate.spell) != typeid(CSpell)) return refusal(Reason::CustomMechanics, required);
	if(candidate.divineMandateFollowup) return refusal(Reason::AutomaticCompletion, required);
	if(arcaneInspiration::profile(source.battleGetFightingHero(side)).corePercent != 0)
		return refusal(Reason::UnsupportedCompletion, required);
	auto target = candidate.dest;
	for(auto & destination : target)
		if(destination.unitValue)
		{
			destination.unitValue = source.battleGetUnitByID(destination.unitValue->unitId());
			if(!destination.unitValue) return refusal(Reason::InvalidTarget, required);
		}
	const auto * hero = source.battleGetFightingHero(side);
	spells::BattleCast cast(&source, hero, spells::Mode::HERO, candidate.spell);
	cast.setOvercharge(candidate.spellOvercharge);
	cast.setSelectiveDispel(candidate.spellSelectiveDispel);
	cast.setCureAffliction(candidate.spellCureAffliction);
	cast.setCurePhysicalAffliction(candidate.spellCurePhysicalAffliction);
	cast.setPurifyChoices(candidate.spellPurifyChoices);
	cast.setMassSlow(candidate.spellMassSlow);
	cast.setShadowGiftSacrificePercent(candidate.spellShadowGiftSacrificePercent);
	auto mechanics = candidate.spell->battleMechanics(&cast);
	if(!mechanics || typeid(*mechanics) != typeid(spells::BattleSpellMechanics))
		return refusal(Reason::CustomMechanics, required);
	if(mechanics->isNewHorizonsStormOfDaggers()
		&& !mechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(target.size())))
		return refusal(Reason::InvalidTarget, required);
	// No castEval, apply, prepared action, payment or RNG occurs here.
	const auto effectReason = inspectSupportedEffects(*mechanics, target, required);
	if(effectReason != Reason::None) return refusal(effectReason, required);
	// The native finite certificate covers two deterministic HP/death endpoints.
	// Opaque death/aura/shared-reroll contexts route CURRENT reference before any
	// action cleanup, payment or original effect; this is not native illegality.
	const auto overflow = newHorizonsMetamagic::overflowSupport(*mechanics, target);
	if(overflow == newHorizonsMetamagic::OverflowSupport::Unsupported)
		return refusal(Reason::UnsupportedCompletion, required);
	if(overflow == newHorizonsMetamagic::OverflowSupport::Supported)
	{
		const auto plan = newHorizonsMetamagic::previewOverflow(*mechanics, target);
		if(plan && plan->conditionalRebirth) return refusal(Reason::UnsupportedCompletion, required);
		if(plan && !plan->additionalRecipients.empty())
			return refusal(Reason::UnsupportedCompletion, required); // Existing compact receipt certifies two endpoints.
		required |= eventBit(Event::Damage) | eventBit(Event::Effect);
	}
	const auto completionReason = metamagicClosure(source, *mechanics, target, required);
	if(completionReason != Reason::None) return refusal(completionReason, required);
	return {Route::Compact, Reason::None, required};
}
}
Reason inspectSupportedEffects(const spells::Mechanics & mechanics, const spells::Target & target, EventMask & required)
{
	return inspectSupportedEffectsImpl(mechanics, target, required);
}
bool supportedImplementation(const spells::effects::Effect & effect)
{
	return typeid(effect) == typeid(DamageEffect) || typeid(effect) == typeid(TimedEffect)
		|| nativeSpells::isFocusEnchantmentEffect(effect);
}
Decision classify(const CBattleInfoCallback & source, BattleSide side, const PossibleSpellcast & candidate)
{
	const auto * branch = dynamic_cast<const HypotheticBattle *>(&source);
	if(!branch || !branch->forecastState()) return refusal(Reason::SourceNotAdmitted);
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) return refusal(Reason::InvisibleHero);
	const auto & owned = branch->forecastSide(side);
	if(!owned || !owned->hasVisibleHero() || !source.battleGetFightingHero(side)) return refusal(Reason::InvisibleHero);
	const auto * hero = source.battleGetFightingHero(side);
	if(!owned->ledger || !owned->mana || !owned->magicOwner || !owned->perkOwner
		|| owned->round != source.battleGetRound()
		|| owned->magicOwner != source.getBattle()->getCapturedMagicRules()
		|| owned->perkOwner != hero->getPerkState().capturedOwner())
		return refusal(Reason::SideState);
	EventMask required = eventBit(Event::HeroResource) | eventBit(Event::Effect);
	Decision result;
	try
	{
		if(candidate.command != HeroCommand::NONE)
		{
			if(!ordinaryOrder(candidate.command) || !heroCommands::isCanonicalRules(source.getBattle()->getHeroCommandRules()))
				return refusal(Reason::UnsupportedOrder, required);
			const auto passive = newHorizonsPassiveDivineMandate::capturePolicy(source.battleGetFightingHero(side));
			if(passive.rank) return refusal(Reason::AutomaticCompletion, required);
			if(consecratedArms::capturePolicy(hero).rank) return refusal(Reason::UnsupportedCompletion, required);
			result = {Route::Compact, Reason::None, required};
		}
		else result = classifySpell(source, side, candidate, required);
	}
	catch(const std::invalid_argument &) { return refusal(Reason::UnsupportedCompletion, required); }
	if(result.route == Route::Compact && !eventsSupported(*branch->forecastState(), result.required))
		return refusal(Reason::MissingEventRecipe, result.required);
	return result;
}
Decision classifyCreature(const CBattleInfoCallback & source, const spells::Mechanics & mechanics,
	const spells::Target & target)
{
	const auto * branch = dynamic_cast<const HypotheticBattle *>(&source);
	if(!branch || !branch->forecastState()) return refusal(Reason::SourceNotAdmitted);
	if(mechanics.battle() != &source || typeid(mechanics) != typeid(spells::BattleSpellMechanics))
		return refusal(Reason::CustomMechanics);
	if(!static_cast<const spells::BattleSpellMechanics &>(mechanics).isFiniteCreatureForecast())
		return refusal(Reason::WrongCastProvenance);
	const auto * caster = mechanics.getUnitCaster();
	if(source.battleGetUnitByID(caster->unitId()) != caster) return refusal(Reason::WrongCastProvenance);
	EventMask required = 0;
	const auto reason = inspectSupportedEffects(mechanics, target, required);
	if(reason != Reason::None) return refusal(reason, required);
	if(!eventsSupported(*branch->forecastState(), required)) return refusal(Reason::MissingEventRecipe, required);
	return {Route::Compact, Reason::None, required};
}
const char * reasonText(Reason reason)
{
	switch(reason)
	{
	case Reason::None: return "admitted finite native closure";
	case Reason::SourceNotAdmitted: return "current source has no compact admission";
	case Reason::InvisibleHero: return "hero knowledge unavailable";
	case Reason::SideState: return "missing owned side completion ledger";
	case Reason::UnsupportedOrder: return "Order has no finite issuance/continuation recipe";
	case Reason::AutomaticCompletion: return "automatic spell completion requires current-source reference";
	case Reason::CustomMechanics: return "custom spell mechanics";
	case Reason::UnsupportedEffect: return "loaded effect implementation has no native recipe";
	case Reason::UnprovenTimedPayload: return "prepared timed payload has no owned lifecycle recipe";
	case Reason::StochasticCompletion: return "eligible Metamagic fuel requires stochastic completion evaluation";
	case Reason::UnsupportedCompletion: return "completion mutation closure has no exact recipe";
	case Reason::MissingEventRecipe: return "current source lacks required unit event recipe";
	case Reason::InvalidTarget: return "target absent from current source";
	case Reason::UnsafeTargeting: return "loaded targeting descriptor or effective chain has no finite recipe";
	case Reason::WrongCastProvenance: return "effects-only entry requires native active creature provenance";
	}
	return "unknown admission reason";
}
}
