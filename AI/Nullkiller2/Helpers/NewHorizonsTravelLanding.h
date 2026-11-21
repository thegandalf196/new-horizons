/*
 * NewHorizonsTravelLanding.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

namespace NK2AI
{
class AIGateway;

/// Consume current-day shared landing plans, then submit EndTurn. Only a query
/// that blocked the attempt (e.g. a post-battle level-up delivered after
/// BattleEnded) authorizes a bounded retry, after that query is answered.
/// Caller owns the same shared game-state guard as AIGateway::makeTurn.
/// False retains the turn; any other refusal never authorizes a retry or free rescue.
bool tryEndTurnWithLegalLanding(AIGateway & gateway);
}
