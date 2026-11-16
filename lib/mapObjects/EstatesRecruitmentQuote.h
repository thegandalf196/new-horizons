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

DLL_LINKAGE RecruitmentQuote quoteRecruitment(const ResourceSet & unitPrice, int32_t quantity,
	const ConscriptionLedger & ledger, bool eligible);
DLL_LINKAGE int32_t affordableRecruitment(const ResourceSet & unitPrice, const ResourceSet & resources,
	int32_t available, const ConscriptionLedger & ledger, bool eligible);
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
