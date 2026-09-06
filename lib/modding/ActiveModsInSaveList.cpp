/*
 * ActiveModsInSaveList.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ActiveModsInSaveList.h"

#include "../GameLibrary.h"
#include "../constants/StringConstants.h"
#include "ModDescription.h"
#include "CModHandler.h"
#include "ModIncompatibility.h"

std::vector<TModID> ActiveModsInSaveList::getActiveGameplayAffectingMods()
{
	std::vector<TModID> result;
	for (auto const & entry : LIBRARY->modh->getActiveMods())
		if (LIBRARY->modh->getModInfo(entry).affectsGameplay())
			result.push_back(entry);

	return result;
}

ModVerificationInfo ActiveModsInSaveList::getVerificationInfo(TModID mod)
{
	return LIBRARY->modh->getModInfo(mod).getVerificationInfo();
}

void ActiveModsInSaveList::verifyActiveMods(const std::map<TModID, ModVerificationInfo> & modList)
{
	auto comparison = ModVerificationInfo::verifyListAgainstLocalMods(modList);
	std::vector<TModID> missingMods;
	std::vector<TModID> excessiveMods;

	for (auto const & compared : comparison)
	{
		if (compared.first.find('.') != std::string::npos)
			continue; // ignore changes in submods since they change a lot between mod updates

		if (compared.second == ModVerificationStatus::NOT_INSTALLED)
			missingMods.push_back(modList.at(compared.first).name);

		if (compared.second == ModVerificationStatus::DISABLED)
			missingMods.push_back(LIBRARY->modh->getModInfo(compared.first).getName());

		// The curated module adds save-scoped rules and new entities without
		// replacing legacy definitions. Old games load empty rule snapshots and
		// retain their original skills/spells; keep the module for later new games.
		// This exception is save-only and only for an EXTRA curated module: a
		// required missing/disabled NH module, or any other excess mod, still fails.
		if (compared.second == ModVerificationStatus::EXCESSIVE
			&& compared.first != GameConstants::NEW_HORIZONS_MOD_SCOPE)
			excessiveMods.push_back(LIBRARY->modh->getModInfo(compared.first).getName());
	}

	if(!missingMods.empty() || !excessiveMods.empty())
		throw ModIncompatibility(missingMods, excessiveMods);
}

