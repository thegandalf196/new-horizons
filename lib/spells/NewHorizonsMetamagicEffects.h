/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsMetamagicMarks.h"
#include "../constants/EntityIdentifiers.h"
#include <span>
#include <set>

class CSpell;
namespace spells { class Caster; }
class IBattleInfo;
class CBattleInfoCallback;
class ServerCallback;
struct SetStackEffect;
struct Bonus;
namespace battle { class Unit; }

namespace newHorizonsMetamagic
{
DLL_LINKAGE bool isNumericalSpellBonus(const Bonus & bonus, SpellID acceptedSpell, SpellID effectSpell);
DLL_LINKAGE CastProfile captureHeroCastProfile(const CGHeroInstance * hero,
	const IBattleInfo * battle, bool genuineHeroCast, SpellID spell = SpellID());
DLL_LINKAGE CastProfile captureDelayedCastProfile(const spells::Caster * caster);
DLL_LINKAGE int32_t targetFreeEffectivenessBonusPercent(const CastProfile * cast);
DLL_LINKAGE int32_t recipientBonusPercent(const CastProfile * cast, const battle::Unit * recipient);
DLL_LINKAGE int32_t outgoingDuration(int32_t ordinary, const CastProfile * cast,
	const battle::Unit * recipient);
DLL_LINKAGE int64_t outgoingMagnitude(int64_t ordinary, const CastProfile * cast,
	const battle::Unit * recipient);
/// Capture offensive damage, rather than a status coefficient, exactly once.
DLL_LINKAGE int64_t outgoingDamageMagnitude(int64_t ordinary, const CastProfile * cast,
	const battle::Unit * recipient);
/// Deferred damage never re-reads current hero perks or repeats cast-time scaling.
DLL_LINKAGE CastProfile capturedOffensiveProfile(const JsonNode * captured);
DLL_LINKAGE int64_t deferredSpellDamage(int64_t ordinary, const JsonNode * captured,
	const CBattleInfoCallback & battle, const battle::Unit * recipient, PlayerColor spellOwner);
DLL_LINKAGE int64_t factionSpellDamagePreview(int64_t ordinary,
	const CGHeroInstance & hero, const CSpell & spell, const JsonNode & rules);
DLL_LINKAGE int64_t spellDamage(int64_t ordinary, const CastProfile * cast,
	const CBattleInfoCallback & battle, const battle::Unit * recipient, PlayerColor spellOwner, bool magicalDamage = true);
DLL_LINKAGE int32_t resistanceChance(int32_t ordinary, const CastProfile * cast,
	const CBattleInfoCallback & battle, const battle::Unit & recipient);
/// Only this cast's new/updated spell bonuses are transformed. Removed bonuses,
/// categorical abilities and non-round lifetime markers remain ordinary.
DLL_LINKAGE void adjustAppliedBonuses(const CastProfile * cast, const CBattleInfoCallback & battle,
	SpellID acceptedSpell, SpellID effectSpell, const std::set<uint32_t> & preexistingRecipientIds,
	SetStackEffect & packet);
/// An inexpensive lower bound for selecting a spell, not permission to cast it.
/// The native target-aware affordability/legality check remains authoritative.
DLL_LINKAGE int minimumSelectionCost(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback * battle, int ordinaryCost);
struct ManaTargetCounts
{
	uint32_t qualifying = 0;
	uint32_t affected = 0;
};
/// Deduplicated prospective legal recipients; no resistance rolls or state changes.
DLL_LINKAGE ManaTargetCounts manaTargetCounts(const CastProfile * cast,
	const CBattleInfoCallback & battle, std::span<const uint32_t> prospectiveRecipientIds);
DLL_LINKAGE int32_t manaCostForRecipients(int32_t ordinary, const CastProfile * cast,
	const CBattleInfoCallback & battle, std::span<const uint32_t> prospectiveRecipientIds);
/// Existing authoritative packet path; acquired copies only, never live mutation.
DLL_LINKAGE void publishResolvedMarks(ServerCallback & server, const CastProfile * cast,
	const CBattleInfoCallback & battle, std::span<const uint32_t> resolvedRecipientIds);
}
