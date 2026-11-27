/*
 * HeroSpellCastContext, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroSpellCastContext.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsCrossSchoolFormula.h"
#include "NewHorizonsInfernoSpecialties.h"
#include "NewHorizonsSpellcraft.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/HeroActionAllowanceState.h"
#include "../battle/NewHorizonsDivineMandate.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/CGTownInstance.h"
namespace newHorizonsMagic
{
namespace
{
}
int32_t HeroSpellCastContext::componentBonusPercent() const
{
	const int combined = static_cast<int>((100LL + arcaneFocus) * grandFormula * (100 + consecrated)
		* (100 + counterpressure) * (100 + concentration) * (100 + crossSchool) / 10000000000LL);
	return combined - 100;
}
HeroSpellCastContext heroSpellCastContext(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback & callback, bool concentrated, bool automaticHeroCast)
{
	HeroSpellCastContext result;
	const auto casterSide = callback.playerToSide(hero.getCasterOwner());
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		return result;
	const auto * battleInfo = callback.getBattle();
	const int32_t battleRound = battleInfo ? battleInfo->getRound() : -1;
	const auto spellAllowance = !automaticHeroCast ? callback.battleGetSpellActionAllowance(casterSide, spell.getId())
		: std::optional<HeroActionAllowanceState::Selection>{};
	if(battleInfo && battleInfo->getSideHero(casterSide) == &hero && spellAllowance
		&& spellAllowance->allowance == HeroActionAllowanceState::AllowanceKind::SPELL
		&& spellAllowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE)
		result.consecrated = newHorizonsDivineMandate::consecratedCastingBonusPercent(&hero);
	if(battleInfo && battleInfo->getSideHero(casterSide) == &hero
		&& !battleInfo->hasCompletedHeroSpellCast(casterSide)
		&& hero.hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsMagic::SPELLCRAFT_ARCANE_FOCUS)))
		result.arcaneFocus = newHorizonsMagic::SPELLCRAFT_ARCANE_FOCUS_BONUS_PERCENT;

	if(battleInfo && battleInfo->getSideHero(casterSide) == &hero
		&& battleRound >= 0 && battleInfo->getSpellResponseState(casterSide).isReadyAt(battleRound)
		&& hero.hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE)))
		result.counterpressure = newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE_BONUS_PERCENT;

	if(battleInfo && battleInfo->getSideHero(casterSide) == &hero)
		{
		result.crossSchool = newHorizonsCrossSchoolFormula::bonusPercent(*battleInfo, casterSide, spell.getId());
		result.kindling = !automaticHeroCast ? infernoSpellSpecialties::kindlingBonusBasisPoints(*battleInfo, casterSide, spell.getId()) : 0;
	}

	const int spellLevel = battleInfo ? callback.battleGetSpellLevel(spell.getId()) : 0;
	if(battleInfo && battleInfo->getSideHero(casterSide) == &hero
		&& (spellLevel == 4 || spellLevel == 5)
		&& !battleInfo->hasCompletedHeroSpellLevel(casterSide, 4)
		&& !battleInfo->hasCompletedHeroSpellLevel(casterSide, 5)
		&& hero.hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsMagic::SPELLCRAFT_GRAND_FORMULA)))
		result.grandFormula = newHorizonsMagic::SPELLCRAFT_GRAND_FORMULA_MULTIPLIER_PERCENT;
	result.concentration = concentrated && battleInfo && battleInfo->getSideHero(casterSide) == &hero
		&& rulesActive(battleInfo->getMagicRules()) && spellAllowedBySavedRoster(battleInfo->getMagicRules(), spell.getId())
		&& hero.hasActivePerk(std::string(SPELLCRAFT_SKILL), std::string(newHorizonsSpellcraft::CONCENTRATION)) ? 15 : 0;
	return result;
}
bool heroCastExtendAvailable(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback & callback, int32_t effectLevel)
{
	const auto * battle = callback.getBattle();
	const auto side = callback.playerToSide(hero.getCasterOwner());
	return battle && (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& battle->getSideHero(side) == &hero
		&& newHorizonsSpellcraft::extendAvailable(*battle, side, spell, effectLevel);
}
int32_t brimstoneSpellPowerBonus(const CGHeroInstance & hero, const CBattleInfoCallback & callback)
{
	const auto * defendedTown = callback.battleGetDefendedTown();
	return rulesActive(hero.getMagicRules()) && defendedTown && defendedTown->getFactionID() == FactionID::INFERNO
		&& defendedTown->hasBuilt(BuildingID::SPECIAL_2)
		&& callback.battleGetFightingHero(BattleSide::DEFENDER) == &hero ? 20 : 0;
}
}
