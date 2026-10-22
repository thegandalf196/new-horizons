/*
 * BattleMovementPreview.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <string>

class BattleHex;
class CBattleInfoCallback;
namespace battle { class Unit; }

namespace battleMovementPreview
{
std::string overwatchWarning(const CBattleInfoCallback & battle,
	const battle::Unit * mover, const BattleHex & destination);
}
