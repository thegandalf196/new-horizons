/*
 * ExtraOptionsInfo.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include <cstdint>
#include <optional>

struct DLL_LINKAGE ExtraOptionsInfo
{
	bool cheatsAllowed = true;
	bool unlimitedReplay = false;
	/// if set, client stores every received netpack on disk so the whole game can be replayed later
	bool recordGame = false;
	/// Fresh-world request only; absence retains the authored map/default rule.
	std::optional<int32_t> neutralArmySizePercent;

	void validateNeutralArmySize() const;
	void validateNeutralArmySizeSerialization(bool supported) const;

	bool operator == (const ExtraOptionsInfo & other) const = default;

	template <typename Handler>
	void serialize(Handler &h)
	{
		if(h.saving)
			validateNeutralArmySizeSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_NEUTRAL_ARMY_START_OPTION));
		h & cheatsAllowed;
		h & unlimitedReplay;
		if(h.hasFeature(Handler::Version::GAME_REPLAY_RECORDING))
			h & recordGame;
		else
			recordGame = false;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_NEUTRAL_ARMY_START_OPTION))
		{
			h & neutralArmySizePercent;
			if(!h.saving) validateNeutralArmySize();
		}
		else if(!h.saving)
			neutralArmySizePercent.reset();
	}
};
