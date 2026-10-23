/*
 * DamageCacheMarks.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
namespace battle { class Unit; }
namespace damageCacheDetail
{
bool hasRangedMarkEffect(const battle::Unit * unit, const char * source);
bool hasNightProwlerBonus(const battle::Unit * unit);
}
