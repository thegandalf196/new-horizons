/*
 * NewHorizonsRealityWarp.cpp, part of VCMI / New Horizons
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "NewHorizonsRealityWarp.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../battle/BattleForm.h"
#include "../battle/NewHorizonsConfusionControl.h"
#include "../battle/NewHorizonsPuppetMaster.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusSelector.h"

namespace newHorizonsRealityWarp
{
namespace
{
EffectBundle copyForRecipient(const EffectBundle & source, PlayerColor recipientOwner)
{
	auto transferred = source;
	for(auto & bonus : transferred.bonuses)
		if(bonus.spellCasterOwner != PlayerColor::CANNOT_DETERMINE
			&& recipientOwner != PlayerColor::CANNOT_DETERMINE)
			bonus.appliedByEnemy = bonus.spellCasterOwner != recipientOwner;
	return transferred;
}
}

ExchangePlan planExchange(const std::vector<EffectBundle> & firstBundles, PlayerColor firstOwner,
	const std::vector<EffectBundle> & secondBundles, PlayerColor secondOwner,
	const RecipientLegality & recipientAllows)
{
	ExchangePlan result;
	result.first.reserve(firstBundles.size() + secondBundles.size());
	result.second.reserve(firstBundles.size() + secondBundles.size());

	std::vector<EffectBundle> movingToFirst;
	std::vector<EffectBundle> movingToSecond;
	movingToFirst.reserve(secondBundles.size());
	movingToSecond.reserve(firstBundles.size());

	const auto route = [&recipientAllows](const std::vector<EffectBundle> & sourceBundles,
		RecipientSide destinationSide, PlayerColor destinationOwner,
		std::vector<EffectBundle> & stay, std::vector<EffectBundle> & moving)
	{
		for(const auto & bundle : sourceBundles)
		{
			if(!bundle.transferable || !recipientAllows
				|| !recipientAllows(bundle, destinationSide, destinationOwner))
			{
				stay.push_back(bundle);
				continue;
			}

			moving.push_back(copyForRecipient(bundle, destinationOwner));
		}
	};

	// Decide both directions from the original snapshots before assembling either
	// result, so a transferred bundle cannot affect legality in the other direction.
	route(firstBundles, RecipientSide::SECOND, secondOwner, result.first, movingToSecond);
	route(secondBundles, RecipientSide::FIRST, firstOwner, result.second, movingToFirst);

	result.first.insert(result.first.end(), movingToFirst.begin(), movingToFirst.end());
	result.second.insert(result.second.end(), movingToSecond.begin(), movingToSecond.end());
	return result;
}

namespace
{
enum SidecarKind : unsigned { REGENERATION = 1, GUARDIAN = 2, CAPACITY = 4, CONFUSION = 8 };
constexpr uint16_t temporaryBattleDurationMask = BonusDuration::ONE_BATTLE | BonusDuration::N_TURNS
	| BonusDuration::UNTIL_BEING_ATTACKED | BonusDuration::UNTIL_ATTACK | BonusDuration::STACK_GETS_TURN
	| BonusDuration::COMMANDER_KILLED | BonusDuration::UNTIL_OWN_ATTACK | BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE
	| BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE | BonusDuration::STACK_ACTIVATION;
struct Collected
{
	EffectBundle bundle;
	StayReason reason = StayReason::MOVED;
	unsigned sidecars = 0;
};

const CSpell * sourceSpell(SpellID spell)
{
	if(!spell.hasValue())
		return nullptr;
	try { return spell.toSpell(); }
	catch(const std::exception &) { return nullptr; }
}

std::optional<JsonNode> parameters(const Bonus & bonus)
{
	if(!bonus.parameters)
		return std::nullopt;
	try
	{
		const auto result = bonus.parameters->toCustom<JsonNode>();
		if(result.isStruct())
			return result;
	}
	catch(const std::exception &) {}
	return std::nullopt;
}

std::vector<Collected> collect(const battle::BattleEffectSnapshot & snapshot, const JsonNode & rules)
{
	std::vector<Collected> result;
	for(const auto & bonus : snapshot.effects)
	{
		const auto spell = bonus.sid.as<SpellID>();
		auto found = std::find_if(result.begin(), result.end(), [&](const Collected & group)
		{
			const auto & first = group.bundle.bonuses.front();
			return first.sid == bonus.sid && first.statusIdentity == bonus.statusIdentity
				&& first.spellCasterOwner == bonus.spellCasterOwner;
		});
		if(found == result.end())
		{
			result.emplace_back();
			found = std::prev(result.end());
			found->bundle.spell = spell;
		}
		found->bundle.bonuses.push_back(bonus);
	}
	for(auto & group : result)
	{
		for(const auto & bonus : group.bundle.bonuses)
		{
			if(bonus.source != BonusSource::SPELL_EFFECT || (bonus.duration & temporaryBattleDurationMask) == 0
				|| vstd::contains(bonus.statusTags, BonusStatusTag::NON_TRANSFERABLE)
				|| battle::isPolymorphMarker(&bonus) || bonus.type == BonusType::PHYSICAL_AFFLICTION)
				group.reason = StayReason::EXCLUDED_EFFECT;
			if(bonus.type == BonusType::GUARDIAN_SPIRIT)
			{
				group.sidecars |= GUARDIAN;
				group.bundle.guardianSpirit = GuardianSpiritPayload{snapshot.sidecars.guardianSpiritHitPoints,
					snapshot.sidecars.guardianSpiritRoundsRemaining};
			}
			if(bonus.type == BonusType::CONFUSION_PENDING)
			{
				group.sidecars |= CONFUSION;
				group.bundle.confusion = ConfusionPayload{snapshot.sidecars.confusionCaster, snapshot.sidecars.confusionConfounder};
				if(!newHorizonsConfusionControl::isPendingMarker(&bonus) || !snapshot.sidecars.confusionPending
					|| bonus.spellCasterOwner != snapshot.sidecars.confusionCaster
					|| (bonus.val == 2) != snapshot.sidecars.confusionConfounder)
					group.reason = StayReason::INVALID_SIDECAR;
			}
		}
		const auto spell = group.bundle.spell;
		const auto * definition = sourceSpell(spell);
		if(!definition)
		{
			group.reason = StayReason::UNKNOWN_SOURCE;
			continue;
		}
		if(!definition->isMagical() || definition->getJsonKey() == "new-horizons:polymorph")
			group.reason = StayReason::EXCLUDED_EFFECT;
		const auto family = newHorizonsMagic::spellVariantBase(rules, spell);
		const auto familyKey = family.toSpell()->getJsonKey();
		if(familyKey == "new-horizons:regeneration")
		{
			group.sidecars |= REGENERATION;
			group.bundle.regeneration = RegenerationPayload{snapshot.sidecars.regenerationRateMillionths,
				snapshot.sidecars.regenerationPendingMicroHealth};
		}
		if(familyKey == "new-horizons:hydrasVitality")
		{
			group.sidecars |= CAPACITY;
			group.bundle.capacityRegeneration = CapacityRegenerationPayload{snapshot.sidecars.capacityRegenerationRemainderTenths};
		}
	}
	// A single unit sidecar cannot identify multiple independently sourced pools.
	for(const unsigned kind : {REGENERATION, GUARDIAN, CAPACITY, CONFUSION})
		if(std::count_if(result.begin(), result.end(), [kind](const Collected & group) { return (group.sidecars & kind) != 0; }) > 1)
			for(auto & group : result)
				if(group.sidecars & kind)
					group.reason = StayReason::SIDECAR_CONFLICT;
	return result;
}

StayReason recipientReason(const CBattleInfoCallback & callback, const JsonNode & rules, Collected & group,
	const battle::Unit & recipient, const battle::BattleEffectSnapshot & destination)
{
	if(group.reason != StayReason::MOVED)
		return group.reason;
	const auto * source = group.bundle.spell.toSpell();
	if(source->isNegative() && recipient.isInvincible())
		return StayReason::ILLEGAL_RECIPIENT;
	const auto family = newHorizonsMagic::spellVariantBase(rules, group.bundle.spell);
	const auto familyKey = family.toSpell()->getJsonKey();
	const auto caster = group.bundle.bonuses.front().spellCasterOwner;
	std::optional<bool> opposition;
	if(caster != PlayerColor::CANNOT_DETERMINE)
		opposition = caster != callback.battleGetOwner(&recipient);
	std::optional<int64_t> maximumHealth;
	for(const auto & bonus : group.bundle.bonuses)
	{
		if(bonus.propagator)
			return StayReason::UNSUPPORTED_CONDITION;
		if(bonus.type == BonusType::STACK_HEALTH
			&& (bonus.updater || bonus.propagationUpdater || bonus.propagator || bonus.limiter))
			return StayReason::UNSUPPORTED_CONDITION;
		if(bonus.type == BonusType::HYPNOTIZED)
		{
			if(caster == PlayerColor::CANNOT_DETERMINE)
				return StayReason::MISSING_CAPTURE;
			const auto captured = parameters(bonus);
			if(!captured || (*captured)["maximumTargetHealth"].getType() != JsonNode::JsonType::DATA_INTEGER
				|| (*captured)["maximumTargetHealth"].Integer() < 0)
				return StayReason::MISSING_CAPTURE;
			const auto ceiling = (*captured)["maximumTargetHealth"].Integer();
			if(maximumHealth && *maximumHealth != ceiling)
				return StayReason::MISSING_CAPTURE;
			maximumHealth = ceiling;
		}
		if(bonus.type == BonusType::PUPPET_MASTER_CONTROL
			&& !newHorizonsPuppetMaster::isValidControlMarker(callback, &recipient, &bonus))
			return caster == PlayerColor::CANNOT_DETERMINE ? StayReason::MISSING_CAPTURE : StayReason::ILLEGAL_RECIPIENT;
	}
	if(group.sidecars & (REGENERATION | CAPACITY))
	{
		if(!recipient.hasBonusOfType(BonusType::LIVING) || recipient.hasBonusOfType(BonusType::MECHANICAL) || recipient.isClone()
			|| recipient.getPhantomInitialIntegrity() > 0 || recipient.hasBonusOfType(BonusType::SIEGE_WEAPON))
			return StayReason::ILLEGAL_RECIPIENT;
		if((group.sidecars & CAPACITY) && destination.capacityHealthReferenceMax <= 0)
			return StayReason::INVALID_SIDECAR;
	}
	if(familyKey == "core:hypnotize" || familyKey == "new-horizons:puppetMaster"
		|| familyKey == "new-horizons:confusion" || familyKey == "core:berserk")
		if(recipient.hasBonusOfType(BonusType::LUCIDITY))
			return StayReason::ILLEGAL_RECIPIENT;
	for(const auto & bonus : group.bundle.bonuses)
	{
		if(familyKey == "new-horizons:soulChain")
		{
			const auto captured = parameters(bonus);
			if(!captured || !(*captured)["primaryUnitId"].isNumber())
				return StayReason::MISSING_CAPTURE;
			// Lua captures numbers as JSON floats; accept only exact integer IDs.
			const auto id = (*captured)["primaryUnitId"].Float();
			if(!std::isfinite(id) || id < 0 || id > std::numeric_limits<uint32_t>::max() || std::floor(id) != id)
				return StayReason::INVALID_LINK;
			const auto * primary = callback.battleGetUnitByID(static_cast<uint32_t>(id));
			if(!primary || !primary->alive() || !primary->isValidTarget(false) || primary->unitId() == recipient.unitId())
				return StayReason::INVALID_LINK;
		}
		if(familyKey == "new-horizons:focusMagic" || familyKey == "new-horizons:arcaneBreach")
		{
			const auto captured = parameters(bonus);
			if(!captured || !(*captured)["beneficiarySide"].isNumber()
				|| ((*captured)["beneficiarySide"].Float() != static_cast<int>(BattleSide::ATTACKER)
					&& (*captured)["beneficiarySide"].Float() != static_cast<int>(BattleSide::DEFENDER)))
				return StayReason::MISSING_CAPTURE;
			if(familyKey == "new-horizons:focusMagic" && !recipient.isShooter())
				return StayReason::ILLEGAL_RECIPIENT;
		}
	}
	const spells::RecipientConditionContext context{source, family, newHorizonsMagic::spellLevel(rules, group.bundle.spell),
		rules["rulesetVersion"].Integer() == newHorizonsMagic::CURRENT_RULESET_VERSION, opposition, maximumHealth};
	switch(source->checkRecipient(context, &recipient))
	{
		case spells::RecipientConditionResult::LEGAL: return StayReason::MOVED;
		case spells::RecipientConditionResult::ILLEGAL: return StayReason::ILLEGAL_RECIPIENT;
		case spells::RecipientConditionResult::MISSING_CASTER_PROVENANCE:
		case spells::RecipientConditionResult::MISSING_HEALTH_CAPTURE: return StayReason::MISSING_CAPTURE;
		case spells::RecipientConditionResult::UNSUPPORTED_CONDITION: return StayReason::UNSUPPORTED_CONDITION;
		case spells::RecipientConditionResult::INVALID_CONTEXT: return StayReason::UNKNOWN_SOURCE;
	}
	return StayReason::UNSUPPORTED_CONDITION;
}

void copySidecars(battle::BattleEffectSidecars & target, const battle::BattleEffectSidecars & source, unsigned mask)
{
	if(mask & REGENERATION)
	{
		target.regenerationRateMillionths = source.regenerationRateMillionths;
		target.regenerationPendingMicroHealth = source.regenerationPendingMicroHealth;
	}
	if(mask & GUARDIAN)
	{
		target.guardianSpiritHitPoints = source.guardianSpiritHitPoints;
		target.guardianSpiritRoundsRemaining = source.guardianSpiritRoundsRemaining;
	}
	if(mask & CAPACITY)
		target.capacityRegenerationRemainderTenths = source.capacityRegenerationRemainderTenths;
	if(mask & CONFUSION)
	{
		target.confusionPending = source.confusionPending;
		target.confusionCaster = source.confusionCaster;
		target.confusionConfounder = source.confusionConfounder;
	}
}
}

PreparedExchange prepareExchange(const CBattleInfoCallback & callback, uint32_t first, uint32_t second)
{
	PreparedExchange result;
	const std::array<uint32_t, 2> ids{first, second};
	std::array<const battle::Unit *, 2> units;
	for(size_t side = 0; side < ids.size(); ++side)
	{
		units[side] = callback.battleGetUnitByID(ids[side]);
		if(first == second || !units[side] || !units[side]->alive()
			|| units[side]->isTurret() || !units[side]->getPosition().isValid())
		{
			result.rejection = EndpointRejection::INVALID_ENDPOINT;
			return result;
		}
		if(units[side]->isTimeStopped())
		{
			result.rejection = EndpointRejection::TIME_STOPPED;
			return result;
		}
		// Generic target validity excludes Time Stop too. Preserve its explicit
		// endpoint rejection instead of masking it as an invalid battlefield unit.
		if(!units[side]->isValidTarget(false))
		{
			result.rejection = EndpointRejection::INVALID_ENDPOINT;
			return result;
		}
		const SpellID lock(SpellID::decode("new-horizons:spellLock"));
		if(units[side]->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(lock))))
		{
			result.rejection = EndpointRejection::SPELL_LOCKED;
			return result;
		}
	}
	const auto * state = dynamic_cast<const IBattleState *>(callback.getBattle());
	if(!state)
	{
		result.rejection = EndpointRejection::SNAPSHOT_UNAVAILABLE;
		return result;
	}
	battle::BattleEffectExchange exchange;
	std::array<std::vector<Collected>, 2> groups;
	try
	{
		for(size_t side = 0; side < ids.size(); ++side)
		{
			auto & endpoint = exchange.endpoints[side];
			endpoint.id = ids[side];
			endpoint.expected = state->captureBattleEffects(ids[side]);
			endpoint.replacement = endpoint.expected;
			groups[side] = collect(endpoint.expected, state->getMagicRules());
		}
	}
	catch(const std::exception &)
	{
		result.rejection = EndpointRejection::SNAPSHOT_UNAVAILABLE;
		return result;
	}
	for(size_t side = 0; side < ids.size(); ++side)
		for(auto & group : groups[side])
		{
			group.reason = recipientReason(callback, state->getMagicRules(), group, *units[1 - side], exchange.endpoints[1 - side].expected);
			group.bundle.transferable = group.reason == StayReason::MOVED;
		}
	// Resolve representability against the final simultaneous plan. A held pool
	// cannot be overwritten by an incoming pool; propagate resulting holds.
	bool changed;
	do
	{
		changed = false;
		for(size_t side = 0; side < ids.size(); ++side)
			for(auto & incoming : groups[1 - side])
				if(incoming.reason == StayReason::MOVED)
					for(const auto & retained : groups[side])
						if(retained.reason != StayReason::MOVED && (incoming.sidecars & retained.sidecars))
						{
							incoming.reason = StayReason::SIDECAR_CONFLICT;
							changed = true;
							break;
						}
	} while(changed);
	for(size_t side = 0; side < ids.size(); ++side)
	{
		auto & replacement = exchange.endpoints[side].replacement;
		replacement.effects.clear();
		for(const auto & group : groups[side])
			if(group.reason == StayReason::MOVED)
				copySidecars(replacement.sidecars, {}, group.sidecars);
		// Keep the original order of retained opaque components, including
		// interleaved groups and duplicates, rather than flattening group order.
		for(const auto & bonus : exchange.endpoints[side].expected.effects)
			for(const auto & group : groups[side])
			{
				const auto & firstBonus = group.bundle.bonuses.front();
				if(firstBonus.sid == bonus.sid && firstBonus.statusIdentity == bonus.statusIdentity
					&& firstBonus.spellCasterOwner == bonus.spellCasterOwner)
				{
					if(group.reason != StayReason::MOVED)
						replacement.effects.push_back(bonus);
					break;
				}
			}
		for(const auto & original : exchange.endpoints[1 - side].expected.effects)
			for(const auto & group : groups[1 - side])
			{
				const auto & firstBonus = group.bundle.bonuses.front();
				if(firstBonus.sid != original.sid || firstBonus.statusIdentity != original.statusIdentity
					|| firstBonus.spellCasterOwner != original.spellCasterOwner)
					continue;
				if(group.reason == StayReason::MOVED)
				{
					auto bonus = original;
					const auto owner = callback.battleGetOwner(units[side]);
					if(bonus.spellCasterOwner != PlayerColor::CANNOT_DETERMINE && owner != PlayerColor::CANNOT_DETERMINE)
						bonus.appliedByEnemy = bonus.spellCasterOwner != owner;
					const auto key = group.bundle.spell.toSpell()->getJsonKey();
					if(key == "new-horizons:focusMagic" || key == "new-horizons:arcaneBreach")
					{
						auto captured = *parameters(bonus);
						auto controller = callback.playerToSide(callback.battleGetOwner(units[side]));
						if(key == "new-horizons:arcaneBreach")
							controller = callback.otherSide(controller);
						captured["beneficiarySide"].Integer() = static_cast<int>(controller);
						bonus.parameters = std::make_shared<BonusParameters>(captured);
					}
					replacement.effects.push_back(std::move(bonus));
				}
				break;
			}
		for(const auto & group : groups[1 - side])
			if(group.reason == StayReason::MOVED)
				copySidecars(replacement.sidecars, exchange.endpoints[1 - side].expected.sidecars, group.sidecars);
		for(auto & group : groups[side])
		{
			group.bundle.transferable = group.reason == StayReason::MOVED;
			result.previews.push_back({ids[side], ids[1 - side], group.bundle, group.reason});
		}
	}
	exchange.validateShape();
	result.exchange = std::move(exchange);
	return result;
}
}
