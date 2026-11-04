/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MetamagicBindings.h"
#include "Mechanics.h"
#include "../battle/Unit.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsTimedMagnitudes.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/spells/RecipientSpellEffectPreview.h"

namespace scripting::api
{
using ::spells::Mechanics;
namespace
{
JsonNode recipientFrailtyPlan(const Mechanics & mechanics, const battle::Unit & recipient)
{
	const auto plan = mechanics.getRecipientFrailtyPlan(&recipient);
	JsonNode result;
	result["totalBasisPoints"].Integer() = plan.totalBasisPoints;
	result["defenseLoss"].Integer() = plan.defenseLoss;
	return result;
}
}

void registerMetamagicMethods(MethodRegistrar & R)
{
	R.function<&newHorizonsMagic::scriptedRoundStatusDuration>("getScriptedRoundStatusDuration", {},
		"Returns the shared native fixed timer for an explicitly owned scripted status, or nil.");
	R.function<&recipientFrailtyPlan>("getRecipientFrailtyPlan",
		{{"recipient", "Actual recipient of cumulative Frailty."}}, {},
		"Returns the exact next cumulative percentage and Defense loss under its native60% cap.");
	R.method<&Mechanics::adjustMetamagicBonus>("adjustMetamagicBonus",
		{{"ordinary", "Established signed stat bonus before Arcane Marks."},
		 {"recipient", "Recipient whose existing Arcane Marks apply."}}, {},
		"Scales the magnitude once and restores its original sign.");
	R.method<&Mechanics::adjustMetamagicDuration>("adjustMetamagicDuration",
		{{"ordinary", "Established native round duration before Arcane Marks."},
		 {"recipient", "Recipient whose existing Arcane Marks apply."}}, {},
		"Applies recipient Metamagic potency and Lingering Magic once to a positive round timer.");
	R.method<&Mechanics::adjustMetamagicMagnitude>("adjustMetamagicMagnitude",
		{{"ordinary", "Normal spell magnitude after its established modifiers."},
		 {"recipient", "Recipient whose existing Arcane Marks apply."}}, {},
		"Applies only this recipient's passive Metamagic effectiveness, once.");
	R.method<&Mechanics::getHandOfFateCollateralPool>("getHandOfFateCollateralPool",
		{{"primary", "Primary recipient excluded from the native random pool."}}, {},
		"Returns the exact ordered collateral pool, without drawing randomness.");
	R.method<&Mechanics::getRecipientDivineRetributionPayoutBasisPoints>("getRecipientDivineRetributionPayoutBasisPoints",
		{{"recipient", "Protected recipient whose existing Arcane Marks apply."}}, {},
		"Returns the new-policy cast-time payout ratio, or nil to retain the historical tuple.");
	R.method<&Mechanics::getRecipientDivineRetributionCap>("getRecipientDivineRetributionCap",
		{{"recipient", "Protected recipient whose existing Arcane Marks apply."}}, {},
		"Returns the actual snapshotted judgment cap before Retributionist payout.");
	R.method<&Mechanics::getRecipientArcaneBreachMarkBasisPoints>("getRecipientArcaneBreachMarkBasisPoints",
		{{"target", "Friendly shooter whose existing Arcane Marks apply."}}, {},
		"Returns Focus Magic's actual cast-time mark potency, preserving the independent Arcane Breach cap.");
	R.method<&Mechanics::getRecipientHexOfPainDamageSharePercent>("getRecipientHexOfPainDamageSharePercent",
		{{"target", "Recipient whose existing Arcane Marks apply."}}, {},
		"Returns the final cast-time Hex damage-share snapshot, respecting its native percentage cap.");
	R.method<&Mechanics::getRecipientSoulChainEchoBasisPoints>("getRecipientSoulChainEchoBasisPoints",
		{{"target", "Secondary recipient whose existing Arcane Marks apply."}}, {},
		"Returns the cast-time Soul Chain echo snapshot, capped at its native supported maximum.");
	R.method<&Mechanics::getRecipientSpellLockDuration>("getRecipientSpellLockDuration",
		{{"target", "Recipient whose existing Arcane Marks apply."}}, {},
		"Returns Spell Lock's actual target-aware duration, including established extensions.");
	R.method<&Mechanics::getGuardianSpiritHitPoints>("getGuardianSpiritHitPoints",
		{{"target", "Recipient whose captured Crown and Altar component applies."}}, {},
		"Returns the native protective pool, followed by this recipient's existing Arcane Marks once.");
	R.method<&Mechanics::getPhantomArmyIntegrity>("getPhantomArmyIntegrity",
		{{"source", "Source whose current aggregate HP supplies the Integrity basis."}}, {},
		"Returns native source-derived Integrity with its base cap and Illusionist, then existing Arcane Marks once.");

}
}
