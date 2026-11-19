/*
 * DamageCacheAdmission.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/battle/BattleSide.h"
#include <array>
#include <cstddef>
#include <optional>
#include <memory>
#include <utility>

namespace battle { class Unit; }
class CBattleInfoCallback;
class CGHeroInstance;
class FlatDamageInputs;

/// A lazy whole-roster fact for one explicitly immutable observation interval.
/// Supply a non-null callback. Construct after unit materialization; destroy before
/// any unit, bonus, hero, ownership, rules or branch mutation, including shared
/// native ancestors. Queries
/// must be read-only; custom callbacks/scripts with mutable queries use no scope.
/// This is deliberately not a version key.
/// The strong owner preserves callback identity; mismatching callbacks fall back.
class OrdinaryDamageObservation
{
public:
	explicit OrdinaryDamageObservation(std::shared_ptr<CBattleInfoCallback> source)
		: source(std::move(source)) {}
	OrdinaryDamageObservation(const OrdinaryDamageObservation &) = delete;
	OrdinaryDamageObservation & operator=(const OrdinaryDamageObservation &) = delete;
	bool matches(const CBattleInfoCallback & candidate) const;
	bool ordinaryCreatureDamageIsStateful();

private:
	const std::shared_ptr<CBattleInfoCallback> source;
	std::optional<bool> stateful;
};

/// Lazy actor and callback-only admission facts for ONE immutable main-target probe batch.
/// Not a damage cache or mutation-aware cache. End before any applied action,
/// activation, actor replacement or branch mutation. Never pass to reverse DPS,
/// detached strike actors or reaction children. Queries retain first-use order.
class DamageCacheAdmission
{
public:
	DamageCacheAdmission(const battle::Unit & actor, const CBattleInfoCallback & battle,
		OrdinaryDamageObservation * observation = nullptr)
		: actor(actor), battle(battle), observation(observation) {}
	DamageCacheAdmission(const DamageCacheAdmission &) = delete;
	DamageCacheAdmission & operator=(const DamageCacheAdmission &) = delete;
	bool matches(const battle::Unit & candidate, const CBattleInfoCallback & branch) const;
	BattleSide side();
	bool hasAmbusher();
	bool hasShadowAssault();
	bool hasNightProwler();
	bool hasNightProwlerEffect();
	bool hasRelentlessAssault();
	bool hasGamblerPenalty();
	bool hasReducedExtraActivation();
	bool hasBloodragePain();
	bool supportsFocusFire();
	bool ordinaryCreatureDamageIsStateful();
	/// Borrowed only by this immutable selector batch; never an applied hit.
	const FlatDamageInputs * flatInputs = nullptr;

private:
	enum Fact : std::size_t { AMBUSHER, SHADOW, NIGHT, NIGHT_EFFECT, RELENTLESS, GAMBLER,
		REDUCED_ACTIVATION, BLOODRAGE, FOCUS_FIRE_RULES, ORDINARY_DAMAGE_POLICY, COUNT };
	const battle::Unit & actor;
	const CBattleInfoCallback & battle;
	OrdinaryDamageObservation * observation;
	std::optional<BattleSide> actorSide;
	std::optional<const CGHeroInstance *> actorHero;
	std::array<std::optional<bool>, COUNT> facts{};
	const CGHeroInstance * hero();
	template<class Query> bool remember(Fact key, Query && query)
	{
		auto & value = facts[key];
		if(!value.has_value())
			value = query(); // Throwing queries leave this slot uninitialized.
		return *value;
	}
};
