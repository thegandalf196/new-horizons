/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BonusEnum.h"
#include "../constants/EntityIdentifiers.h"
#include <optional>
#include <vector>
/// Value-only capture inputs. No source/node/library identities are retained.
struct DLL_LINKAGE OwnedBonusEvaluationFacts
{
	struct ArmySlot
	{
		CreatureID creature;
		FactionID faction;
		int level;
		int count;
	};
	BonusNodeType nodeType = BonusNodeType::UNKNOWN;
	PlayerColor owner = PlayerColor::CANNOT_DETERMINE;
	std::optional<int> heroLevel;
	std::optional<int> stackCount;
	std::optional<int> stableStackLevel;
	std::optional<std::vector<ArmySlot>> armySlots;
	/// Required applicable facts cannot be replaced by zero or a live fallback.
	void validate() const;
};
