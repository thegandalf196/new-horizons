/*
 * CreatureUpgradeConfirmation.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/ResourceSet.h"

#include <functional>

namespace creatureUpgradeConfirmation
{
/// Present the selected authoritative upgrade offer without changing its command.
void show(const TResources & totalCost, CreatureID target, unsigned int count,
	const std::function<void()> & onUpgrade);
}
