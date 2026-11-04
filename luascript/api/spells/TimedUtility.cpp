/*
 * TimedUtility.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "Mechanics.h"
#include "../adventure/HeroInstance.h"
#include "../battle/Unit.h"
#include "../../../lib/spells/NewHorizonsTimedUtility.h"
#include "../../../lib/spells/NewHorizonsTimedStatusPlan.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/spells/NewHorizonsTimedMagnitudes.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
namespace scripting::api
{
using ::spells::Mechanics;
int64_t MechanicsProxy::getHolyArmorReduction(const ::spells::Mechanics & m, int32_t configuredBase,
	const battle::Unit & unit)
{
	return newHorizonsMagic::holyArmorReductionForRecipient(m, configuredBase, unit);
}
int32_t MechanicsProxy::getForgetfulnessDuration(const ::spells::Mechanics & m)
{
	return newHorizonsMagic::forgetfulnessDurationForCast(m);
}
int64_t MechanicsProxy::getHexOfPainDamageSharePercent(const ::spells::Mechanics & m)
{
	return m.scaleHeroLuckDamage(newHorizonsMagic::hexOfPainDamageSharePercent());
}
namespace
{
int32_t currentEntangleDuration(const ::spells::Mechanics & m)
{
	return newHorizonsMagic::entangleDurationForCast(m);
}
int32_t currentMisfortuneDuration(const ::spells::Mechanics & m)
{
	return newHorizonsMagic::misfortuneDurationForCast(m);
}
int32_t currentMisfortuneChance(const ::spells::Mechanics & m)
{
	return newHorizonsMagic::misfortuneChanceForCast(m);
}
int32_t currentChaosShield(const ::spells::Mechanics & m)
{
	return newHorizonsMagic::chaosShieldReductionForCast(m);
}
int64_t currentDivineCap(const ::spells::Mechanics & m, const battle::Unit & unit)
{
	return m.getDivineRetributionCap(&unit);
}
int32_t currentSpellLockDuration(const ::spells::Mechanics & m)
{
	const auto * hero = m.getHeroCaster();
	return m.adjustEffectDuration(newHorizonsSorcery::spellLockDurationBasisPoints(m.getEffectPower(),
		hero && hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.spellbinder"),
		m.getSpellPowerCoefficientBasisPoints(), m.getEmpowerSpellBonusPercent()));
}
int64_t currentHeavenlyGaleReduction(const ::spells::Mechanics & m, const battle::Unit & unit)
{
	return newHorizonsMagic::heavenlyGaleReductionForRecipient(m, unit);
}
}
void MechanicsProxy::registerTimedUtilityMethods(MethodRegistrar & R)
{
	R.function<&newHorizonsMagic::misfortuneValueForRecipient>("getMisfortuneRecipientValue",
		{{"recipient", "Actual recipient whose tier selects the native specialty."}, {"ordinary", "Ordinary buffered value."}}, {},
		"Uses the native scaling, peculiar-enchant and caster-enchant order once.");
	R.function<&newHorizonsMagic::configuredTimedDurationForCast>("getConfiguredTimedDuration",
		{{"configuredTurns", "Actual loaded timer, or zero."}, {"fallbackTurns", "Already selected cast timer."},
		 {"roundTimer", "Whether a positive literal timer uses N_TURNS."}}, {},
		"Uses the exact native timed-effect fallback and extension order.");
	R.function<&currentEntangleDuration>("getEntangleDuration", {}, "Shared capped Rootcaller duration followed by cast extensions.");
	R.function<&currentMisfortuneDuration>("getMisfortuneDuration", {}, "Shared capped Misfortune duration followed by cast extensions.");
	R.function<&currentMisfortuneChance>("getMisfortuneChanceMultiplier", {}, "Shared chance multiplier in basis points; no random draw.");
	R.function<&currentChaosShield>("getShieldOfChaosReduction", {}, "Shared stored reduction in basis points, including Paradox after cap.");
	R.function<&currentDivineCap>("getDivineRetributionCap", {{"target", "Recipient of the protection."}}, {},
		"Shared stored reflection cap before Retributionist payout.");
	R.function<&currentSpellLockDuration>("getSpellLockDuration", {},
		"Returns the shared capped Spell Lock duration followed by this cast's Extend/Echo adjustment.");
	R.function<&currentHeavenlyGaleReduction>("getHeavenlyGaleReductionBasisPoints",
		{{"target", "Recipient of this bonus."}}, {},
		"Returns Heavenly Gale's shared capped reduction, with Aegis and this recipient's Crown and Altar.");
	R.function<&MechanicsProxy::getHexOfPainDamageSharePercent>("getHexOfPainDamageSharePercent", {},
		"Returns Hex of Pain's authored damage-share percentage with the originating cast's resolved Luck multiplier; no RNG.");
	R.function<&MechanicsProxy::getHolyArmorReduction>("getHolyArmorReduction",
		{{"configuredBase", "Fixed reduction from this effect's loaded bonus."}, {"target", "Recipient of this bonus."}}, {},
		"Returns Holy Armor's shared fixed-base plus scaled-power reduction, including Aegis and this recipient's Crown and Altar.");
	R.function<&MechanicsProxy::getForgetfulnessDuration>("getForgetfulnessDuration", {},
		"Returns the shared capped Forgetfulness duration followed by this cast's Extend and Echoed Duration adjustments.");
	R.method<&Mechanics::getExtendSpellBonusRounds>("getExtendSpellBonusRounds", {},
		"Returns this cast's snapshotted Extend Spell round bonus without changing other duration modifiers.");
	R.function<&newHorizonsMagic::timedLightEffectDuration>("getTimedLightEffectDuration", {},
		"Returns the native Light enchantment duration, including spell-specific fixed bases and caps.");
	R.method<&Mechanics::getEffectDuration>("getEffectDuration", {},
		"Returns the effect duration in turns.");
	R.method<&Mechanics::adjustEffectDuration>("adjustEffectDuration",
		{{"baseDuration", "Base effect duration in turns."}}, {},
		"Returns the base duration adjusted by cast-specific duration mechanics.");
}
}
