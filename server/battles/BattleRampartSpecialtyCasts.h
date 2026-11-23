/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/battle/BattleSide.h"
#include "../../lib/battle/Destination.h"

#include <cstdint>
#include <utility>
#include <vector>

class CBattleInfoCallback;
class CGameHandler;
class CGHeroInstance;
class CSpell;
namespace spells { class Mechanics; }

/// Authoritative riders of the revised Rampart specialties: Uland's Living Remedy
/// after his Cure, Alagar's Brittle after his ice spells and Melodia's Against the
/// Odds at the start of combat. Rules live in lib/battle/NewHorizonsRampartSpecialties.
namespace battleRampartSpecialties
{
/// The stacks a rider cast may reach, with their Health before the cast.
struct CastContext
{
	std::vector<std::pair<uint32_t, int64_t>> recipients;
};

CastContext prepareCast(const CBattleInfoCallback & battle, const CGHeroInstance & hero, const CSpell & spell,
	const spells::Mechanics & mechanics, const battle::Target & target);
void completeCast(CGameHandler & handler, const CBattleInfoCallback & battle, const CGHeroInstance & hero,
	const CSpell & spell, BattleSide side, const CastContext & context);
/// Melodia's Luck for this combat when the enemy army is stronger than hers.
void grantAgainstTheOdds(CGameHandler & handler, const CBattleInfoCallback & battle);
}
