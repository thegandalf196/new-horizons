/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <functional>
#include <optional>
#include <string>

class PlayerState;

namespace stewardReminder
{
/// "<Town> has no steward, and <Hero> is free to be one." for the first owned
/// town without a steward while an eligible owned hero stewards no town.
std::optional<std::string> pendingReminder(const PlayerState & player);
/// Ends the turn directly, or after the player confirms a pending reminder
/// when the adventure "stewardReminder" setting is enabled.
void confirmEndTurn(const std::function<void()> & endTurn);
}
