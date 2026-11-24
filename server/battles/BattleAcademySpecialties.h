/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/battle/BattleSide.h"
#include "../../lib/constants/EntityIdentifiers.h"

#include <map>
#include <vector>

class CBattleInfoCallback;
class CGameHandler;
class CGHeroInstance;
class CSpell;
struct BattleStackAttacked;

/// Authoritative riders of the revised Academy specialties: Neela's Hardened marks after
/// each hit, Astral's Practice Makes Perfect after each of his casts and Rissa's Spoils of
/// Alchemy after a won combat. Rules live in lib/battle and lib/entities/hero
/// NewHorizonsAcademySpecialties.
namespace battleAcademySpecialties
{
/// Each damaged stack of a Hardened hero takes less damage from later hits until it acts.
void markHardenedHits(CGameHandler & handler, const CBattleInfoCallback & battle,
	const std::vector<BattleStackAttacked> & hits);
/// Astral's cast makes that spell stronger for him, up to his level cap.
void completeCast(CGameHandler & handler, const CGHeroInstance & hero, const CSpell & spell, bool automatic);
/// Theodorus's Inexhaustible Knowledge at the start of a combat round after the first.
void regenerateMana(CGameHandler & handler, const CBattleInfoCallback & battle);
/// Rissa's Mercury for the enemy creatures destroyed in the combat she won.
void grantSpoilsOfAlchemy(CGameHandler & handler, BattleID battleID, const CGHeroInstance * winner,
	const std::map<CreatureID, int32_t> & enemyCasualties);
}
