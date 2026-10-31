/*
 * NewHorizonsCureHealing.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <cstdint>
class CGHeroInstance;
namespace newHorizonsMagic
{
/// Complete non-resurrection Cure budget; cleansing and target legality are separate.
/// Fixed/SP floors stay distinct; Healer strengthens only the final SP component.
/// Recipient bonuses scale the SP numerator before the same existing specialty floors.
DLL_LINKAGE int64_t cureHealingBudget(const CGHeroInstance * hero, int32_t rawSpellPower,
	int32_t coefficientBasisPoints, int32_t warcastingPercent = 0, int32_t empowerPercent = 0,
	int32_t recipientSpellPowerBonusPercent = 0);
}
