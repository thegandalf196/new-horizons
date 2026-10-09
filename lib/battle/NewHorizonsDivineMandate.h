/*
 * NewHorizonsDivineMandate.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../Global.h"

#include <cstdint>
#include <vector>

class CGHeroInstance;
class CBattleInfoCallback;
class IBattleState;
struct Bonus;
struct HeroOrderState;
enum class BattleSide : int8_t;

namespace newHorizonsDivineMandate
{
/// Normal Spell Points recovered by Chaplain's Reserve when the first completed
/// Divine Mandate pair transitions from zero to one in the current combat.
DLL_LINKAGE int32_t chaplainReserveRecovery(const CGHeroInstance * hero,
	uint8_t beforeCompletedPairs, uint8_t afterCompletedPairs);
/// Percentage-point efficiency captured when the selected Order payment is Divine Mandate.
DLL_LINKAGE int32_t sacredCommandEfficiencyBonusPercent(const CGHeroInstance * hero);
/// Additional percentage-point efficiency captured for a Knightly Sequence Order follow-up.
DLL_LINKAGE int32_t knightlySequenceOrderBonusPercent(const CGHeroInstance * hero);
/// Flat Mana reduction for a Knightly Sequence Light Spell follow-up.
DLL_LINKAGE int32_t knightlySequenceSpellCostReduction(const CGHeroInstance * hero);
/// Spell Power-derived percentage captured for an eligible Divine Mandate cast.
DLL_LINKAGE int32_t consecratedCastingBonusPercent(const CGHeroInstance * hero);
/// True when Divine Mandate and Purifying Mandate are active for this hero.
DLL_LINKAGE bool hasPurifyingMandatePerk(const CGHeroInstance * hero);
/// Captured only for an actual Divine Mandate Order follow-up.
DLL_LINKAGE bool hasRoyalStandardPerk(const CGHeroInstance * hero);
DLL_LINKAGE bool hasSharedPurposePerk(const CGHeroInstance * hero);
/// The original eligible recipients, independent of later Order consumption.
DLL_LINKAGE std::vector<uint32_t> sharedPurposeOrderRecipients(
	const CBattleInfoCallback & battle, BattleSide side, const HeroOrderState & order);
DLL_LINKAGE std::vector<uint32_t> sharedPurposeFriendlyRecipients(
	const CBattleInfoCallback & battle, BattleSide side, const std::vector<uint32_t> & recipients,
	bool includeDead = false);
DLL_LINKAGE Bonus sharedPurposeMoraleBonus();
DLL_LINKAGE bool isSharedPurposeMoraleBonus(const Bonus * bonus);
DLL_LINKAGE void applySharedPurpose(IBattleState & state, const CBattleInfoCallback & battle,
	BattleSide side, const std::vector<uint32_t> & recipients);
}
