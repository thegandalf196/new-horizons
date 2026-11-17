/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleLastStandDamage.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/UnitShieldState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include <stdexcept>
namespace battleLastStandDamage
{
bool prepare(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, BattleAttack & packet,
	BattleStackAttacked & hit, battle::CUnitState & state, std::array<bool, 2> & usedThisAttack, bool retaliation,
	const battle::DamageContext & context)
{
	const auto * target = attack.defender;
	const auto side = battle.playerToSide(battle.battleGetOwner(target));
	const bool valid = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER;
	const auto index = valid ? static_cast<size_t>(side) : 0;
	const auto * hero = valid ? battle.battleGetFightingHero(side) : nullptr;
	const bool eligible = valid
		&& newHorizonsArmorer::isEligiblePhysicalAttack(attack.attacker, attack.physicalDamage, packet.spellLike())
		&& newHorizonsArmorer::canTriggerLastStand(hero, target);
	const bool used = valid && (battle.getBattle()->armorerLastStandUsed(side) || usedThisAttack[index]);
	if(!eligible || used)
		return false;
	const auto preview = battle::previewShieldDamage(state, hit.damageAmount,
		battle::DamageProvenance::PHYSICAL_CREATURE, &context);
	if(!preview)
		throw std::runtime_error("Last Stand requires an exact shield and casualty preview");
	if(preview->healthLoss < target->getAvailableHealth())
		return false;
	const int64_t shieldAbsorption = hit.damageAmount - preview->postShield;
	const auto result = newHorizonsArmorer::resolveLastStandDamage(hit.damageAmount,
		shieldAbsorption, 1, target->getAvailableHealth(), eligible, used);
	if(!result.triggered)
		return false;
	hit.damageAmount = result.damageToApply;
	hit.armorerLastStandSide = side;
	usedThisAttack[index] = true;
	state.armorerLastStandDefending = true;
	if(retaliation && battle.getBattle()->getActiveStackID() == static_cast<int32_t>(target->unitId()))
	{
		hit.armorerLastStandEndsActivation = true;
		state.armorerLastStandEndedActivation = true;
		packet.flags |= BattleAttack::LAST_STAND_RETALIATION;
	}
	return true;
}
}
