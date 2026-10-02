/*
 * Mechanics.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "Mechanics.h"

#include "../Registry.h"
#include "../../LuaStack.h"
#include "../../LuaCallWrapper.h"

#include "../adventure/HeroInstance.h"
#include "../battle/Unit.h"
#include "../callback/IBattleInfoCallback.h"
#include "../library/Spell.h"

#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsBlink.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/battle/Unit.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/texts/CGeneralTextHandler.h"
#include "../../../lib/texts/Languages.h"

#include <vcmi/spells/Service.h>

namespace scripting::api
{
using ::spells::Mechanics;

bool MechanicsProxy::ownerMatchesUnit(const Mechanics & m, const battle::Unit & unit)
{
	return m.ownerMatches(&unit);
}

bool MechanicsProxy::ownerIsSameAsUnit(const Mechanics & m, const battle::Unit & unit)
{
	return m.ownerMatches(&unit, true);
}

bool MechanicsProxy::isProtectedAreaCenter(const Mechanics & m, const battle::Unit & unit, BattleHex centerHex)
{
	if(!m.usesNewHorizonsMagic() || !centerHex.isValid())
		return false;

	const auto * spell = m.getSpell();
	if(!spell)
		return false;

	const auto & spellKey = spell->getJsonKey();
	if(spellKey != "core:fireball" && spellKey != "core:inferno" && spellKey != "core:meteorShower")
		return false;

	const auto * hero = m.getHeroCaster();
	if(!hero || !hero->hasActivePerk("new-horizons:havocMagic", "new-horizons:havocMagic.controlledBlast"))
		return false;

	const auto * battle = m.battle();
	if(!battle)
		return false;

	const auto * centerUnit = battle->battleGetUnitByPos(centerHex, true);
	if(!centerUnit || centerUnit->unitId() != unit.unitId())
		return false;

	const auto controllingSide = battle->playerToSide(battle->battleGetOwner(centerUnit));
	return controllingSide == m.getCasterSide();
}

bool MechanicsProxy::isNatureSpell(const Mechanics & m)
{
	const auto * battle = m.battle();
	if(!battle)
		return false;
	for(const auto school : battle->battleGetSpellSchools(m.getSpellId()))
		if(school.serializationKey() == "new-horizons:nature")
			return true;
	return false;
}

std::string MechanicsProxy::getPluralFormTextID(const spells::Mechanics & m, const std::string & baseTextID, int32_t count)
{
	std::string lang = LIBRARY->generaltexth->getPreferredLanguage();
	return Languages::getPluralFormTextID(lang, count, baseTextID);
}

std::string MechanicsProxy::getCureAfflictionSource(const spells::Mechanics & m)
{
	const auto * spell = m.getCureAffliction().toSpell();
	return spell ? spell->getJsonKey() : std::string();
}

int32_t MechanicsProxy::getArcaneBreachMarkBasisPoints(const spells::Mechanics & m)
{
	using namespace newHorizonsSorcery;
	// Validate the same domain as the shared unmodified formula. Warcasting
	// and the saved School x Spellcraft coefficient boost only the Spell Power component,
	// before the final per-mark cap.
	const auto effectPower = m.getEffectPower();
	arcaneBreachMarkBasisPoints(effectPower);
	const auto component = m.scaleSpellPowerComponentWithCoefficientBasisPoints(
		static_cast<int64_t>(effectPower) * ARCANE_BREACH_POWER_BASIS_POINTS,
		1, m.getSpellPowerCoefficientBasisPoints());
	return static_cast<int32_t>(std::min<int64_t>(ARCANE_BREACH_CAP_BASIS_POINTS,
		ARCANE_BREACH_BASE_BASIS_POINTS + component));
}

int32_t MechanicsProxy::getBattleRound(const spells::Mechanics & m)
{
	const auto * battle = m.battle();
	return battle ? battle->battleGetRound() : -1;
}

int MechanicsProxy::getBlinkPreview(lua_State * L)
{
	LuaStack S(L);
	const Mechanics * mechanics;
	const battle::Unit * unit;
	S.getNonNull(1, mechanics);
	S.getNonNull(2, unit);
	S.clear();

	const auto result = newHorizonsBlink::preview(*mechanics, unit);
	if(!result)
	{
		S.pushNil();
		return 1;
	}

	lua_newtable(L);
	const int tableIndex = S.absindex(-1);
	S.push(result->radius);
	lua_setfield(L, tableIndex, "radius");
	S.push(result->legalDestinations);
	lua_setfield(L, tableIndex, "legalDestinations");
	S.push(result->blinkmaster);
	lua_setfield(L, tableIndex, "blinkmaster");
	return 1;
}

BattleHex MechanicsProxy::chooseBlinkmasterDestination(const Mechanics & m, const battle::Unit & unit,
	BattleHex first, BattleHex second)
{
	const auto result = newHorizonsBlink::preview(m, &unit);
	if(!result || !vstd::contains(result->legalDestinations, first)
		|| !vstd::contains(result->legalDestinations, second))
		return BattleHex::INVALID;
	if(!result->blinkmaster)
		return first;
	return newHorizonsBlink::fartherDestination(unit.getPosition(), first, second);
}

void MechanicsProxy::registerMethods(MethodRegistrar & R)
{
	R.method<&Mechanics::isPositiveSpell>("isPositive", {},
		"True if the spell mechanics classify this cast as a positive effect.");
	R.method<&Mechanics::isNegativeSpell>("isNegative", {},
		"True if the spell mechanics classify this cast as a negative effect.");
	R.method<&Mechanics::isMagicMirror>("isMagicMirror", {},
		"True when this spell effect is being resolved after Magic Mirror reflection.");
	R.method<&Mechanics::isSmart>("isSmart", {},
		"True if the spell only affects friendly or enemy targets (vs. anyone in range).");
	R.method<&Mechanics::isMassive>("isMassive", {},
		"True if the spell targets all valid targets simultaneously.");
	R.method<&Mechanics::alwaysHitFirstTarget>("alwaysHitFirstTarget", {},
		"True if the first selected target is always considered hit, ignoring magic resistance.");
	R.method<&Mechanics::wouldResist>("wouldResist",
		{{"target", "Unit whose resistance is being tested."}}, {},
		"Returns true if the given target would resist this cast.");

	R.method<&Mechanics::getEffectLevel>("getEffectLevel", {},
		"Returns the effective mastery level used for the spell's magnitude.");
	R.method<&Mechanics::getRangeLevel>("getRangeLevel", {},
		"Returns the effective mastery level used for the spell's range.");
	R.function<&MechanicsProxy::getArcaneBreachMarkBasisPoints>("getArcaneBreachMarkBasisPoints", {},
		"Returns one Arcane Breach mark's penetration in basis points, including the cast's Warcasting boost to its Spell Power component before the cap.");
	R.method<&Mechanics::getEffectPower>("getEffectPower", {},
		"Returns the effective spell power applied to the magnitude calculation.");
	R.method<&Mechanics::getEffectPowerDivisor>("getEffectPowerDivisor", {},
		"Returns the saved caster power divisor; legacy and ordinary creature casts use one.");
	R.method<&Mechanics::getSchoolRankPowerCoefficientPercent>("getSchoolRankPowerCoefficientPercent", {},
		"Returns the effective school-rank Spell Power coefficient from the saved battle rules and caster; "
		"v1/v2 and excluded spells use 100%.");
	R.method<&Mechanics::getEffectiveChainLength>("getEffectiveChainLength",
		{{"configuredLength", "Target count configured for this spell mastery."}}, {},
		"Returns the chain-effect target count resolved against the saved battle rules. "
		"New Horizons v3 fixes core Chain Lightning at five targets; legacy/v1/v2 retain content values.");
	R.method<&Mechanics::isNewHorizonsStormOfDaggers>("isNewHorizonsStormOfDaggers", {},
		"True when this cast uses the saved v3 New Horizons Storm of Daggers rules.");
	R.method<&Mechanics::setStormOfDaggersTargetCount>("setStormOfDaggersTargetCount",
		{{"selectedTargetCount", "Number of distinct enemy stacks selected, from one to five."}}, {},
		"Sets the selection count for the shared Storm of Daggers preview value. Returns false for an invalid count or another spell.");
	R.method<&Mechanics::getStormOfDaggersDamagePerTarget>("getStormOfDaggersDamagePerTarget",
		{{"selectedTargetCount", "Number of distinct enemy stacks selected, from one to five."}}, {},
		"Returns the raw, equal damage assigned to each selected stack before target-specific resistance or mitigation.");
	R.method<&Mechanics::getStormOfDaggersTotalDamage>("getStormOfDaggersTotalDamage",
		{{"selectedTargetCount", "Number of distinct enemy stacks selected, from one to five."}}, {},
		"Returns the rounded raw Storm of Daggers total before target-specific resistance or mitigation.");
	R.method<&Mechanics::getEffectDuration>("getEffectDuration", {},
		"Returns the effect duration in turns.");
	R.function<&MechanicsProxy::getBattleRound>("getBattleRound", {},
		"Returns the current battle round, or -1 when the cast has no battle context.");
	R.cfunction<&MechanicsProxy::getBlinkPreview>("getBlinkPreview",
		{{"unit", "Unit", "Creature stack whose landing radius is previewed."}},
		{"table | nil", "A table containing radius, legalDestinations, and blinkmaster, or nil when this cast/target has no legal Blink preview."},
		"Returns the shared saved-v3 Blink radius and sorted legal landing hexes without consuming random state.");
	R.function<&MechanicsProxy::chooseBlinkmasterDestination>("chooseBlinkmasterDestination",
		{
			{"unit", "Creature stack whose origin hex is used."},
			{"first", "First legal destination drawn by the authoritative RNG."},
			{"second", "Second independent legal destination drawn with replacement."}
		}, {},
		"Resolves the farther of two Blinkmaster destinations, using lower BattleHex id for equal-distance ties.");
	R.method<&Mechanics::adjustEffectDuration>("adjustEffectDuration",
		{{"baseDuration", "Base effect duration in turns."}}, {},
		"Returns the base duration adjusted by cast-specific duration mechanics.");
	R.method<&Mechanics::isSelectiveDispel>("isSelectiveDispel", {},
		"True when this authoritative cast selected the Sorcery Selective Dispel mode.");
	R.method<&Mechanics::isMetamagicFollowup>("isMetamagicFollowup", {},
		"True when this hero spell is being cast through an additional Metamagic Spell Action.");
	R.method<&Mechanics::isNewHorizonsCure>("isNewHorizonsCure", {},
		"True when this cast uses the explicitly saved New Horizons Cure behavior.");
	R.function<&MechanicsProxy::getCureAfflictionSource>("getCureAfflictionSource", {},
		"Returns the selected Cure affliction source key, or an empty string for heal-only.");
	R.method<&Mechanics::isMassSlow>("isMassSlow", {},
		"True when this authoritative cast selected the Sorcery Temporal Field Mass Slow mode.");
	R.method<&Mechanics::getShadowGiftSacrificePercent>("getShadowGiftSacrificePercent", {},
		"Returns the selected saved-v3 Shadow Gift sacrifice tier (10, 20 or 30), or zero when absent.");
	R.method<&Mechanics::getShadowGiftSacrificeCostBasisPoints>("getShadowGiftSacrificeCostBasisPoints", {},
		"Returns the actual current/max HP sacrifice percentage in basis points, including Dark Gift's cost reduction.");
	R.method<&Mechanics::getShadowGiftDamageBonusBasisPoints>("getShadowGiftDamageBonusBasisPoints", {},
		"Returns the selected Shadow Gift damage bonus in basis points, with only the Spell Power term scaled by saved School, Spellcraft, Warcasting and Empower.");
	R.method<&Mechanics::usesNewHorizonsMagic>("usesNewHorizonsMagic", {},
		"True when the battle uses a saved New Horizons magic-rules snapshot.");
	R.method<&Mechanics::usesNewHorizonsMagicV3>("usesNewHorizonsMagicV3", {},
		"True when the battle uses a saved New Horizons magic-rules v3 snapshot.");
	R.method<&Mechanics::usesNewHorizonsQuicksandSelectedPlacement>("usesNewHorizonsQuicksandSelectedPlacement", {},
		"True when the saved battle rules enable exact caster-selected Quicksand placement.");
	R.method<&Mechanics::usesNewHorizonsMultiplicativeMDR>("usesNewHorizonsMultiplicativeMDR", {},
		"True when the saved New Horizons spell roster contains the Holy Armor marker and uses independent multiplicative magical damage reduction.");
	R.function<&MechanicsProxy::isNatureSpell>("isNatureSpell", {},
		"True when the authoritative saved spell-school mapping classifies this cast as Nature.");
	R.method<&Mechanics::getEffectValue>("getEffectValue", {},
		"Returns the computed effect value (e.g. damage / health amount).");
	R.method<&Mechanics::getCasterColor>("getCasterColor", {},
		"Returns the player color of the caster.");
	R.method<&Mechanics::getCasterSide>("getCasterSide", {},
		"Returns the battle side of the caster.");
	R.method<&Mechanics::getHeroCaster>("getHeroCaster", {},
		"Returns the hero performing the cast, or nil if cast by a unit.");
	R.method<&Mechanics::getSummonedCreatureMaxHealth>("getSummonedCreatureMaxHealth",
		{{"creature", "Creature template whose temporary stack would be created."},
		 {"natureSummoned", "Whether the hypothetical stack has Nature-summon provenance."}}, {},
		"Returns the max health per creature after applying the normal hero and creature bonuses and limiters, "
		"without adding a unit to the live battle or bonus graph.");
	R.method<&Mechanics::getUnitCaster>("getUnitCaster", {},
		"Returns the unit performing the cast, or nil if cast by a hero.");
	R.method<&Mechanics::getCasterNameTextID>("getCasterNameTextID", {},
		"Returns the text ID of the caster's name.");
	R.method<&Mechanics::battle>("getBattle", {},
		"Returns the battle callback associated with this cast.");
	R.method<&Mechanics::calculateRawEffectValue>("calculateRawEffectValue",
		{
			{"basePowerMultiplier",  "Multiplier applied to the spell's base power."},
			{"levelPowerMultiplier", "Multiplier applied to the per-level power bonus."}
		}, {},
		"Returns the raw effect value before unit-specific adjustments.");
	R.method<&Mechanics::scaleSpellPowerComponent>("scaleSpellPowerComponent",
		{
			{"numerator", "An explicitly Spell-Power-derived numerator, before applying its divisor."},
			{"divisor", "Divisor applied after the optional Warcasting percentage; defaults to one."}
		}, {},
		"Applies this cast's snapshotted Warcasting percentage to an unranked Spell-Power-derived component, then divides "
		"it with integer truncation. Fixed base and level-power terms must be added separately.");
	R.method<&Mechanics::scaleSpellPowerComponentWithCoefficient>("scaleSpellPowerComponentWithCoefficient",
		{
			{"numerator", "An explicitly Spell-Power-derived numerator, before applying its divisor."},
			{"divisor", "Divisor applied after the optional Warcasting percentage."},
			{"coefficientPercent", "Saved school-rank Spell Power coefficient in percent."}
		}, {},
		"Applies the supplied percentage and this cast's snapshotted Warcasting percentage to a Spell-Power-derived component, "
		"then divides it with integer truncation. Fixed base and level-power terms must be added separately.");
	R.method<&Mechanics::getSpellPowerCoefficientBasisPoints>("getSpellPowerCoefficientBasisPoints", {},
		"Returns the composed Spellcraft, school-rank and cast-specific Spell Power coefficient in basis points. "
		"10000 basis points means 100%; legacy profiles and excluded spells use 10000.");
	R.method<&Mechanics::getNewHorizonsQuicksandPatchCount>("getNewHorizonsQuicksandPatchCount", {},
		"Returns the authoritative saved-v3 Quicksand patch count with School, Spellcraft, "
		"Warcasting, and Empower scaling; returns zero for legacy profiles and other spells.");
	R.method<&Mechanics::getNewHorizonsLandMinePatchCount>("getNewHorizonsLandMinePatchCount", {},
		"Returns the Land Mine placement count shared by casting, target selection and AI. "
		"Saved-v3 Spell Power scaling affects only the additional mines, preserving the fixed base and cap.");
	R.method<&Mechanics::scaleSpellPowerComponentWithCoefficientBasisPoints>("scaleSpellPowerComponentWithCoefficientBasisPoints",
		{
			{"numerator", "An explicitly Spell-Power-derived numerator, before applying its divisor."},
			{"divisor", "Divisor applied after the composed coefficient and optional Warcasting percentage."},
			{"coefficientBasisPoints", "Saved composed Spell Power coefficient in basis points; 10000 means 100%."}
		}, {},
		"Applies the supplied basis-point coefficient and this cast's snapshotted Warcasting percentage to a Spell-Power-derived component, "
		"then divides it with integer truncation. Fixed base and level-power terms must be added separately.");
	R.method<&Mechanics::applySpecificSpellBonus>("applySpecificSpellBonus",
		{{"value", "Base value to which spell-specific modifiers are applied. Use 0 for default"}}, {},
		"Applies any spell-specific bonus modifier and returns the resulting value.");
	R.method<&Mechanics::applySpellBonus>("applySpellBonus",
		{
			{"value",  "Base value to which the bonus is applied. Use 0 for default."},
			{"target", "Unit whose own bonuses are factored into the result."}
		}, {},
		"Applies the generic spell-damage bonus and returns the resulting value.");
	R.method<&Mechanics::isReceptive>("isReceptive",
		{{"target", "Unit whose receptivity is being checked."}}, {},
		"True if the target is receptive (not immune) to the spell.");
	R.function<&ownerMatchesUnit>("ownerMatches",
		{{"unit", "Unit whose ownership is being compared against the caster's."}}, {},
		"Matches ownership using the spell's ordinary positive/negative/neutral targeting rules.");
	R.function<&ownerIsSameAsUnit>("ownerIsSameAs",
		{{"unit", "Unit whose ownership is being compared against the caster's."}}, {},
		"True if the given unit is owned by the same player as the caster, independent of spell polarity.");
	R.function<&MechanicsProxy::isProtectedAreaCenter>("isProtectedAreaCenter",
		{{"unit", "Unit whose identity is compared with the original area center."},
		 {"centerHex", "Original targeted hex used to resolve the area center."}}, {},
		"True when saved New Horizons Controlled Blast rules exclude this friendly center unit from Fireball, Inferno, or Meteor Shower damage.");
	R.method<&Mechanics::getSpell>("getSpell", {},
		"Returns the Spell being cast.");
	R.method<&Mechanics::adjustEffectValue>("adjustEffectValue",
		{{"target", "Unit against which per-target adjustments are computed."}}, {},
		"Applies all per-target adjustments to the raw effect value.");
	R.method<&Mechanics::adjustRecipientDamage>("adjustRecipientDamage",
		{{"target", "Recipient of an already resolved raw hit."},
		 {"rawDamage", "Damage before this recipient's own modifiers."}}, {},
		"Applies recipient damage modifiers without repeating caster bonuses or execution rules.");
	R.method<&Mechanics::adjustEffectValueBeforeExecution>("adjustEffectValueBeforeExecution",
		{{"target", "Unit against which per-target adjustments are computed."}}, {},
		"Applies per-target damage adjustments before an execute-style threshold override. Mechanics without such an override return their ordinary adjusted value.");
	R.function<&MechanicsProxy::getPluralFormTextID>("getPluralFormTextID",
		{
			{"baseTextID", "Base text ID used as the lookup base."},
			{"count",      "Count for which engine needs to select plural form."}
		}, {},
		"Picks the appropriate plural-form variant of a text ID for the given count and language.");
}
}
