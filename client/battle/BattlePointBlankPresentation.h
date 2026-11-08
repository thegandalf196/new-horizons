/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <string>
class CBattleInfoCallback;
class CStack;
class BattleHex;
namespace pointBlankPresentation
{
bool setCursor(const CBattleInfoCallback & battle, const CStack * shooter, const BattleHex & target);
std::string status(const CBattleInfoCallback & battle, const CStack & shooter, const BattleHex & target);
}
