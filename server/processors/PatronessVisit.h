/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/constants/EntityIdentifiers.h"

class CGameHandler;
class CGHeroInstance;
class CGTownInstance;

namespace newHorizonsPatroness
{
/// Jenova's Patroness on her arrival in one of her player's towns: once per town and
/// week she collects part of its daily income (captured revisedSpecialties, format 1166).
void collectOnArrival(CGameHandler & handler, const CGTownInstance & town, const CGHeroInstance & hero);
/// After a battle fought over a visited object: a won siege captured the town, and that
/// capture is her arrival there even though no town visit follows the battle. Takes ids,
/// not pointers: the battle's aftermath may already have removed the object or the hero.
void collectAfterBattle(CGameHandler & handler, ObjectInstanceID object, ObjectInstanceID hero);
}
