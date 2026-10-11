/*
 * BattleFieldReachability.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../lib/battle/BattleHexArray.h"

namespace battle { class Unit; }

/// Logical movement cache; refresh never depends on painting a background.
class BattleFieldReachability
{
	const battle::Unit * actor = nullptr;
	BattleHexArray availableHexes;
	bool needsRefresh = true;

public:
	void invalidate()
	{
		needsRefresh = true;
	}

	template<typename Loader>
	const BattleHexArray & get(const battle::Unit * activeActor, Loader && load)
	{
		if(!activeActor)
		{
			availableHexes.clear();
			actor = nullptr;
			needsRefresh = false;
		}
		else if(needsRefresh || actor != activeActor)
		{
			needsRefresh = true;
			auto fresh = load();
			availableHexes = std::move(fresh);
			actor = activeActor;
			needsRefresh = false;
		}
		return availableHexes;
	}
};
