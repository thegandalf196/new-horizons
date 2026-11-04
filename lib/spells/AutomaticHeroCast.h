/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../Global.h"
#include "../battle/BattleSide.h"
#include "../constants/EntityIdentifiers.h"

#include <cstdint>

class CBattleInfoCallback;
class CGHeroInstance;
class CSpell;
enum class HeroCommand : int8_t;

namespace spells
{
/// Server-created spell provenance, never a client BattleAction option. These
/// casts retain HERO spell mechanics while paying no Mana or action allowance.
enum class AutomaticHeroCastKind : uint8_t
{
	NONE,
	DIVINE_MANDATE,
	DIVINE_FURY
};

struct DLL_LINKAGE AutomaticHeroCastReceipt
{
	AutomaticHeroCastKind kind = AutomaticHeroCastKind::NONE;
	uint8_t mandateUseOrdinal = 0;
	HeroCommand issuingOrder = static_cast<HeroCommand>(0);
	int32_t issuingRound = 0;
	uint8_t inspiringDuration = 0;
	bool automatic() const { return kind != AutomaticHeroCastKind::NONE; }
	void validateShape() const;
	void validateEnvelope(bool castByHero, bool activeCast, int32_t casterStack,
		BattleSide side, int32_t paidHeroManaCost) const;
	bool operator==(const AutomaticHeroCastReceipt &) const = default;
};

/// Common native/projection admission. No RNG, resource mutation, effect
/// application or action search. Target legality is still native mechanics.
DLL_LINKAGE bool automaticHeroCastAllowed(const CBattleInfoCallback & battle,
	const CGHeroInstance * hero, const CSpell * spell, AutomaticHeroCastReceipt receipt);
}
