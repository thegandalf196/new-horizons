/*
 * HypotheticEffectSnapshotRules.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/bonuses/Bonus.h"

#include <set>
#include <utility>

namespace battle { class Unit; }

namespace hypotheticEffectSnapshotRules
{
bool hasElementalMemory(const battle::Unit & unit);
bool isInPhysicalAfflictionGroup(const Bonus * bonus,
	const std::set<std::pair<BonusSource, BonusSourceID>> & groupHistory);
bool isCapturedProjectionEffect(const Bonus * bonus,
	const std::set<std::pair<BonusSource, BonusSourceID>> & groupHistory);
bool timedProjectionEffect(const Bonus * bonus,
	const std::set<std::pair<BonusSource, BonusSourceID>> & groupHistory);
bool hasTimedMagicalSpellEffects(const IBonusBearer & bearer);
bool hasSnapshotHeroSpecialEffects(const IBonusBearer & bearer);
}
