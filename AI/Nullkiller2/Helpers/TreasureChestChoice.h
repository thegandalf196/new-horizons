/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

namespace NK2AI
{
/// Treasure chest choice: true takes the Gold, false the Experience.
/// A secondary hero, or any hero while Gold pressure is over its maximum, values the
/// Experience at half the Gold and takes the Gold. Caitlin's Alms of the Faithful
/// returns part of that Gold with the Experience, so from a donation of half the
/// Gold the Experience option is worth at least as much and she takes it.
bool prefersChestGold(bool mainHero, bool goldPressure, int donationPercent);
}
