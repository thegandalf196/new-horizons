/*
 * NewHorizonsScholar.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
class CGameHandler;
class CGHeroInstance;
namespace newHorizonsScholar
{
/// Validated ordinary receipt and both pre-meeting spell sets; simulation only.
void execute(CGameHandler & handler, const CGHeroInstance & first, const CGHeroInstance & second);
}
