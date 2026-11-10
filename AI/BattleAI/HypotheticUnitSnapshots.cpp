/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "HypotheticEffectSnapshotRules.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsNecromancyCombat.h"
#include "../../lib/bonuses/BonusList.h"

void HypotheticBattle::captureUnitSnapshots(const CBattleInfoCallback & source, bool hasParent)
{
	using namespace hypotheticEffectSnapshotRules;
	if(ownedFrameBatch)
	{
		getForUpdate(static_cast<uint32_t>(activeUnitId));
		return;
	}
	// Rebind every potential Veteran recipient, even with no current Order.
	// Other controllers retain the existing lazy timer/status policy. Owned
	// frame views already rebind every escaped unit through ownedFrameUnitsIf.
	for(const auto * unit : source.battleGetAllUnits(false))
	{
		if(!hasParent && !mayContainSerpentBindings && unit->hasBonusOfType(BonusType::BIND_EFFECT))
		{
			const auto bindings = unit->getBonuses(Selector::type()(BonusType::BIND_EFFECT));
			for(const auto & bonus : *bindings)
				if(bonus) noteSerpentBindingAdmission(*bonus);
		}
		const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
		const bool pendingPoison = unit->physicalPoisonSpellOrigin() != BattleSide::NONE
			|| newHorizonsBulwark::physicalPoisonTickDamage(state) > 0;
		const auto side = source.playerToSide(source.battleGetOwner(unit));
		const bool orderStats = (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
			&& orderBenefitStatValues.at(side) != 0;
		if(unit->unitId() == static_cast<uint32_t>(activeUnitId) || orderStats
			|| hasTimedMagicalSpellEffects(*unit) || hasSnapshotHeroSpecialEffects(*unit)
			|| newHorizonsNecromancy::needsCombatPassiveSnapshot(*unit)
			|| newHorizonsFrozen::isFrozen(*unit) || pendingPoison || (state && state->moraleExtraActivation))
		{
			const auto captured = getForUpdate(unit->unitId());
			if(pendingPoison) captured->captureEffects();
		}
	}
}
