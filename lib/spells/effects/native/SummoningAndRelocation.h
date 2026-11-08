/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
struct ScriptTypeDescription;
namespace spells::effects { class Effect; }
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createSummoningAndRelocationEffect(const ScriptTypeDescription &);
}
