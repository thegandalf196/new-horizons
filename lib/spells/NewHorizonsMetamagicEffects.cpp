/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../battle/NewHorizonsPassiveSpecialties.h"
#include "../battle/NewHorizonsCommandResources.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsMetamagicTimedStatus.h"
#include "ISpellMechanics.h"
#include "BattleSpellMechanics.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsWisdom.h"
#include "NewHorizonsSorcery.h"
#include "NewHorizonsSorceryRevision.h"
#include "NewHorizonsTimedMagnitudes.h"
#include "NewHorizonsTimedUtility.h"
#include "NewHorizonsRestorationPools.h"
#include "../battle/NewHorizonsSoulChain.h"
#include "../battle/NewHorizonsFactionSpellDamage.h"
#include "../battle/NewHorizonsElementalAttunement.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/CUnitState.h"
#include "../battle/Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/SetStackEffect.h"
#include "../bonuses/BonusParameters.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <vcmi/ServerCallback.h>
#include <algorithm>
#include <limits>
#include <set>

namespace newHorizonsMetamagic
{
namespace
{
bool numericalBonus(const Bonus & bonus, SpellID acceptedSpell, SpellID effectSpell)
{
	const auto * accepted = acceptedSpell.toSpell();
	const auto * family = effectSpell.toSpell();
	const bool frailty = (accepted && accepted->getJsonKey() == newHorizonsMagic::SHADOW_FRAILTY_SPELL)
		|| (family && family->getJsonKey() == newHorizonsMagic::SHADOW_FRAILTY_SPELL);
	if(frailty && bonus.type == BonusType::PRIMARY_SKILL)
		return false; // Prospective cast fraction was scaled before the native cumulative cap.
	if(isChaosShieldMagnitude(bonus, acceptedSpell, effectSpell)
		|| isMisfortuneChanceFactor(bonus, acceptedSpell, effectSpell)
		|| lightReductionLimit(bonus, acceptedSpell, effectSpell))
		return true;
	// Capability levels and absolute HP capacity are not effect magnitudes.
	if(bonus.valType == BonusValueType::INDEPENDENT_MIN || bonus.valType == BonusValueType::INDEPENDENT_MAX)
		return false;
	switch(bonus.type)
	{
	case BonusType::PRIMARY_SKILL:
	case BonusType::STACKS_SPEED:
	case BonusType::STACKS_MOVEMENT_RANGE:
	case BonusType::STACKS_INITIATIVE:
	case BonusType::STACKS_INITIATIVE_FLAT:
	case BonusType::CREATURE_DAMAGE:
	case BonusType::STACK_HEALTH:
	case BonusType::HP_REGENERATION:
	case BonusType::HEAVENLY_GALE:
	case BonusType::DIVINE_RETRIBUTION:
	case BonusType::LUCK:
	case BonusType::MORALE:
	case BonusType::GENERAL_ATTACK_REDUCTION:
	case BonusType::MAGIC_MIRROR:
	case BonusType::GENERAL_DAMAGE_REDUCTION:
	case BonusType::SPELL_DAMAGE_REDUCTION:
	case BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS:
	case BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS:
	case BonusType::MORE_DAMAGE_FROM_SPELL:
		return true;
	case BonusType::MAGIC_RESISTANCE:
		return (!accepted || accepted->getJsonKey() != newHorizonsSorcery::SPELL_LOCK_SPELL)
			&& (!family || family->getJsonKey() != newHorizonsSorcery::SPELL_LOCK_SPELL);
	case BonusType::COMBAT_EVENT_TRIGGER:
		// Native reflection percentage, not a capability level.
		return acceptedSpell == SpellID::FIRE_SHIELD || effectSpell == SpellID::FIRE_SHIELD;
	default:
		return false;
	}
}

int32_t fortification(const CBattleInfoCallback & callback, const battle::Unit & recipient, PlayerColor spellOwner)
{
	const auto * battle = callback.getBattle();
	if(!battle)
		return 0;
	const auto owner = callback.battleGetOwner(&recipient);
	if(owner == PlayerColor::CANNOT_DETERMINE || spellOwner == PlayerColor::CANNOT_DETERMINE || owner == spellOwner)
		return 0;
	const auto side = callback.playerToSide(owner);
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return 0;
	const auto * hero = battle->getSideHero(side);
	if(!hero || !usesArcaneMarks(hero->getPerkState().rules)
		|| hero->getPerkSkillRank("new-horizons:metamagic") == 0
		|| !hero->hasActivePerk("new-horizons:metamagic", "new-horizons:metamagic.arcaneFortification"))
		return 0;
	return recipient.metamagicMarks(side) >= QUALIFYING_MARKS ? 20 : 0;
}
}

bool isNumericalSpellBonus(const Bonus & bonus, SpellID acceptedSpell, SpellID effectSpell)
{
	return numericalBonus(bonus, acceptedSpell, effectSpell);
}

CastProfile captureHeroCastProfile(const CGHeroInstance * hero, const IBattleInfo * battle, bool genuineHeroCast, SpellID spell)
{
	CastProfile result;
	if(!hero || !battle || !genuineHeroCast)
		return result;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(battle->getSideHero(side) == hero)
		{
			result.hero = hero;
			result.factionSpellDamageBonus = newHorizonsElementalAttunement::spellDamageBonusPercent(
				battle->getElementalAttunementProfile(side), battle->getMagicRules(), spell);
			result.perks = profile(hero);
			result.perks.wisdomEffectivenessBonus = newHorizonsWisdom::effectivenessBonusPercent(hero, *battle, side, spell);
			if(result.perks.pointPolicy)
			{
				const auto & state = battle->getMetamagicPointState(side);
				result.points = state.points;
				result.singularityReached = state.singularityReached;
			}
			result.chaosShieldLimitBasisPoints = hero->hasActivePerk("new-horizons:chaosMagic",
				"new-horizons:chaosMagic.paradoxShield") ? 9000 : 8000;
			result.heroSide = side;
			break;
		}
	return result;
}

int32_t targetFreeEffectivenessBonusPercent(const CastProfile * cast)
{
	if(!cast) return 0;
	return cast->perks.pointPolicy ? effectivenessBonusPercent(cast->perks, cast->points)
		: cast->perks.wisdomEffectivenessBonus;
}


int32_t recipientBonusPercent(const CastProfile * cast, const battle::Unit * recipient)
{
	return cast && recipient ? effectivenessBonusPercent(cast->perks, castCounter(*cast, recipient->metamagicMarks(cast->heroSide))) : 0;
}

int32_t outgoingDuration(int32_t ordinary, const CastProfile * cast, const battle::Unit * recipient)
{
	if(!cast) return ordinary;
	if(recipient) return newHorizonsMetamagic::scaleDuration(ordinary, cast->perks,
		castCounter(*cast, recipient->metamagicMarks(cast->heroSide)));
	auto universal = cast->perks.pointPolicy ? cast->perks : newHorizonsMetamagic::Profile{};
	universal.wisdomEffectivenessBonus = cast->perks.wisdomEffectivenessBonus;
	return newHorizonsMetamagic::scaleDuration(ordinary, universal, cast->perks.pointPolicy ? cast->points : 0);
}

int64_t outgoingMagnitude(int64_t ordinary, const CastProfile * cast, const battle::Unit * recipient)
{
	if(!cast) return ordinary;
	if(recipient) return scaleMagnitude(ordinary, cast->perks, castCounter(*cast, recipient->metamagicMarks(cast->heroSide)));
	Profile universal;
	universal.wisdomEffectivenessBonus = targetFreeEffectivenessBonusPercent(cast);
	return scaleMagnitude(ordinary, universal, 0);
}

int64_t spellDamage(int64_t ordinary, const CastProfile * cast, const CBattleInfoCallback & callback,
	const battle::Unit * recipient, PlayerColor spellOwner, bool magicalDamage)
{
	if(!recipient)
		return ordinary;
	if(magicalDamage && newHorizonsPassiveSpecialties::magicalDamageImmune(*recipient))
		return 0;
	const int32_t factionBonus = newHorizonsFactionSpellDamage::bonusPercent(callback, cast ? cast->hero : nullptr, recipient)
		+ (cast ? cast->factionSpellDamageBonus : 0);
	const int32_t offensiveBonus = recipientBonusPercent(cast, recipient) + factionBonus;
	const int32_t defensiveReduction = fortification(callback, *recipient, spellOwner);
	if(offensiveBonus == 0 && defensiveReduction == 0)
		return newHorizonsCommand::mitigateIncomingDamage(callback, recipient, ordinary);
	const int capPercent = offensiveBonus > 0 ? recipient->valOfBonuses(BonusType::DAMAGE_RECEIVED_CAP) : 0;
	const int64_t cap = capPercent > 0 ? static_cast<int64_t>(recipient->getMaxHealth()) * capPercent / 100 : 0;
	const auto adjusted = spellDamageOutput(ordinary, cast ? cast->perks : Profile{},
		cast ? castCounter(*cast, recipient->metamagicMarks(cast->heroSide)) : 0, defensiveReduction, cap, factionBonus);
	return newHorizonsCommand::mitigateIncomingDamage(callback, recipient, adjusted);
}

int32_t resistanceChance(int32_t ordinary, const CastProfile * cast,
	const CBattleInfoCallback & callback, const battle::Unit & recipient)
{
	if(!cast || cast->perks.pointPolicy || callback.battleGetOwner(&recipient) == callback.sideToPlayer(cast->heroSide))
		return ordinary;
	const int32_t reduction = resistanceReductionPoints(cast->perks, recipient.metamagicMarks(cast->heroSide));
	return reduction == 0 ? ordinary : std::clamp(std::min(ordinary, 100) - reduction, 0, 100);
}

void adjustAppliedBonuses(const CastProfile * cast, const CBattleInfoCallback & callback,
	SpellID acceptedSpell, SpellID effectSpell, const std::set<uint32_t> & preexistingRecipientIds,
	SetStackEffect & packet)
{
	if(!cast)
		return;
	const auto adjust = [&](auto & changes)
	{
		for(auto & [unitId, bonuses] : changes)
		{
			if(!preexistingRecipientIds.contains(unitId))
				continue; // Fresh-entry seeds cannot retroactively amplify the creating spell.
			const auto * unit = callback.battleGetUnitByID(unitId);
			if(!unit)
				continue;
			const int32_t marks = castCounter(*cast, unit->metamagicMarks(cast->heroSide));
			for(auto & bonus : bonuses)
			{
				if(bonus.source != BonusSource::SPELL_EFFECT
					|| (bonus.sid != BonusSourceID(acceptedSpell) && bonus.sid != BonusSourceID(effectSpell)))
					continue;
				if(isChaosShieldMagnitude(bonus, acceptedSpell, effectSpell))
					bonus.val = chaosShieldMagnitude(bonus.val, cast->perks, marks, cast->chaosShieldLimitBasisPoints);
				else if(isMisfortuneChanceFactor(bonus, acceptedSpell, effectSpell))
					bonus.val = misfortuneChanceFactor(bonus.val, cast->perks, marks);
				else if(const auto limit = lightReductionLimit(bonus, acceptedSpell, effectSpell))
					bonus.val = std::min(*limit, scaleSignedBonus(bonus.val, cast->perks, marks));
				else if(numericalBonus(bonus, acceptedSpell, effectSpell))
					bonus.val = scaleSignedBonus(bonus.val, cast->perks, marks);
				if((bonus.duration & BonusDuration::N_TURNS) != 0 && bonus.turnsRemain > 0)
					bonus.turnsRemain = static_cast<int16_t>(std::min<int32_t>(std::numeric_limits<int16_t>::max(),
						scaleDuration(bonus.turnsRemain, cast->perks, marks)));
			}
		}
	};
	adjust(packet.toAdd);
	adjust(packet.toUpdate);
}

int minimumSelectionCost(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * battle, int ordinaryCost)
{
	if(!battle || !battle->getBattle() || !spell.isCombat())
		return ordinaryCost;
	const auto facts = newHorizonsMetamagic::profile(&hero);
	if(!facts.pointPolicy && (!facts.manaAttunement || !newHorizonsMetamagic::usesArcaneMarks(hero.getPerkState().rules)))
		return ordinaryCost;
	BattleSide side = BattleSide::NONE;
	for(const auto candidate : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(battle->battleGetFightingHero(candidate) == &hero)
			side = candidate;
	if(side == BattleSide::NONE)
		return ordinaryCost;
	if(facts.pointPolicy)
		return singularityManaCost(ordinaryCost, facts, battle->getBattle()->getMetamagicPointState(side).singularityReached);
	spells::BattleCast cast(battle, &hero, spells::Mode::HERO, &spell);
	const auto mechanics = spell.battleMechanics(&cast);
	if(!mechanics)
		return ordinaryCost;
	// Receptivity is a cheap necessary condition. This bound does not search
	// AoE positions, claim a valid target or replace native canBeCastAt.
	for(const auto * unit : battle->getBattle()->getUnitsIf([](const battle::Unit * unit) { return unit != nullptr; }))
		if(unit->metamagicMarks(side) >= newHorizonsMetamagic::QUALIFYING_MARKS && mechanics->isReceptive(unit))
			return newHorizonsMetamagic::adjustedManaCost(ordinaryCost, facts, 1, 1);
	return ordinaryCost;
}

ManaTargetCounts manaTargetCounts(const CastProfile * cast, const CBattleInfoCallback & callback,
	std::span<const uint32_t> prospectiveRecipientIds)
{
	ManaTargetCounts result;
	if(!cast)
		return result;
	std::set<uint32_t> seen;
	for(const auto id : prospectiveRecipientIds)
		if(const auto * recipient = callback.battleGetUnitByID(id); recipient && seen.insert(id).second)
		{
			++result.affected;
			if(castCounter(*cast, recipient->metamagicMarks(cast->heroSide)) >= QUALIFYING_MARKS)
				++result.qualifying;
		}
	return result;
}

int32_t manaCostForRecipients(int32_t ordinary, const CastProfile * cast, const CBattleInfoCallback & callback,
	std::span<const uint32_t> prospectiveRecipientIds)
{
	if(!cast)
		return ordinary;
	if(cast->perks.pointPolicy)
		return singularityManaCost(ordinary, cast->perks, cast->singularityReached);
	if(!cast->perks.manaAttunement)
		return ordinary;
	const auto counts = manaTargetCounts(cast, callback, prospectiveRecipientIds);
	return adjustedManaCost(ordinary, cast->perks, counts.qualifying, counts.affected);
}

void publishResolvedMarks(ServerCallback & server, const CastProfile * cast, const CBattleInfoCallback & callback,
	std::span<const uint32_t> resolvedRecipientIds)
{
	if(!cast || cast->perks.pointPolicy)
		return;
	std::set<uint32_t> seen;
	BattleUnitsChanged packet;
	packet.battleID = callback.getBattle()->getBattleID();
	for(const auto id : resolvedRecipientIds)
	{
		const auto * recipient = callback.battleGetUnitByID(id);
		if(!recipient || recipient->isGhost() || !seen.insert(id).second)
			continue;
		const int32_t previous = castCounter(*cast, recipient->metamagicMarks(cast->heroSide));
		const int32_t next = marksAfterResolvedSpell(cast->perks, previous);
		if(next == previous)
			continue;
		auto state = recipient->acquireState();
		state->arcaneMarks.set(cast->heroSide, next);
		UnitChanges change(id, UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		packet.changedStacks.push_back(std::move(change));
	}
	if(!packet.changedStacks.empty())
		server.apply(packet);
}
}

namespace spells
{
int32_t Mechanics::adjustMetamagicBonus(int32_t ordinary, const battle::Unit * recipient) const
{
	const auto * cast = getMetamagicCastProfile();
	return cast && recipient ? newHorizonsMetamagic::scaleSignedBonus(ordinary, cast->perks,
		castCounter(*cast, recipient->metamagicMarks(cast->heroSide))) : ordinary;
}

int32_t Mechanics::adjustMetamagicDuration(int32_t ordinary, const battle::Unit * recipient) const
{
	return newHorizonsMetamagic::outgoingDuration(ordinary, getMetamagicCastProfile(), recipient);
}

int64_t Mechanics::adjustMetamagicMagnitude(int64_t ordinary, const battle::Unit * recipient) const
{
	return newHorizonsMetamagic::outgoingMagnitude(ordinary, getMetamagicCastProfile(), recipient);
}

int64_t Mechanics::getDivineRetributionCap(const battle::Unit * recipient) const
{
	return newHorizonsMagic::divineRetributionCap(getEffectPower(), getSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent(), getCrownAndAltarBonusPercent(recipient));
}

int64_t Mechanics::getRecipientDivineRetributionCap(const battle::Unit * recipient) const
{
	return adjustMetamagicMagnitude(getDivineRetributionCap(recipient), recipient);
}

std::optional<int32_t> Mechanics::getRecipientDivineRetributionPayoutBasisPoints(const battle::Unit * recipient) const
{
	if(!getMetamagicCastProfile() || !recipient)
		return std::nullopt; // Historical tuples retain absence, not a new default field.
	return static_cast<int32_t>(adjustMetamagicMagnitude(3000, recipient));
}

newHorizonsMagic::FrailtyAccumulation Mechanics::getRecipientFrailtyPlan(const battle::Unit * recipient) const
{
	if(!recipient)
		return {};
	const auto defense = Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE));
	const int32_t baseDefense = std::max(0, recipient->valOfBonuses(
		defense.And(Selector::sourceTypeSel(BonusSource::CREATURE_ABILITY))));
	const auto previous = recipient->getBonuses(defense.And(
		Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(getSpellId()))));
	const int32_t total = previous && !previous->empty() && previous->front()->parameters
		? std::max(0, previous->front()->parameters->toNumber()) : 0;
	return newHorizonsMagic::frailtyAccumulation(baseDefense, total,
		adjustMetamagicMagnitude(getFrailtyDefenseLossBasisPoints(), recipient));
}

