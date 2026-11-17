/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../battle/NewHorizonsPassiveSpecialties.h"
#include "../battle/NewHorizonsCommandResources.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsMetamagicEnchantments.h"
#include "NewHorizonsMetamagicTimedStatus.h"
#include "ISpellMechanics.h"
#include "BattleSpellMechanics.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsWisdom.h"
#include "NewHorizonsSorcery.h"
#include "NewHorizonsSorcerySpells.h"
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
		return true;
	case BonusType::COMBAT_EVENT_TRIGGER:
		// Native reflection percentage, not a capability level.
		return acceptedSpell == SpellID::FIRE_SHIELD || effectSpell == SpellID::FIRE_SHIELD;
	default:
		return false;
	}
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
			result.perks.wisdomEffectivenessBonus = newHorizonsWisdom::effectivenessBonusPercent(hero, *battle, side, spell);
			result.chaosShieldLimitBasisPoints = hero->hasActivePerk("new-horizons:chaosMagic",
				"new-horizons:chaosMagic.paradoxShield") ? 9000 : 8000;
			result.heroSide = side;
			break;
		}
	return result;
}

int32_t targetFreeEffectivenessBonusPercent(const CastProfile * cast)
{
	return cast ? cast->perks.wisdomEffectivenessBonus : 0;
}
int32_t outgoingDuration(int32_t ordinary, const CastProfile * cast, const battle::Unit *)
{
	return cast ? scaleDuration(ordinary, cast->perks) : ordinary;
}
int64_t outgoingMagnitude(int64_t ordinary, const CastProfile * cast, const battle::Unit *)
{
	return cast ? scaleMagnitude(ordinary, cast->perks) : ordinary;
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
	const int32_t offensiveBonus = targetFreeEffectivenessBonusPercent(cast) + factionBonus;
	if(offensiveBonus == 0)
		return newHorizonsCommand::mitigateIncomingDamage(callback, recipient, ordinary);
	const int capPercent = offensiveBonus > 0 ? recipient->valOfBonuses(BonusType::DAMAGE_RECEIVED_CAP) : 0;
	const int64_t cap = capPercent > 0 ? static_cast<int64_t>(recipient->getMaxHealth()) * capPercent / 100 : 0;
	const auto adjusted = spellDamageOutput(ordinary, cast ? cast->perks : Profile{}, cap, factionBonus);
	return newHorizonsCommand::mitigateIncomingDamage(callback, recipient, adjusted);
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
			for(auto & bonus : bonuses)
			{
				if(bonus.type == BonusType::BIND_EFFECT
					&& bonus.stacking == newHorizonsSorcerySpells::FORCE_LANCE)
					continue; // Fixed one-round pin bypasses all ordinary effect scaling.
				if(bonus.source != BonusSource::SPELL_EFFECT
					|| (bonus.sid != BonusSourceID(acceptedSpell) && bonus.sid != BonusSourceID(effectSpell)))
					continue;
				if(isChaosShieldMagnitude(bonus, acceptedSpell, effectSpell))
					bonus.val = chaosShieldMagnitude(bonus.val, cast->perks, cast->chaosShieldLimitBasisPoints);
				else if(isMisfortuneChanceFactor(bonus, acceptedSpell, effectSpell))
					bonus.val = misfortuneChanceFactor(bonus.val, cast->perks);
				else if(const auto limit = lightReductionLimit(bonus, acceptedSpell, effectSpell))
					bonus.val = std::min(*limit, scaleSignedBonus(bonus.val, cast->perks));
				else if(numericalBonus(bonus, acceptedSpell, effectSpell))
					bonus.val = scaleSignedBonus(bonus.val, cast->perks);
				if((bonus.duration & BonusDuration::N_TURNS) != 0 && bonus.turnsRemain > 0)
					bonus.turnsRemain = static_cast<int16_t>(std::min<int32_t>(std::numeric_limits<int16_t>::max(),
						scaleDuration(bonus.turnsRemain, cast->perks)));
			}
		}
	};
	adjust(packet.toAdd);
	adjust(packet.toUpdate);
	enchantmentCapabilities(cast->hero, callback, acceptedSpell, effectSpell, packet);
}

}

namespace spells
{
int32_t Mechanics::adjustMetamagicBonus(int32_t ordinary, const battle::Unit * recipient) const
{
	const auto * cast = getMetamagicCastProfile();
	return cast && recipient ? newHorizonsMetamagic::scaleSignedBonus(ordinary, cast->perks) : ordinary;
}

int32_t Mechanics::adjustMetamagicDuration(int32_t ordinary, const battle::Unit * recipient) const
{
	if(getSpellId().toSpell()->getJsonKey() == newHorizonsSorcerySpells::FORCE_LANCE) return ordinary;
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
	// native cap, then snapshot this component; later attacks never read current caster effectiveness.
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
