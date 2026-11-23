/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

class CGameHandler;
class CGHeroInstance;
class CGObjectInstance;
class CGTownInstance;

namespace newHorizonsPatroness
{
/// Jenova's Patroness on her arrival in one of her player's towns: once per town and
/// week she collects part of its daily income (captured revisedSpecialties, format 1166).
void collectOnArrival(CGameHandler & handler, const CGTownInstance & town, const CGHeroInstance & hero);
/// After a battle fought over a visited object: a won siege captured the town, and that
/// capture is her arrival there even though no town visit follows the battle.
void collectAfterBattle(CGameHandler & handler, const CGObjectInstance & object, const CGHeroInstance & hero);
}
