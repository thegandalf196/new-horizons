/*
 * RoundBonusDuration.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../bonuses/BonusSelector.h"
namespace battle
{
class Unit;
/// Construct base once per round, capture seal/polarity once per timed unit,
/// before its own exported duration records are changed.
DLL_LINKAGE CSelector roundTimedBonuses();
DLL_LINKAGE CSelector roundTimedBonusesFor(const Unit & unit, const CSelector & base);
}
