/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleSide.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <memory>
#include <vector>

class CBattleInfoCallback;
class CPlayerBattleCallback;
class CGHeroInstance;
class CSpell;
class DamageCache;
class HypotheticBattle;
class PossibleSpellcast;

/// Metamagic 6 (Double Cast) battle-AI decision. An open offer is evaluated by the
/// ordinary hero-spell evaluator; this unit only filters the illegal repeat, caps
/// the follow-up roster, and prices the completion perks the projection omits
/// (completion effects run only when the authority advances the use counters).
namespace doubleCastAI
{
/// Upper bound on follow-up spells sent to the ordinary evaluator per offer.
inline constexpr size_t MAX_FOLLOW_UP_SPELLS = 12;

class FollowUpFilter
{
	const CBattleInfoCallback * battle = nullptr;
	BattleSide side = BattleSide::NONE;
	SpellID firstSpell = SpellID::NONE;
	bool open = false;
	bool mustDiffer = false;
public:
	FollowUpFilter(const CBattleInfoCallback & battle, BattleSide side, const CGHeroInstance * hero);
	bool offerOpen() const { return open; }
	/// True when the spell would be paid by the open offer's grant.
	bool paidByOffer(SpellID spell) const;
	/// The authority refuses repeating the first spell with the offer's grant.
	bool refuses(SpellID spell) const;
	/// Offer-paid spells beyond MAX_FOLLOW_UP_SPELLS (ranked by highest level, then
	/// lowest id); spells paid by other allowances are never dropped.
	std::vector<SpellID> overCap(const std::vector<const CSpell *> & spells) const;
};

/// Rough per-follow-up value of the hero's owned completion perks: Arcane Ward
/// shield HP on friendly stacks still exposed this round, and Mana gained or denied
/// (Mana Siphon, Formula Reserve) priced at the best candidate's marginal value per
/// Mana. Afterglow and Cleansing Weave are unpriced. Never negative.
DLL_LINKAGE float completionValue(const CPlayerBattleCallback & battle, const std::shared_ptr<HypotheticBattle> & board,
	BattleSide side, const CGHeroInstance & hero, DamageCache & damage, float valuePerMana, float positive);
/// Adds completionValue to each evaluated candidate paid by the open offer.
DLL_LINKAGE void addCompletionValue(std::vector<PossibleSpellcast> & candidates, const CPlayerBattleCallback & battle,
	const std::shared_ptr<HypotheticBattle> & board, BattleSide side, float baseline, DamageCache & damage, float positive);
}
