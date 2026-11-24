/*
 * MarchLeftTransfer.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

class CArmedInstance;
class CGHeroInstance;

namespace NK2AI::marchLeftTransfer
{
/// A hero left with less than this share of his daily movement is stranded for the day.
constexpr int STRANDED_PERCENT = 10;

/// Whether taking every stack of the source would strand the receiver for the rest of
/// today: he can still march now, but the troops' march left would leave him less than
/// STRANDED_PERCENT of his daily movement.
bool strandsReceiver(const CGHeroInstance & receiver, const CArmedInstance & source, int day);
}
