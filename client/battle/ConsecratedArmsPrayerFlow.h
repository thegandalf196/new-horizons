/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/battle/BattleAction.h"

#include <functional>
#include <memory>

class BattleInterface;

/// Chosen Prayers (Consecrated Arms 4) submission. The Order (or Loynis's
/// Sermon spell) is the primary action and the prayer is optional:
/// 1. With an eligible recipient the spellbook opens on its Prayers bookmark;
///    otherwise the action is sent at once.
/// 2. Closing the book sends the action without a prayer.
/// 3. A chosen prayer continues on the battlefield, where a left-click on an
///    eligible stack sends the action with the prayer and a right-click,
///    Escape or any other way out sends it without the prayer.
/// Each route sends one ordinary validated request and changes nothing locally.
namespace consecratedArmsPrayerUI
{
using HeroSpellSender = std::function<void(const BattleAction &)>;
/// Single submission point for player Orders.
void submitOrder(const std::shared_ptr<BattleInterface> & owner, const BattleAction & order);
/// Hero spell submission: `send` submits the spell, with or without Sermon's prayer.
void submitHeroSpell(const std::shared_ptr<BattleInterface> & owner, const BattleAction & spell, HeroSpellSender send);
}
