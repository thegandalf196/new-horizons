/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "LeadershipEfficiencyBadge.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"

#include <array>
#include <optional>
#include <vector>

class Canvas;
class Point;
class CCreatureSet;
class CGHeroInstance;

namespace leadershipPresentation
{
void drawBadge(Canvas & canvas, const Point & position, int percent, BadgeRole role);
std::string detailText(const newHorizonsHeroes::LeadershipStackReadout & readout);
std::string heroDetailText(const CGHeroInstance & hero);

/// UI-only observation cache. It never owns or modifies gameplay state.
/// Composition records here track adventure armies; battle callers bind the
/// immutable battle capture's identity instead of the changing live army.
class ReadoutCache
{
	const CGHeroInstance * hero = nullptr;
	const void * context = nullptr;
	int32_t bonusVersion = -1;
	uint32_t level = 0;
	int64_t policyVersion = 0;
	CreatureID creature;
	int64_t count = -1;
	std::vector<std::pair<SecondarySkill, ui8>> skills;
	std::vector<newHorizonsHeroes::PerkSelection> perks;
	std::array<CreatureID, 7> composition{};
	newHorizonsHeroes::LeadershipStackReadout value;
	bool valid = false;

	bool matches(const CGHeroInstance * owner, CreatureID type, int64_t amount,
		const CCreatureSet * army, const void * capture) const;
	void capture(const CGHeroInstance * owner, CreatureID type, int64_t amount,
		const CCreatureSet * army, const void * capture);

public:
	void invalidate() { valid = false; }

	template<typename Calculate>
	const newHorizonsHeroes::LeadershipStackReadout & get(const CGHeroInstance * owner,
		CreatureID type, int64_t amount, const CCreatureSet * army, const void * capture,
		Calculate && calculate)
	{
		if(!matches(owner, type, amount, army, capture))
		{
			value = calculate();
			this->capture(owner, type, amount, army, capture);
		}
		return value;
	}
};
}
