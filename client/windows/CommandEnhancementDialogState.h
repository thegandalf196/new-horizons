/*
 * CommandEnhancementDialogState.h, part of VCMI engine
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

/// Presentation lifecycle only; successful ACKs never grant an award or finish a query.
class CommandEnhancementDialogState
{
	int32_t query;
	std::optional<uint32_t> request;
	int selection = -1;
	bool waiting = false;
	bool resolved = false;

public:
	explicit CommandEnhancementDialogState(int32_t queryID) : query(queryID) {}
	int selected() const { return selection; }
	bool isWaiting() const { return waiting; }
	bool matchesQuery(int32_t queryID) const { return query == queryID; }
	bool canClose(bool topmost) const { return resolved && topmost; }
	bool select(int index)
	{
		if(waiting || resolved || index < 0 || index >= 9)
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
		if(requestID >= 0)
			request = static_cast<uint32_t>(requestID);
		else
		{
			request.reset();
			waiting = false;
			selection = -1;
		}
	}
	bool requestApplied(uint32_t requestID, bool success)
	{
		if(resolved || success || !request || *request != requestID)
			return false;
		request.reset();
		waiting = false;
		selection = -1;
		return true;
	}
	void queryResolved(int32_t queryID)
	{
		if(queryID == query)
			resolved = true;
	}
};