int32_t Mechanics::getRecipientSpellLockDuration(const battle::Unit * recipient) const
{
	const auto * hero = getHeroCaster();
	const bool spellbinder = hero && hero->hasActivePerk(
		"new-horizons:sorceryMagic", "new-horizons:sorceryMagic.spellbinder");
	const auto ordinary = adjustEffectDuration(newHorizonsSorcery::spellLockDurationBasisPoints(getEffectPower(),
		spellbinder, getSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent()));
	return adjustMetamagicDuration(ordinary, recipient);
}

int32_t Mechanics::getArcaneBreachMarkBasisPoints() const
{
	using namespace newHorizonsSorcery;
	// Validate the same domain as the shared unmodified formula. Empower
	// and the saved School x Spellcraft coefficient boost only the Spell Power component,
	// before the final per-mark cap.
	const auto effectPower = getEffectPower();
	// Focus Magic captures its own first-shot penetration. Other Arcane Breach
	// mark producers retain the ordinary formula, including post-hit marks.
	const auto * hero = getHeroCaster();
	const int specialty = hero && usesNewHorizonsMagicV3()
		&& getSpellId().toSpell()->getJsonKey() == FOCUS_MAGIC_SPELL
		? hero->getNonDamageSpellSpecialtyBonusPercent(getSpellId()) : 0;
	// Only this enchantment opts into revised potency. Other mark producers
	// and all historical captures continue to use the original formula.
	const auto * state = battle() ? battle()->getBattle() : nullptr;
	const auto revised = state && getSpellId().toSpell()->getJsonKey() == FOCUS_MAGIC_SPELL
		? newHorizonsSorceryRevision::focusRules(state->getMagicRules()) : std::nullopt;
	const auto profile = revised.value_or(newHorizonsSorceryRevision::FocusRules{
		ARCANE_BREACH_BASE_BASIS_POINTS, ARCANE_BREACH_POWER_BASIS_POINTS, ARCANE_BREACH_CAP_BASIS_POINTS});
	return newHorizonsSorceryRevision::focusMarkBasisPoints(profile, effectPower,
		getSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent(), specialty);
}

