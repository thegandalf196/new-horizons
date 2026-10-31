/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/bonuses/BonusSelector.h"
class IBattleInfo;
namespace battle { class Unit; }
namespace graveDustAI
{
/// Native protected removal applies equally to ordinary and explicit owned subjects.
void validateSelectorRemoval(const IBattleInfo & battle, const battle::Unit & unit, const CSelector & requested);
CSelector protectedRemovalSelector(const battle::Unit & unit, const CSelector & requested);
}
