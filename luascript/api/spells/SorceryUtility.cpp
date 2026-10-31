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
	using namespace newHorizonsSorcery;
	// Validate the same domain as the shared unmodified formula. Warcasting
	// and the saved School x Spellcraft coefficient boost only the Spell Power component,
	// before the final per-mark cap.
	const auto effectPower = m.getEffectPower();
	// Focus Magic captures its own first-shot penetration. Other Arcane Breach
	// mark producers retain the ordinary formula, including post-hit marks.
	const auto * hero = m.getHeroCaster();
	const int specialty = hero && m.usesNewHorizonsMagicV3()
		&& m.getSpellId().toSpell()->getJsonKey() == FOCUS_MAGIC_SPELL
		? hero->getNonDamageSpellSpecialtyBonusPercent(m.getSpellId()) : 0;
	// Only this enchantment opts into revised potency. Other mark producers
	// and all historical captures continue to use the original formula.
	const auto * state = m.battle() ? m.battle()->getBattle() : nullptr;
	const auto revised = state && m.getSpellId().toSpell()->getJsonKey() == FOCUS_MAGIC_SPELL
		? newHorizonsSorceryRevision::focusRules(state->getMagicRules()) : std::nullopt;
	const auto profile = revised.value_or(newHorizonsSorceryRevision::FocusRules{
		ARCANE_BREACH_BASE_BASIS_POINTS, ARCANE_BREACH_POWER_BASIS_POINTS, ARCANE_BREACH_CAP_BASIS_POINTS});
	return newHorizonsSorceryRevision::focusMarkBasisPoints(profile, effectPower,
		m.getSpellPowerCoefficientBasisPoints(), m.getWarcastingBonusPercent(), m.getEmpowerSpellBonusPercent(), specialty);
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
	return newHorizonsSorceryRevision::livingArmorHitPoints(
		MechanicsProxy::getLivingArmorParameter(m, "baseHitPoints"),
		MechanicsProxy::getLivingArmorParameter(m, "spellPowerCoefficient"),
		m.getEffectPower(), m.getEffectPowerDivisor(), m.getSpellPowerCoefficientBasisPoints(),
		m.getWarcastingBonusPercent(), m.getEmpowerSpellBonusPercent());
}
}
void registerSorceryUtilityMethods(MethodRegistrar & R)
{
	R.function<&MechanicsProxy::getLivingArmorParameter>("getLivingArmorParameter",
		{{"name", "Captured summon parameter name."}}, {},
		"Returns a captured Living Armor parameter, or zero when absent.");
	R.function<&MechanicsProxy::getArcaneBreachMarkBasisPoints>("getArcaneBreachMarkBasisPoints", {},
		"Returns one Arcane Breach mark's penetration in basis points, including the cast's Warcasting boost to its Spell Power component before the cap.");
	R.function<&currentLivingArmorHitPoints>("getLivingArmorHitPoints", {},
		"Returns the saved Living Armor HP pool using its loaded fixed base and the captured power-component multipliers. No targets or RNG.");
}
}