int32_t Mechanics::getRecipientArcaneBreachMarkBasisPoints(const battle::Unit * recipient) const
{
	const auto ordinary = getArcaneBreachMarkBasisPoints();
	if(!getMetamagicCastProfile())
		return ordinary;
	// The independent Arcane Breach consumer caps each of its three marks at20%.
	return static_cast<int32_t>(std::min<int64_t>(newHorizonsSorcery::ARCANE_BREACH_CAP_BASIS_POINTS,
		adjustMetamagicMagnitude(ordinary, recipient)));
}

int32_t Mechanics::getRecipientSoulChainEchoBasisPoints(const battle::Unit * recipient) const
{
	const auto * hero = getHeroCaster();
	const bool soulBinder = hero && hero->hasActivePerk(
		std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL), "new-horizons:shadowMagic.soulBinder");
	const auto ordinary = newHorizonsSoulChain::echoPercentBasisPoints(std::max<int32_t>(0, getEffectPower()),
		getSpellPowerCoefficientBasisPoints(), soulBinder);
	const auto maximum = newHorizonsSoulChain::BASE_ECHO_CAP_BASIS_POINTS
		+ newHorizonsSoulChain::SOUL_BINDER_BONUS_BASIS_POINTS;
	return static_cast<int32_t>(std::min<int64_t>(maximum, adjustMetamagicMagnitude(ordinary, recipient)));
}

