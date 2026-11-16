/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsPerkState.h"

namespace newHorizonsHeroes
{
/// Scoped operation over an unchanged captured rules object. Catalogue admission
/// happens exactly once on entry; mutable choices are private until commit.
/// Never retain this borrowed operation in hero state or across callbacks that
/// can change its source rules. No gameplay state or RNG is stored in the view.
class DLL_LINKAGE PerkSelectionOperation
{
	const PerkState & source;
	ValidatedPerkRules catalog;
	std::vector<PerkSelection> working;
	bool committed = false;
public:
	explicit PerkSelectionOperation(const PerkState & source);
	PerkSelectionOperation(PerkState &&) = delete;
	PerkSelectionOperation(const PerkState &&) = delete;
	PerkSelectionOperation(const PerkSelectionOperation &) = delete;
	PerkSelectionOperation & operator=(const PerkSelectionOperation &) = delete;
	const ValidatedPerkRules & definitions() const { return catalog; }
	const std::vector<PerkSelection> & selections() const { return working; }
	bool hasTier(const std::string & skill, int tier) const;
	bool canAcquire(const std::string & skill, const std::string & perk, int rank) const;
	std::vector<PerkOfferCandidate> candidates(const std::string & skill, int tier) const;
	std::vector<PerkOfferCandidate> eligibleCandidates(const std::string & skill, int rank) const;
	std::vector<PerkOfferCandidate> next(const std::function<int(const std::string &)> & rank) const;
	void select(const std::string & skill, const std::string & perk, int rank);
	void commit(PerkState & destination);
};
}
