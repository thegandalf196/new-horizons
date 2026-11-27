/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "HeroSpellCastContext.h"
#include "NewHorizonsMagic.h"
#include "CSpell.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/NewHorizonsDivineMandate.h"
#include "../battle/MetamagicDoubleCast.h"
#include "../battle/QuickStudySpecialty.h"
#include "../entities/hero/NewHorizonsCastleSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
namespace spells
{
void BaseMechanics::initializeHeroCastContext(const IBattleCast * event)
{
	if(mode == Mode::HERO && dynamic_cast<const CGHeroInstance *>(caster)
		&& (casterSide == BattleSide::ATTACKER || casterSide == BattleSide::DEFENDER))
	{
		const auto * battleInfo = cb->getBattle();
		const auto * hero = dynamic_cast<const CGHeroInstance *>(caster);
		const auto scalarContext = newHorizonsMagic::heroSpellCastContext(*hero, *owner, *cb,
			event->isConcentrated(), automaticHeroCast.automatic());
		consecratedCastingBonusPercent = scalarContext.consecrated;
		arcaneFocusBonusPercent = scalarContext.arcaneFocus;
		counterpressureBonusPercent = scalarContext.counterpressure;
		crossSchoolFormulaBonusPercent = scalarContext.crossSchool;
		kindlingBonusBasisPoints = scalarContext.kindling;
		grandFormulaMultiplierPercent = scalarContext.grandFormula;
		concentrationBonusPercent = scalarContext.concentration;
		const auto spellAllowance = cb->battleGetSpellActionAllowance(casterSide, owner->getId());
		if(battleInfo && hero && spellAllowance
			&& spellAllowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
			&& spellAllowance->allowance == HeroActionAllowanceState::AllowanceKind::SPELL
			&& newHorizonsDivineMandate::hasCrownAndAltarPerk(hero))
		{
			const auto & grants = battleInfo->getHeroActionAllowances(casterSide).grants;
			const auto grant = std::find_if(grants.begin(), grants.end(), [&spellAllowance](const auto & entry)
				{ return entry.id == spellAllowance->grantId; });
			if(grant != grants.end())
				crownAndAltarRecipientUnitIds = grant->divineMandateRecipients;
		}

		overwhelmingFormulaEligible = battleInfo && hero && owner
			&& (owner->isNegative() || newHorizonsMagic::isLandMine(owner->getId())
				|| newHorizonsMagic::isFireWall(owner->getId()))
			&& owner->isMagical() && newHorizonsMagic::rulesActive(battleInfo->getMagicRules())
			&& battleInfo->getSideHero(casterSide) == hero
			&& hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
				"new-horizons:spellcraft.overwhelmingFormula");


	}

}
void BaseMechanics::initializeCastPower(const IBattleCast * event)
{

	{
		auto value = event->getSpellLevel();
		rangeLevel = value.value_or(caster->getSpellSchoolLevel(owner));
		vstd::abetween(rangeLevel, 0, 3);
	}
	{
		auto value = event->getSpellLevel();
		effectLevel = value.value_or(caster->getEffectLevel(owner));
		vstd::abetween(effectLevel, 0, 3);
	}
	{
		auto value = event->getEffectPower();
		effectPower = value.value_or(caster->getEffectPower(owner));
		vstd::amax(effectPower, 0);

		// Inferno's New Horizons Brimstone Stormclouds are a siege-only
		// Spell Power bonus for the defending hero.  Keep it in the battle
		// mechanics boundary so it affects every ordinary spell effect while
		// never leaking onto an adventure-map hero or an attacking hero.
		if(const auto * heroCaster = caster->getHeroCaster())
			effectPower += newHorizonsMagic::brimstoneSpellPowerBonus(*heroCaster, *cb)
				+ metamagicDoubleCast::crescendoSpellPower(*heroCaster, *cb);
		// Sanya's Quick Study: a copied spell uses a fraction of her Spell Power.
		if(const auto * heroCaster = caster->getHeroCaster(); heroCaster && mode == Mode::HERO && !value)
			effectPower = quickStudySpecialty::copyEffectPower(*heroCaster, owner->getId(), effectPower);
	}
}
}