int64_t Mechanics::getRecipientHexOfPainDamageSharePercent(const battle::Unit * recipient) const
{
	const auto ordinary = scaleHeroLuckDamage(newHorizonsMagic::hexOfPainDamageSharePercent());
	if(!getMetamagicCastProfile())
		return ordinary;
	// Existing combat metadata permits integer percentages0..100. Preserve that
	// native cap, then snapshot this component; later attacks never read new marks.
	return std::min<int64_t>(100, adjustMetamagicMagnitude(ordinary, recipient));
}

int64_t Mechanics::getPhantomArmyIntegrity(const battle::Unit * source) const
{
	if(!source || getSpellId().toSpell()->getJsonKey() != "new-horizons:phantomArmy")
		return 0;
	const auto * hero = getHeroCaster();
	const auto ordinary = std::max<int64_t>(1, newHorizonsSorcery::phantomArmyIntegrityWithModifiers(
		source->getAvailableHealth(), std::max(0, getEffectPower()),
		hero && hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.illusionist"),
		getSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent(),
		hero ? hero->getNonDamageSpellSpecialtyBonusPercent(getSpellId()) : 0));
	return adjustMetamagicMagnitude(ordinary, source);
}

int64_t Mechanics::getGuardianSpiritHitPoints(const battle::Unit * target) const
{
	if(getSpellId().toSpell()->getJsonKey() != "new-horizons:guardianSpirit")
		return 0;
	const auto * hero = getHeroCaster();
	const auto ordinary = newHorizonsMagic::guardianSpiritHitPoints(getEffectPower(), getSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent(),
		hero ? hero->getNonDamageSpellSpecialtyBonusPercent(getSpellId()) : 0,
		hero && hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.healer"),
		hero && hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.guardian"),
		getCrownAndAltarBonusPercent(target));
	return adjustMetamagicMagnitude(ordinary, target);
}
int64_t Mechanics::getPlagueTickDamage() const
{
	if(getSpellId().toSpell()->getJsonKey() != "new-horizons:plague")
		return 0;
	const auto * hero = getHeroCaster();
	// Preserve Plague's authored initial snapshot: coefficient, not an extra
	// Empower application. Children reuse this exact captured value.
	return scaleHeroLuckDamage(newHorizonsMagic::plagueTickDamage(getEffectPower(), getDamageSpellPowerCoefficientBasisPoints(),
		hero ? hero->getDamageSpellSpecialtyBonusPercent(getSpellId()) : 0));
}

int32_t Mechanics::getFrailtyDefenseLossBasisPoints() const
{
	if(getSpellId().toSpell()->getJsonKey() != newHorizonsMagic::SHADOW_FRAILTY_SPELL)
		return 0;
	const auto * hero = getHeroCaster();
	return newHorizonsMagic::frailtyDefenseLoss(getEffectPower(), getSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent(),
		hero ? hero->getNonDamageSpellSpecialtyBonusPercent(getSpellId()) : 0,
		hero && hero->hasActivePerk(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL),
			std::string(newHorizonsMagic::SHADOW_WITHERING_TOUCH_PERK)));
}

}
