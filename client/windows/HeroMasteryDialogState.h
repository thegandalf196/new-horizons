/*
 * HeroMasteryDialogState.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <cstdint>
#include <optional>

/// Client-only presentation lifecycle. Never grants a mastery or resolves a query.
/// Kept independent of graphics so covered-window ACK ordering is testable.
class HeroMasteryDialogState
{
	int32_t query;
	std::optional<uint32_t> request;
	int selection = -1;
	bool waiting = false;
	bool resolved = false;

public:
	explicit HeroMasteryDialogState(int32_t queryID) : query(queryID) {}
	int selected() const { return selection; }
	bool isWaiting() const { return waiting; }
	bool isResolved() const { return resolved; }
	bool canClose(bool isTopWindow) const { return resolved && isTopWindow; }

	bool select(int index)
	{
		if(waiting || resolved || index < 0 || index >= 3)
			return false;
		selection = index;
		return true;
	}
	bool beginSubmission()
	{
		if(waiting || resolved || selection < 0)
			return false;
		waiting = true;
		return true;
	}
	void submitted(int requestID)
	{
		if(requestID < 0)
		{
			request.reset();
			waiting = false;
			selection = -1;
		}
		else
			request = static_cast<uint32_t>(requestID);
	}
	bool requestApplied(uint32_t requestID, bool success)
	{
		if(resolved || !request || *request != requestID || success)
			return false;
		request.reset();
		waiting = false;
		selection = -1;
		return true;
	}
	void queryResolved(int32_t queryID)
	{
		if(query == queryID)
			resolved = true;
	}
};
