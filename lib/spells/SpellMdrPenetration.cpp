/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "ObstacleCasterProxy.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsWisdom.h"
#include "NewHorizonsNaturesWrath.h"
#include "NewHorizonsMetamagicMarks.h"
#include "NewHorizonsMetamagicEffects.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
namespace spells
{
JsonNode BaseMechanics::getCapturedMdrPenetration() const
{
	if(mode == Mode::PASSIVE)
		if(const auto * obstacle = dynamic_cast<const ObstacleCasterProxy *>(caster))
			return obstacle->getCapturedMdrPenetration();
	JsonNode result;
	const auto * hero = caster ? caster->getHeroCaster() : nullptr;
	const auto * metamagic = getMetamagicCastProfile();
	if(mode != Mode::HERO || !hero || !newHorizonsMagic::rulesActive(hero->getMagicRules()))
		return result;
	result["sourceHeroCasterId"].Integer() = hero->id.getNum();
	// Capture offensive faction output for delayed statuses as well as obstacles.
	// Absence remains the identity value and leaves unrelated spell packets intact.
	if(metamagic && metamagic->factionSpellDamageBonus > 0)
		result["factionSpellDamageBonusPercent"].Integer() = metamagic->factionSpellDamageBonus;
	// Placement spells can be neutral. Capture their uniform output independently
	// of penetration eligibility, before the existing hostile-only guard below.
	if(metamagic && (newHorizonsMagic::isLandMine(owner->getId()) || newHorizonsMagic::isFireWall(owner->getId())))
		result["effectivenessBonusPercent"].Integer() = newHorizonsMetamagic::targetFreeEffectivenessBonusPercent(metamagic);
	if(!isNegativeSpell() && !overwhelmingFormulaEligible
		&& !newHorizonsNaturesWrath::enabled(*this) && !(metamagic && metamagic->perks.pointPolicy))
		return result;
	const auto add = [&result](int percent)
	{
		result["penetrations"].Vector().emplace_back(percent);
	};
	if(const auto * meta = getMetamagicCastProfile(); meta && meta->perks.pointPolicy)
		add(newHorizonsMetamagic::effectivenessBonusPercent(meta->perks, meta->points) - meta->perks.wisdomEffectivenessBonus);
	if(const int wisdom = newHorizonsWisdom::archmagePenetrationPercent(hero, hero->getMagicRules(), owner->getId()))
		add(wisdom);
	if(hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
		"new-horizons:spellcraft.spellPenetration"))
		add(20);
	if(newHorizonsMagic::hasAnnihilatorPerk(hero, owner))
		add(20);
	if(overwhelmingFormulaEligible && overwhelmingFormulaToken != 0)
	{
		result["overwhelmingFormulaSide"].Integer() = static_cast<int32_t>(casterSide);
		// A decimal string survives Lua's double-only numerical representation.
		result["overwhelmingFormulaToken"].String() = std::to_string(overwhelmingFormulaToken);
	}
	else if(overwhelmingFormulaEligible && cb && cb->getBattle()
		&& cb->getBattle()->getOverwhelmingFormulaState(casterSide).winningCastToken == 0
		&& cb->getBattle()->getOverwhelmingFormulaState(casterSide).lastCandidateCastToken
			!= std::numeric_limits<uint64_t>::max())
	{
		// Unaccepted UI prediction is read-only. Execution registers above before
		// preparing effects, so this temporary contribution is never saved on one.
		add(50);
	}
	if(metamagicFollowup && metamagicFocusedPairingEligible
		&& metamagicFirstTargetUnitId != std::numeric_limits<uint32_t>::max())
	{
		result["focusedTargetUnitId"].Integer() = metamagicFirstTargetUnitId;
		result["focusedPenetrationPercent"].Integer() = 20;
	}
	return result;
}

}
