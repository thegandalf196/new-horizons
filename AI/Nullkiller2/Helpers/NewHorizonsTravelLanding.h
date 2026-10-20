/*
 * NewHorizonsTravelLanding.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

namespace NK2AI
{
class AIGateway;

/// Consume current-day shared landing plans, then submit at most one EndTurn.
/// Caller owns the same shared game-state guard as AIGateway::makeTurn.
/// False retains the turn; it never authorizes an EndTurn retry or free rescue.
bool tryEndTurnWithLegalLanding(AIGateway & gateway);
}
