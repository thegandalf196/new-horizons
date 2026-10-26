/*
 * HeroOrderLog.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <cstdint>

class CBattleInfoCallback;
class MetaString;
struct HeroOrderState;
enum class BattleSide : int8_t;
enum class HeroCommand : int8_t;

const char * heroOrderDisplayName(HeroCommand command);
MetaString heroOrderLogLine(const CBattleInfoCallback & battle, BattleSide side, const HeroOrderState & state);
