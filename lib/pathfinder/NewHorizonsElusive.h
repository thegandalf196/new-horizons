/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../int3.h"

class IGameInfoCallback;
class CGHeroInstance;
class CGObjectInstance;

/// Kyrre's Elusive (captured revisedSpecialties, format 1166): she moves next to a
/// neutral guard of a rank her level covers without being attacked. Stepping onto
/// the guard itself still starts the fight. Shared by the pathfinder, the server
/// movement check and, through the pathfinder, the adventure AI.
namespace newHorizonsElusive
{
/// True when this guard does not stop `hero` on `tile`: it is passable for the hero
/// (Peacemaker) or Elusive covers it, and `tile` is not the guard's own tile.
DLL_LINKAGE bool ignoresGuard(const IGameInfoCallback & gameInfo, const CGHeroInstance & hero,
	const CGObjectInstance & guardian, const int3 & tile);
}
