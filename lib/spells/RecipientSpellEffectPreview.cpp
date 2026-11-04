/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RecipientSpellEffectPreview.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <limits>
#include "NewHorizonsTimedMagnitudes.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "NewHorizonsTimedStatusPlan.h"
#include "RecipientTimedSpellEffectPreview.h"
#include "../battle/Unit.h"

namespace newHorizonsMagic
{
namespace
{
RecipientSpellEffectPreview quantity(const std::string & label, int64_t value, int32_t scale = 1)
{
	return {{"new-horizons.spellbook.effect." + label,
		"new-horizons.spellbook.effect." + label + ".short",
		SpellEffectPreview::Quantity{value, scale, "new-horizons.spellbook.unit.number"}, {}, {}}, {}};
}
}

std::optional<int32_t> scriptedRoundStatusDuration(const spells::Mechanics & mechanics)
{
	if(!mechanics.battle() || !mechanics.battle()->getBattle())
		return std::nullopt;
	const auto * family = effectSpellForCast(mechanics);
	if(!family)
		return std::nullopt;
	const auto & key = family->getJsonKey();
	if(key == "new-horizons:hexOfPain" || key == "new-horizons:hydrasVitality")
		return mechanics.adjustEffectDuration(3);
	if(key == "new-horizons:soulChain")
		return mechanics.adjustEffectDuration(2);
	return std::nullopt;
}

std::optional<RecipientSpellEffectPreview> recipientSpellEffectPreview(
	const spells::Mechanics & mechanics, const battle::Unit * recipient)
{
	if(!mechanics.battle() || !mechanics.battle()->getBattle())
		return std::nullopt;
	const auto * family = effectSpellForCast(mechanics);
	if(!recipient || !family || !mechanics.usesNewHorizonsMagicV3())
		return std::nullopt;
	const auto & key = family->getJsonKey();
	std::optional<RecipientSpellEffectPreview> result;
	if(key == "new-horizons:focusMagic")
		result = quantity("focus", mechanics.getRecipientArcaneBreachMarkBasisPoints(recipient), 100);
	else if(key == "new-horizons:hexOfPain")
	{
		result = quantity("hex", mechanics.adjustMetamagicMagnitude(mechanics.getHexOfPainFlatDamage(), recipient));
		result->effect.secondary = SpellEffectPreview::Quantity{
			mechanics.getRecipientHexOfPainDamageSharePercent(recipient), 1, "new-horizons.spellbook.unit.number"};
		result->effect.qualificationKey = "new-horizons.spellbook.hexTrigger";
	}
	else if(key == "new-horizons:soulChain")
		result = quantity("soulChain", mechanics.getRecipientSoulChainEchoBasisPoints(recipient), 100);
	else if(key == "new-horizons:spellLock")
	{
		const auto rounds = mechanics.getRecipientSpellLockDuration(recipient);
		result = quantity("spellLock", rounds);
		if(rounds > 0)
			{
			Bonus incoming;
			incoming.type = BonusType::MAGIC_RESISTANCE;
			if(mechanics.battle()->battleGetOwner(recipient) != mechanics.getCasterColor())
				incoming.statusIdentity = key;
			result->durationRounds = refreshedTimedStatusDuration(mechanics, *recipient, incoming, rounds);
			result->effect.primary->value = *result->durationRounds;
		}
	}
	else if(key == "new-horizons:guardianSpirit")
		result = quantity("restorationPool", mechanics.getGuardianSpiritHitPoints(recipient));
	else if(key == "new-horizons:frailty")
		result = quantity("frailty", mechanics.getRecipientFrailtyPlan(recipient).totalBasisPoints, 100);
	else if(key == "new-horizons:hydrasVitality")
		result = quantity("hydra", mechanics.adjustMetamagicMagnitude(
			std::clamp<int64_t>(mechanics.getEffectValue(), 25'000'000, 50'000'000), recipient), 1'000'000);
	else if(key == "new-horizons:divineRetribution")
	{
		const auto * hero = mechanics.getHeroCaster();
		const int rider = hero && hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.retributionist") ? 120 : 100;
		result = quantity("divineReflection", divineRetributionPayout(std::numeric_limits<int64_t>::max(),
			mechanics.getRecipientDivineRetributionCap(recipient), rider,
			mechanics.getRecipientDivineRetributionPayoutBasisPoints(recipient).value_or(3000)));
	}
	if(result)
		if(const auto ordinary = scriptedRoundStatusDuration(mechanics))
		{
			const auto rounds = mechanics.adjustMetamagicDuration(*ordinary, recipient);
			if(rounds > 0)
			{
				if(key == "new-horizons:hydrasVitality")
				{
					Bonus incoming;
					incoming.type = BonusType::STACK_HEALTH;
					incoming.valType = BonusValueType::INDEPENDENT_MAX;
					result->durationRounds = refreshedTimedStatusDuration(mechanics, *recipient, incoming, rounds);
				}
				else
					result->durationRounds = rounds; // Hex and Soul explicitly remove old local status first.
			}
		}
	return result ? result : recipientTimedSpellEffectPreview(mechanics, *recipient);
}
}
