/*
 * BattleResultReply.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../lib/constants/EntityIdentifiers.h"

/// A displayed result does not necessarily have a server confirmation query.
template<typename Sender>
void sendBattleResultReply(int selection, QueryID queryID, const Sender & sender)
{
	if(queryID != QueryID::NONE)
		sender(selection, queryID);
}
