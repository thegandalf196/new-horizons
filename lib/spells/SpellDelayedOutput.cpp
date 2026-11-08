/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicEffects.h"
#include "ObstacleCasterProxy.h"
#include "../battle/CObstacleInstance.h"
#include <cmath>
namespace newHorizonsMetamagic
{
CastProfile captureDelayedCastProfile(const spells::Caster * caster)
{
	CastProfile result;
	const auto * proxy = dynamic_cast<const spells::ObstacleCasterProxy *>(caster);
	if(!proxy || !proxy->sourceObstacle().damageSnapshot) return result;
	const auto & value = proxy->getCapturedMdrPenetration()["effectivenessBonusPercent"];
	if(!value.isNumber() || !std::isfinite(value.Float()) || value.Float() < 0
		|| value.Float() > 1000 || std::floor(value.Float()) != value.Float()) return result;
	// A uniform cast-time contribution shares the existing output stage. No
	// live hero/perk/point history is read at this later passive trigger.
	result.perks.wisdomEffectivenessBonus = static_cast<int32_t>(value.Integer());
	result.heroSide = proxy->sourceObstacle().casterSide;
	result.factionSpellDamageBonus = capturedOffensiveProfile(&proxy->getCapturedMdrPenetration()).factionSpellDamageBonus;
	return result;
}

}
