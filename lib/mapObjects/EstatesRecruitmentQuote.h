/*
 * Part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once

#include "../ResourceSet.h"
#include "../gameState/EstatesState.h"

class CGHeroInstance;

namespace newHorizonsEstates
{
struct DLL_LINKAGE RecruitmentQuote
{
	ResourceSet cost;
	ConscriptionLedger before{};
	ConscriptionLedger after{};
	bool discounted = false;
};

/// A hero specialty discount added to Conscription: permille of each resource's base
/// price, rounded down, with a cap on the combined discount.
struct DLL_LINKAGE RecruitmentSpecialtyDiscount
{
	int32_t permille = 0;
	int32_t combinedCapPercent = 100;
};

DLL_LINKAGE RecruitmentQuote quoteRecruitment(const ResourceSet & unitPrice, int32_t quantity,
	const ConscriptionLedger & ledger, bool eligible, const RecruitmentSpecialtyDiscount & specialty = RecruitmentSpecialtyDiscount());
DLL_LINKAGE int32_t affordableRecruitment(const ResourceSet & unitPrice, const ResourceSet & resources,
	int32_t available, const ConscriptionLedger & ledger, bool eligible, const RecruitmentSpecialtyDiscount & specialty = RecruitmentSpecialtyDiscount());
/// Lord Haart's Well Connected for creatures recruited into his army; never war machines.
DLL_LINKAGE RecruitmentSpecialtyDiscount recruitmentSpecialtyDiscount(const CGHeroInstance * buyer, CreatureID creature);
DLL_LINKAGE ConscriptionLedger recruitmentLedger(const CGHeroInstance * buyer);
DLL_LINKAGE bool usesConscriptionLedger(const CGHeroInstance * buyer, CreatureID creature = CreatureID::NONE);

/// Isolated prospective ledger for a multi-row UI/AI basket. Never a live state reference.
class DLL_LINKAGE RecruitmentQuoteCursor
{
	ConscriptionLedger ledger;
public:
	explicit RecruitmentQuoteCursor(const CGHeroInstance * buyer);
	explicit RecruitmentQuoteCursor(ConscriptionLedger initial) : ledger(initial) {}
	const ConscriptionLedger & current() const { return ledger; }
	void accept(const RecruitmentQuote & quote);
};
}
