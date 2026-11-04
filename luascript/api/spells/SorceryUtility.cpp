/*
 * SorceryUtility.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "SorceryUtility.h"
#include "Mechanics.h"
#include "../adventure/HeroInstance.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/IBattleState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/NewHorizonsSorceryRevision.h"
namespace scripting::api
{
int32_t MechanicsProxy::getArcaneBreachMarkBasisPoints(const spells::Mechanics & m)
{
	return m.getArcaneBreachMarkBasisPoints();
}

int64_t MechanicsProxy::getLivingArmorParameter(const spells::Mechanics & m, const std::string & name)
{
	const auto * state = m.battle() ? m.battle()->getBattle() : nullptr;
	if(!state || (name != "baseHitPoints" && name != "spellPowerCoefficient" && name != "duration"))
		return 0;
	const auto & config = state->getMagicRules()["spells"][m.getSpell()->getJsonKey()]["livingArmor"];
	if(!config.isStruct() || config["version"].Integer() != 1)
		return 0;
	return config[name].Integer();
}

namespace
{
int64_t currentLivingArmorHitPoints(const ::spells::Mechanics & m)
{
	return m.adjustMetamagicMagnitude(newHorizonsSorceryRevision::livingArmorHitPoints(
		MechanicsProxy::getLivingArmorParameter(m, "baseHitPoints"),
		MechanicsProxy::getLivingArmorParameter(m, "spellPowerCoefficient"),
		m.getEffectPower(), m.getEffectPowerDivisor(), m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent()), nullptr);
}
}
void registerSorceryUtilityMethods(MethodRegistrar & R)
{
	R.function<&MechanicsProxy::getLivingArmorParameter>("getLivingArmorParameter",
		{{"name", "Captured summon parameter name."}}, {},
		"Returns a captured Living Armor parameter, or zero when absent.");
	R.function<&MechanicsProxy::getArcaneBreachMarkBasisPoints>("getArcaneBreachMarkBasisPoints", {},
		"Returns one Arcane Breach mark's penetration in basis points, including the cast's composed Spell Power component before the cap.");
	R.function<&currentLivingArmorHitPoints>("getLivingArmorHitPoints", {},
		"Returns the saved Living Armor HP pool using its loaded fixed base and the captured power-component multipliers. No targets or RNG.");
}
}
