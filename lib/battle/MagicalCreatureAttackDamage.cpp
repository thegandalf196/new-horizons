/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPassiveSpecialties.h"
#include "MagicalCreatureAttackDamage.h"
#include "ArcaneInspiration.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "CommandArmyBonuses.h"
#include "CurrentFireBasis.h"
#include "Unit.h"
#include "../spells/MagicalDamageReduction.h"

namespace battle
{
void applyMagicalAttackReduction(const CBattleInfoCallback & callback,
	const BattleAttackInfo & attack, DamageEstimation & damage)
{
	if(attack.physicalDamage || !attack.defender)
		return;
	if(newHorizonsPassiveSpecialties::magicalDamageImmune(*attack.defender))
	{
		damage.damage.min = damage.damage.max = 0;
		damage.kills.min = damage.kills.max = 0;
		return;
	}
	// The attack calculator does not consume SPELL_DAMAGE_REDUCTION bonuses.
	// Admit these two projected sources here, once, alongside intrinsic/Spellward
	// so the common aggregate cap and relative penetration cover the whole fold.
	auto reductions = commandArmyBonuses::magicalReductionBasisPoints(*attack.defender);
	const int intrinsic = attack.defender->intrinsicMagicalDamageReductionPercent();
	if(intrinsic > 0)
		reductions.push_back(std::clamp(intrinsic, 0, 100) * 100);
	const int spellward = newHorizonsMagicalAbilityDamage::spellwardReductionBasisPoints(
		callback.battleGetOwnerHero(attack.defender));
	if(spellward > 0)
		reductions.push_back(std::clamp(spellward, 0, 10000));
	if(reductions.empty())
		return;
	auto penetrations = spells::targetMdrPenetrations(*attack.defender);
	const int ignored = arcaneInspiration::attackPlan(callback, attack).protectionIgnorePercent;
	if(ignored > 0)
		penetrations.push_back(ignored);
	for(auto * bound : {&damage.damage.min, &damage.damage.max})
		*bound = spells::calculateMagicalDamageReductionBasisPoints(*bound, reductions, penetrations).damageWithPenetration;
	const auto killedBy = [&](int64_t amount) -> int64_t
	{
		const auto integrity = attack.defender->getPhantomIntegrity();
		if(integrity > 0)
			return amount >= integrity ? attack.defender->getCount() : 0;
		const auto firstHealth = attack.defender->getFirstHPleft();
		return amount < firstHealth ? 0 : std::min<int64_t>(attack.defender->getCount(),
			1 + (amount - firstHealth) / std::max<uint32_t>(1, attack.defender->getMaxHealth()));
	};
	damage.kills.min = killedBy(damage.damage.min);
	damage.kills.max = killedBy(damage.damage.max);
}
}
