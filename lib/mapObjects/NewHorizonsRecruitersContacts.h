/*
 * NewHorizonsRecruitersContacts.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include <optional>

class CGHeroInstance;
class CGDwelling;

namespace newHorizonsRecruitment
{
struct DLL_LINKAGE ContactsAward
{
	uint32_t row;
	uint32_t amount;
};
/// The first empty, positive-normal-growth row of an owned external dwelling.
/// Pure eligibility shared by visit production, authoritative replication and AI.
DLL_LINKAGE std::optional<ContactsAward> recruitersContactsAward(const CGHeroInstance & hero,
	const CGDwelling & dwelling, int32_t absoluteWeek);
}
