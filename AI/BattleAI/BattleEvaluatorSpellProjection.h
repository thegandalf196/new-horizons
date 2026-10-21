/*
 * BattleEvaluatorSpellProjection.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "HeroActionAdmission.h"
#include "../../lib/spells/NewHorizonsVengefulVines.h"

class CSpell;
class CGHeroInstance;
class CBattleInfoCallback;
class PossibleSpellcast;

namespace battleEvaluatorProjection
{
bool isCanonicalVerdantPrison(const CSpell * spell);
bool isCanonicalRegeneration(const CSpell * spell, const JsonNode & rules);
struct SpellProjection
{
	bool accepted = false;
	std::optional<newHorizonsVengefulVines::ProjectionReceipt> vines;
};
/// Runs the ordinary generic spell projection and accepted-action bookkeeping.
/// Failure leaves only the disposable branch mutated; callers discard it.
SpellProjection projectSpell(HypotheticBattle & state, const CBattleInfoCallback & live,
	const CGHeroInstance * hero, BattleSide side, const PossibleSpellcast & candidate,
	const heroActionProjection::Admission & admission, uint32_t & targetId,
	const std::vector<uint32_t> & sharedPurposeRecipients);
}
