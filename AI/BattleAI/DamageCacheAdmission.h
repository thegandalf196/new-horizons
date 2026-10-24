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

namespace battle { class Unit; }
class CBattleInfoCallback;
class CGHeroInstance;

/// Lazy actor and callback-only admission facts for ONE immutable main-target probe batch.
/// Not a damage cache or mutation-aware cache. End before any applied action,
/// activation, actor replacement or branch mutation. Never pass to reverse DPS,
/// detached strike actors or reaction children. Queries retain first-use order.
class DamageCacheAdmission
{
public:
	DamageCacheAdmission(const battle::Unit & actor, const CBattleInfoCallback & battle)
		: actor(actor), battle(battle) {}
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

private:
	enum Fact : std::size_t { AMBUSHER, SHADOW, NIGHT, NIGHT_EFFECT, RELENTLESS, GAMBLER,
		REDUCED_ACTIVATION, BLOODRAGE, FOCUS_FIRE_RULES, COUNT };
	const battle::Unit & actor;
	const CBattleInfoCallback & battle;
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
