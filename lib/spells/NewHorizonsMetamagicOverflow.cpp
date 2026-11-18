/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NewHorizonsMetamagicOverflow.h"
#include "NewHorizonsMetamagicCore.h"
#include "SpellDamageContext.h"
#include "../CStack.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <vstd/RNG.h>
#include "NewHorizonsSorcerySpells.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsImplosion.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "BattleSpellMechanics.h"
#include "effects/native/Damage.h"
#include "effects/native/Timed.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/CUnitState.h"
#include "../battle/IBattleState.h"
#include "../battle/NewHorizonsCommandResources.h"
#include "../battle/NewHorizonsHeroLuck.h"
#include "../battle/NewHorizonsPassiveSpecialties.h"
#include "../combatScripts/nativeEvents/NativeEventCommon.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../nativeRules/CompiledMetamagicRules.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../bonuses/Propagators.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <typeinfo>

namespace newHorizonsMetamagic
{
namespace
{
bool nativeTownTavernInformation(const Bonus & row)
{
	// This incoming adventure statistic is provided by a persistent town, not
	// by the dying battle unit. Recognize only the exact native tavern record.
	const auto building = row.sid.as<BuildingTypeUniqueID>();
	return typeid(row) == typeid(Bonus) && row.type == BonusType::THIEVES_GUILD_ACCESS
		&& row.source == BonusSource::TOWN_STRUCTURE && building.hasValue()
		&& building.getBuilding() == BuildingID::TAVERN && row.val == 1
		&& row.valType == BonusValueType::ADDITIVE_VALUE && row.duration == BonusDuration::PERMANENT
		&& row.turnsRemain == 0 && row.effectRange == BonusLimitEffect::NO_LIMIT
		&& row.subtype == BonusSubtypeID() && row.targetSourceType == BonusSource::OTHER
		&& row.stacking.empty() && !row.parameters && !row.limiter && !row.updater && !row.propagationUpdater
		&& row.propagator && typeid(*row.propagator) == typeid(CPropagatorNodeType)
		&& static_cast<const CPropagatorNodeType &>(*row.propagator).getPropagatorType() == BonusNodeType::PLAYER;
}
bool unrepresentedDeathContext(const battle::Unit & unit)
{
	if(unit.hasBonusOfType(BonusType::COMBAT_EVENT_TRIGGER)) return true;
	// A dying provider can change a peer's protection/target topology. Without
	// that provider's post-death recipe a current-board number is not a bound.
	const auto records = unit.getUnstackedBonuses(Selector::all);
	return std::ranges::any_of(*records, [](const auto & record)
	{ return !record || ((record->propagator || record->propagationUpdater) && !nativeTownTavernInformation(*record)); });
}
const CSpell * ordinarySpell(const spells::Mechanics & mechanics)
{
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	const auto * hero = mechanics.getHeroCaster();
	const auto * callback = mechanics.battle();
	if(!spell || !hero || mechanics.caster != hero || !callback || !callback->getBattle()
		|| !mechanics.isGenuineHeroCast() || mechanics.getAutomaticHeroCast().automatic()
		|| mechanics.isRunicReflectionShare() || !spell->isCommonHeroSpell()
		|| !spell->isDamage() || !spell->isMagical() || mechanics.isMassive()
		|| callback->battleGetFightingHero(mechanics.getCasterSide()) != hero
		|| !singleTargetSpell(mechanics) || singleRecipientEffectPayload(mechanics) != true
		|| mechanics.getEffectiveChainLength(1) > 1) return nullptr;
	return spell;
}
const battle::Unit * primaryTarget(const spells::Mechanics & mechanics, const spells::Target & aim)
{
	const auto * callback = mechanics.battle();
	if(!callback || aim.size() != 1) return nullptr;
	if(aim.front().unitValue)
		return callback->battleGetUnitByID(aim.front().unitValue->unitId());
	// Ordinary GUI creature targeting sends a hex; explicit selectors and AI
	// may send a unit. Resolve both through the same current native battlefield.
	return aim.front().hexValue.isValid() ? callback->battleGetUnitByPos(aim.front().hexValue, true) : nullptr;
}

int percent(const spells::Mechanics & mechanics)
{
	const auto * hero = mechanics.getHeroCaster();
	const auto * policy = nativeRules::metamagicRules(hero);
	return policy ? policy->overflowPercent(nativeRules::metamagicSkillRank(hero)) : 0;
}
bool forceLance(const spells::Mechanics & mechanics)
{
	bool found = false;
	mechanics.forEachEffect([&](const spells::effects::Effect & effect)
	{
		found = found || nativeSpells::isForceLanceEffect(effect);
		return found;
	});
	return found;
}
std::vector<OverflowCandidate> candidates(const CBattleInfoCallback & callback, PlayerColor owner)
{
	std::vector<OverflowCandidate> result;
	for(const auto * unit : callback.battleGetAllUnits(false))
		if(unit) result.push_back({unit->unitId(), unit->getHexes(), unit->alive() && !unit->isGhost()
			&& unit->isValidTarget(false) && callback.battleGetOwner(unit) != PlayerColor::CANNOT_DETERMINE
			&& callback.battleGetOwner(unit) != owner});
	return result;
}
spells::RecipientConditionResult condition(const CBattleInfoCallback &,
	const spells::Mechanics & mechanics, const battle::Unit & target)
{
	const auto * native = dynamic_cast<const spells::BattleSpellMechanics *>(&mechanics);
	return native ? native->checkOverflowRecipient(&target) : spells::RecipientConditionResult::UNSUPPORTED_CONDITION;
}
bool uncertainResistanceReroll(const CBattleInfoCallback & callback, const battle::Unit & target)
{
	const auto resistance = callback.battleGetMagicResistance(&target);
	const auto side = callback.playerToSide(callback.battleGetOwner(&target));
	return resistance > 0 && resistance < 100
		&& (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& callback.getBattle()->getAdverseCombatRerollState(side).available();
}
std::optional<int64_t> defendedDamage(const CBattleInfoCallback & callback, const spells::Mechanics & mechanics,
	const battle::Unit & target, int64_t raw, bool nativeConditionFallback)
{
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	if(!spell) throw std::invalid_argument("Overflow damage requires its original spell identity");
	if(raw <= 0 || target.isInvincible() || callback.battleGetMagicResistance(&target) >= 100
		|| newHorizonsPassiveSpecialties::magicalDamageImmune(target)) return 0;
	const auto verdict = condition(callback, mechanics, target);
	if(verdict == spells::RecipientConditionResult::ILLEGAL) return 0;
	if(verdict != spells::RecipientConditionResult::LEGAL)
	{
		if(!nativeConditionFallback) return {};
		const auto * native = dynamic_cast<const spells::BattleSpellMechanics *>(&mechanics);
		if(!native) throw std::logic_error("Overflow has no native ordinary condition owner");
		// The live owner's ordinary condition API is authoritative. This bridge
		// does not call the original spell's special Force Lance admission bypass.
		if(!native->isOverflowRecipientReceptive(&target)) return 0;
	}
	const auto damage = spell->adjustRawDamage(nullptr, &target, raw, 0,
		callback.battleGetHoldTheLineMagicalReductionBasisPoints(&target), 100,
		mechanics.usesNewHorizonsMultiplicativeMDR(), mechanics.usesNewHorizonsMagicV3(), false,
		callback.battleGetPerkMagicalReductionBasisPoints(&target));
	return newHorizonsCommand::mitigateIncomingDamage(callback, &target, damage);
}
bool readyRebirth(const battle::Unit * unit)
{
	if(!unit || unit->getPhantomInitialIntegrity() > 0) return false;
	const auto value = unit->acquireState();
	return value->valOfBonuses(BonusType::REBIRTH) > 0 && value->canCast();
}
std::optional<OverflowPreview> plan(const CBattleInfoCallback & callback, const spells::Mechanics & mechanics,
	const OverflowState & state, bool nativeConditionFallback, std::optional<uint32_t> excluded = std::optional<uint32_t>{})
{
	if(!state.lethal || !state.primary || state.invalid || state.percent <= 0) return {};
	const auto raw = overflowDamage(state.lethal->postShieldBeforeCasualties, state.lethal->healthBefore, state.percent);
	if(raw <= 0) return {};
	const auto owner = mechanics.getCasterColor();
	auto pool = candidates(callback, owner);
	if(excluded) std::erase_if(pool, [&](const OverflowCandidate & candidate) { return candidate.id == *excluded; });
	const auto selected = nearestOverflowEnemy(*state.primary, state.footprint, pool);
	if(!selected) return {};
	const auto * target = callback.battleGetUnitByID(*selected);
	if(!target) return {};
	const auto defended = defendedDamage(callback, mechanics, *target, raw, nativeConditionFallback);
	if(!defended) return {};
	auto copy = target->acquireState();
	auto applied = *defended;
	std::optional<battle::VowCasualtyQuota> quota;
	const auto damageContext = spells::previewSpellDamageContext(mechanics, *target, quota);
	copy->damageWithContext(applied, false, battle::DamageProvenance::SPELL, damageContext);
	const auto * primary = callback.battleGetUnitByID(*state.primary);
	const bool rebirth = readyRebirth(primary) || readyRebirth(target);
	return OverflowPreview{*state.primary, *selected, owner, callback.battleGetOwner(target), state.lethal->healthBefore,
		state.lethal->postShieldBeforeCasualties, state.lethal->appliedDamage, raw, *defended, applied,
		state.primaryResistancePercent, std::clamp(callback.battleGetMagicResistance(target), 0, 100), rebirth};
}
}
int64_t overflowDamage(int64_t postShield, int64_t healthBefore, int percent)
{
	if(postShield <= 0 || healthBefore <= 0 || postShield <= healthBefore || percent <= 0 || percent > 100) return 0;
	const auto excess = postShield - healthBefore;
	return excess / 100 * percent + excess % 100 * percent / 100;
}
std::optional<uint32_t> nearestOverflowEnemy(uint32_t primary, const BattleHexArray & footprint,
	std::span<const OverflowCandidate> pool)
{
	std::optional<std::pair<int, uint32_t>> best;
	for(const auto & candidate : pool)
	{
		if(candidate.id == primary || !candidate.livingHostile || candidate.footprint.empty()) continue;
		int distance = std::numeric_limits<int>::max();
		for(const auto origin : footprint)
			for(const auto target : candidate.footprint)
				distance = std::min(distance, static_cast<int>(BattleHex::getDistance(origin, target)));
		if(distance == std::numeric_limits<int>::max()) continue;
		const auto key = std::pair{distance, candidate.id};
		if(!best || key < *best) best = key;
	}
	return best ? std::optional(best->second) : std::nullopt;
}
void OverflowState::observe(const OverflowPrimaryDamage & damage)
{
	if(consumed || !primary || percent <= 0) return;
	if(damage.primary != *primary) { invalid = true; return; }
	if(damage.defeated && damage.appliedDamage > 0 && damage.healthBefore > 0
		&& damage.postShieldBeforeCasualties > damage.healthBefore)
	{
		if(lethal) invalid = true; // An original cast cannot pay a second spill.
		else lethal = damage;
	}
}
OverflowSupport overflowSupport(const spells::Mechanics & mechanics, const spells::Target & target, bool postKillProjection)
{
	// Do not invoke unknown mechanics/spell callbacks in a read-only preflight.
	if(typeid(mechanics) != typeid(spells::BattleSpellMechanics)) return OverflowSupport::Unsupported;
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	if(!spell || typeid(*spell) != typeid(CSpell)) return OverflowSupport::Unsupported;
	if(percent(mechanics) <= 0) return OverflowSupport::Inapplicable;
	// A known non-damaging spell cannot produce damage overflow. Its Timed or
	// enchantment payload is owned by the separate effect-admission path.
	if(!spell->isDamage()) return OverflowSupport::Inapplicable;
	const auto shape = singleRecipientEffectPayload(mechanics);
	if(!shape) return OverflowSupport::Unsupported;
	if(!ordinarySpell(mechanics)) return OverflowSupport::Inapplicable;
	const auto * primary = primaryTarget(mechanics, target);
	if(!primary) return OverflowSupport::Inapplicable;
	unsigned damageCount = 0;
	bool supported = true;
	mechanics.forEachEffect([&](const spells::effects::Effect & effect)
	{
		const bool damage = typeid(effect) == typeid(spells::effects::native::Damage);
		const bool timed = typeid(effect) == typeid(spells::effects::native::Timed);
		const bool lance = nativeSpells::isForceLanceEffect(effect);
		if(!damage && !timed && !lance) { supported = false; return true; }
		const auto & unitEffect = static_cast<const spells::effects::native::UnitEffect &>(effect);
		if(!unitEffect.supportsFiniteForecastTargeting(mechanics)) { supported = false; return true; }
		if(damage)
		{
			++damageCount;
			supported = !effect.indirect && !effect.optional
				&& static_cast<const spells::effects::native::Damage &>(effect).supportsFiniteForecast();
		}
		else if(lance) ++damageCount;
		else supported = static_cast<const spells::effects::native::Timed &>(effect).supportsFiniteForecastPayload();
		return !supported;
	});
	if(!supported || damageCount != 1) return OverflowSupport::Unsupported;
	if(mechanics.getTargetTypes() != std::vector<spells::AimType>{spells::AimType::CREATURE}) return OverflowSupport::Unsupported;
	const auto * callback = mechanics.battle();
	if(!postKillProjection && newHorizonsImplosion::rulesFor(callback->getBattle()->getMagicRules(), spell->getId()))
		return OverflowSupport::Unsupported;
	if(callback->battleGetOwner(primary) == mechanics.getCasterColor()) return OverflowSupport::Inapplicable;
	if(!forceLance(mechanics) && mechanics.isNegativeSpell() && primary->valOfBonuses(BonusType::MAGIC_MIRROR) > 0)
		return OverflowSupport::Unsupported;
	if(unrepresentedDeathContext(*primary) || readyRebirth(primary) || uncertainResistanceReroll(*callback, *primary)
		|| (!postKillProjection && primary->hasBonusOfType(BonusType::SPELL_RESISTANCE_AURA))) return OverflowSupport::Unsupported;
	auto pool = candidates(*callback, mechanics.getCasterColor());
	const int recipients = nativeRules::hasMetamagicPerk(mechanics.getHeroCaster(), nativeRules::MetamagicPerk::ArcaneCascade) ? 2 : 1;
	for(int index = 0; index < recipients; ++index)
	{
		const auto id = nearestOverflowEnemy(primary->unitId(), primary->getHexes(), pool);
		if(!id) break;
		if(const auto * secondary = callback->battleGetUnitByID(*id);
			secondary && (unrepresentedDeathContext(*secondary) || uncertainResistanceReroll(*callback, *secondary)
				|| (condition(*callback, mechanics, *secondary) != spells::RecipientConditionResult::LEGAL
					&& condition(*callback, mechanics, *secondary) != spells::RecipientConditionResult::ILLEGAL))) return OverflowSupport::Unsupported;
		std::erase_if(pool, [&](const OverflowCandidate & candidate) { return candidate.id == *id; });
	}
	if(!forceLance(mechanics) && condition(*callback, mechanics, *primary) != spells::RecipientConditionResult::LEGAL
		&& condition(*callback, mechanics, *primary) != spells::RecipientConditionResult::ILLEGAL) return OverflowSupport::Unsupported;
	const auto & native = static_cast<const spells::BattleSpellMechanics &>(mechanics);
	if(newHorizonsHeroLuck::hasDruidicLuck(mechanics.getHeroCaster()) && !native.hasResolvedHeroLuck())
		return OverflowSupport::Unsupported;
	return OverflowSupport::Supported;
}
OverflowState prepareOverflow(const spells::Mechanics & mechanics, const spells::Target & aim,
	const spells::effects::Effects::EffectsToApply & effects, bool acceptedOriginal)
{
	OverflowState state;
	if(!acceptedOriginal || percent(mechanics) <= 0 || !ordinarySpell(mechanics)) return state;
	const auto * primary = primaryTarget(mechanics, aim);
	if(!primary || !primary->alive() || mechanics.battle()->battleGetOwner(primary) == mechanics.getCasterColor()) return state;
	state.primary = primary->unitId(); state.footprint = primary->getHexes(); state.percent = percent(mechanics);
	state.cascade = nativeRules::hasMetamagicPerk(mechanics.getHeroCaster(), nativeRules::MetamagicPerk::ArcaneCascade);
	state.primaryResistancePercent = (forceLance(mechanics) || empoweredDamage(mechanics)) ? 0 : std::clamp(mechanics.battle()->battleGetMagicResistance(primary), 0, 100);
	for(const auto & [effect, targets] : effects)
		for(const auto & target : targets)
			if(!target.unitValue || target.unitValue->unitId() != *state.primary) state.invalid = true;
	return state;
}
std::optional<OverflowPreview> previewOverflow(const spells::Mechanics & mechanics, const spells::Target & aim)
{
	if(overflowSupport(mechanics, aim) != OverflowSupport::Supported) return {};
	const auto * native = dynamic_cast<const spells::BattleSpellMechanics *>(&mechanics);
	if(!native || (newHorizonsHeroLuck::hasDruidicLuck(mechanics.getHeroCaster()) && !native->hasResolvedHeroLuck())) return {};
	const auto * primary = primaryTarget(mechanics, aim);
	if(!primary || !primary->alive()) return {};
	const auto * damage = mechanics.findEffect<spells::effects::native::Damage>();
	const bool bypass = forceLance(mechanics);
	if(primary->isInvincible() || (!bypass && ((!empoweredDamage(mechanics) && mechanics.battle()->battleGetMagicResistance(primary) >= 100)
		|| damage->filterTarget(&mechanics, {spells::Destination(primary)}).empty()))) return {};
	auto amount = bypass ? newHorizonsSorcerySpells::forceLanceDamage(mechanics) : damage->damageForTarget(0, &mechanics, primary);
	auto copy = primary->acquireState();
	const auto before = bypass ? primary->getShadowGiftCurrentHealth() : primary->getAvailableHealth();
	int64_t postShield = amount;
	std::optional<battle::VowCasualtyQuota> quota;
	auto damageContext = spells::previewSpellDamageContext(mechanics, *primary, quota);
	damageContext.bypassMagicalAbsorption = bypass;
	copy->damageWithContext(amount, false, battle::DamageProvenance::SPELL, damageContext, &postShield, bypass);
	OverflowState state;
	state.primary = primary->unitId(); state.footprint = primary->getHexes(); state.percent = percent(mechanics);
	state.cascade = nativeRules::hasMetamagicPerk(mechanics.getHeroCaster(), nativeRules::MetamagicPerk::ArcaneCascade);
	state.primaryResistancePercent = (bypass || empoweredDamage(mechanics)) ? 0 : std::clamp(mechanics.battle()->battleGetMagicResistance(primary), 0, 100);
	state.observe({primary->unitId(), before, postShield, amount, !copy->alive()});
	auto preview = plan(*mechanics.battle(), mechanics, state, false);
	if(preview && state.cascade)
		if(auto second = plan(*mechanics.battle(), mechanics, state, false, preview->recipient))
			preview->additionalRecipients.push_back(*second);
	return preview;
}
std::optional<OverflowPreview> planOverflow(const CBattleInfoCallback & callback, const spells::Mechanics & mechanics, OverflowState & state)
{
	if(state.consumed) return {};
	state.consumed = true;
	const auto * primary = state.primary ? callback.battleGetUnitByID(*state.primary) : nullptr;
	if(primary && primary->alive()) return {}; // Includes successful native Rebirth.
	auto preview = plan(callback, mechanics, state, true);
	if(preview && state.cascade)
		if(auto second = plan(callback, mechanics, state, true, preview->recipient))
			preview->additionalRecipients.push_back(*second);
	return preview;
}
std::optional<OverflowPreview> completeOverflow(ServerCallback & server, const CBattleInfoCallback & callback,
	const spells::Mechanics & mechanics, OverflowState & state, OverflowProjectionOutcome outcome)
{
	const auto preview = planOverflow(callback, mechanics, state);
	if(!preview || outcome == OverflowProjectionOutcome::SpillResisted) return preview;
	const auto * native = dynamic_cast<const spells::BattleSpellMechanics *>(&mechanics);
	if(!native) return preview;
	std::vector<const OverflowPreview *> recipients{&*preview};
	for(const auto & extra : preview->additionalRecipients) recipients.push_back(&extra);
	StacksInjured packet;
	packet.battleID = callback.getBattle()->getBattleID();
	// All defenses, resistance and detached injuries precede publication. A dying
	// secondary cannot remove an aura and change the other simultaneous share.
	for(const auto * share : recipients)
	{
		const auto * target = callback.battleGetUnitByID(share->recipient);
		if(!target || share->defendedSecondaryDamage <= 0 || native->wouldResistOverflow(target)) continue;
		BattleStackAttacked hit;
		hit.stackAttacked = share->recipient;
		hit.attackerID = -1;
		hit.damageAmount = share->defendedSecondaryDamage;
		auto copy = target->acquireState();
		const auto damageContext = mechanics.spellDamageContext(*target);
		CStack::prepareAttacked(hit, *server.getRNG(), copy, false, false, battle::DamageProvenance::SPELL, nullptr, &damageContext);
		// Existing injury presentation replays this spell's impact, including
		// custom Force Lance visuals, without publishing another spell cast.
		hit.flags |= BattleStackAttacked::SECONDARY | BattleStackAttacked::SPELL_HIT_EFFECT;
		hit.spellID = mechanics.getSpell()->getId();
		if(damageContext.redemption) damageContext.redemption->record(callback, std::span<const BattleStackAttacked>(&hit, 1));
		packet.stacks.push_back(std::move(hit));
	}
	if(!packet.stacks.empty()) server.apply(packet);
	static const JsonNode parameters;
	static const CombatEventPayload payload;
	const nativeEvents::Context context{server, callback, nullptr, nullptr, parameters, payload};
	for(const auto & hit : packet.stacks)
		if(const auto * current = callback.battleGetUnitByID(hit.stackAttacked)) nativeEvents::clearFrozen(context, *current);
	return preview;
}
std::pair<int64_t, int64_t> applyOverflowDamage(ServerCallback & server, const CBattleInfoCallback & callback, const OverflowPreview & preview)
{
	const auto * target = callback.battleGetUnitByID(preview.recipient);
	if(!target || !target->alive() || target->isGhost() || preview.primary == preview.recipient
		|| callback.battleGetOwner(target) == preview.casterOwner
		|| callback.battleGetOwner(target) != preview.recipientOwner || preview.defendedSecondaryDamage <= 0) return {};
	static const JsonNode parameters;
	static const CombatEventPayload payload;
	const nativeEvents::Context context{server, callback, nullptr, nullptr, parameters, payload};
	const auto result = nativeEvents::damage(context, *target, preview.defendedSecondaryDamage, nullptr, true, nullptr, false, true);
	if(const auto * current = callback.battleGetUnitByID(preview.recipient)) nativeEvents::clearFrozen(context, *current);
	return result;
}
}
