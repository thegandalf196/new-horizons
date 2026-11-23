/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

class CGameHandler;
class CGHeroInstance;
class CGTownInstance;

namespace newHorizonsPatroness
{
/// Jenova's Patroness on her arrival in one of her player's towns: once per town and
/// week she collects part of its daily income (captured revisedSpecialties, format 1166).
void collectOnArrival(CGameHandler & handler, const CGTownInstance & town, const CGHeroInstance & hero);
}
