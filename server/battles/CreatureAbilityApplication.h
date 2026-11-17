/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/CreatureAbility.h"
class CGameHandler;
class CBattleInfoCallback;
struct SetStackEffect;
namespace creatureAbilityApplication
{
bool resisted(CGameHandler & game, const CBattleInfoCallback & battle,
	const battle::Unit & target, const creatureAbility::Descriptor & descriptor);
/// Direct application only. Spell-backed groups use the cast's existing one
/// cached resistance result, so this path must never roll for them again.
void preventDirectAfflictions(CGameHandler & game, SetStackEffect & effects);
}
