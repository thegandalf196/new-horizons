/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <vcmi/spells/Caster.h>
#include <memory>
class CBattleInfoCallback;
namespace spells
{
class Caster;
class Spell;
/// Same-cast native creature facts, owned before any reflected effects execute.
bool admitsRunicCreatureCaster(const Caster &, const CBattleInfoCallback &);
std::unique_ptr<Caster> copyRunicCreatureCaster(const Caster &);
DLL_LINKAGE std::unique_ptr<Caster> captureRunicCreatureCaster(const Caster &, const Spell &, const CBattleInfoCallback &, const battle::Units &);
}
