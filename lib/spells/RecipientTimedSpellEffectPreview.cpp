/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RecipientTimedSpellEffectPreview.h"
#include "NewHorizonsTimedStatusPlan.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../networkPacks/SetStackEffect.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace newHorizonsMagic
{
namespace
{

RecipientSpellEffectPreview transformedPreview(const spells::Mechanics & m, const battle::Unit & recipient,
	Bonus incoming, int32_t rounds, const std::string & label, int scale, bool valueIsDuration)
{
	const auto family = effectSpellForCast(m)->getId();
	incoming.source = BonusSource::SPELL_EFFECT;
	incoming.sid = BonusSourceID(family);
	incoming.turnsRemain = static_cast<int16_t>(rounds); // Same typed producer as Lua's Bonus conversion.
	SetStackEffect packet;
	packet.battleID = m.battle()->getBattle()->getBattleID();
	packet.toAdd.emplace_back(recipient.unitId(), std::vector<Bonus>{incoming});
	newHorizonsMetamagic::adjustAppliedBonuses(m.getMetamagicCastProfile(), *m.battle(),
		m.getSpellId(), family, {recipient.unitId()}, packet);
	const auto & adjusted = packet.toAdd.front().second.front();
	const auto duration = refreshedTimedStatusDuration(m, recipient, adjusted, adjusted.turnsRemain);
	RecipientSpellEffectPreview result;
	result.effect.labelKey = "new-horizons.spellbook.effect." + label;
	result.effect.compactLabelKey = result.effect.labelKey + ".short";
	result.effect.primary = SpellEffectPreview::Quantity{valueIsDuration ? duration : adjusted.val,
		scale, "new-horizons.spellbook.unit.number"};
	if(duration > 0)
		result.durationRounds = duration;
	return result;
}
}

std::optional<RecipientSpellEffectPreview> recipientTimedSpellEffectPreview(
	const spells::Mechanics & m, const battle::Unit & recipient)
{
	const auto * family = effectSpellForCast(m);
	const auto * actual = m.getSpellId().toSpell();
	if(!family || !actual || !m.battle() || !m.battle()->getBattle() || !m.usesNewHorizonsMagicV3())
		return std::nullopt;
	const auto & key = family->getJsonKey();
	struct Family
	{
		std::string_view key;
		BonusType type;
		std::string_view label;
		int scale;
		bool durationOnly;
	};
	static constexpr std::array families{
		Family{"new-horizons:holyArmor", BonusType::SPELL_DAMAGE_REDUCTION, "holyArmor", 1, false},
		Family{"new-horizons:heavenlyGale", BonusType::HEAVENLY_GALE, "heavenlyGale", 100, false},
		Family{"new-horizons:shieldOfChaos", BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS, "chaosShield", 100, false},
		Family{"new-horizons:entangle", BonusType::BIND_EFFECT, "entangle", 1, true},
		Family{"core:misfortune", BonusType::LUCK, "misfortune", 100, false},
		Family{"core:forgetfulness", BonusType::FORGETFULL, "forgetfulness", 1, true},
		Family{"core:bless", BonusType::ALWAYS_MAXIMUM_DAMAGE, "bless", 1, true}
	};
	const auto selected = std::ranges::find(families, key, &Family::key);
	if(selected == families.end())
		return std::nullopt;
	const std::string label(selected->label);
	auto incoming = configuredRoundBonusIdentity(*actual, m.getEffectLevel(), selected->type);
	if(!incoming)
		return std::nullopt;
	int32_t rounds = configuredTimedDurationForCast(m, incoming->turnsRemain, m.getEffectDuration(), true);
	if(label == "holyArmor")
		incoming->val = holyArmorReductionForRecipient(m, incoming->val, recipient);
	else if(label == "heavenlyGale")
		incoming->val = heavenlyGaleReductionForRecipient(m, recipient);
	else if(label == "chaosShield")
	{
		incoming->val = chaosShieldReductionForCast(m); // Native producer overwrites specialty-adjusted val.
		rounds = m.adjustEffectDuration(2);
	}
	else if(label == "entangle")
		rounds = entangleDurationForCast(m);
	else if(label == "forgetfulness")
	{
		incoming->val = 100;
		rounds = forgetfulnessDurationForCast(m);
	}
	else if(label == "misfortune")
	{
		incoming->type = BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS;
		incoming->valType = BonusValueType::INDEPENDENT_MIN;
		incoming->statusIdentity = "core:misfortune";
		incoming->val = static_cast<int32_t>(misfortuneValueForRecipient(m, recipient, misfortuneChanceForCast(m)));
		rounds = misfortuneDurationForCast(m);
	}
	auto result = transformedPreview(m, recipient, *incoming, rounds, label, selected->scale, selected->durationOnly);
	if(label == "chaosShield")
		result.effect.qualificationKey = "new-horizons.spellbook.chaosShieldCap";
	if(label == "misfortune")
	{
		result.effect.secondary = SpellEffectPreview::Quantity{result.durationRounds.value_or(0), 1,
			"new-horizons.spellbook.unit.number"};
		result.effect.qualificationKey = "new-horizons.spellbook.misfortuneChance";
	}
	return result;
}
}
