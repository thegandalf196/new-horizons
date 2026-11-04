/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "effects/Effects.h"
#include <vcmi/spells/Caster.h>
class CSpell;
class JsonNode;
class CBattleInfoCallback;
class ServerCallback;
namespace spells { class BattleSpellMechanics; }
namespace newHorizonsMetamagic { struct CastProfile; }
namespace newHorizonsRegenerationEffects
{
DLL_LINKAGE bool isSpell(const CSpell * spell, const JsonNode & savedRules);
DLL_LINKAGE bool isMassSpell(const CSpell * spell, const JsonNode & savedRules);
DLL_LINKAGE bool eligible(const battle::Unit * unit);
DLL_LINKAGE int32_t rate(const spells::BattleSpellMechanics & mechanics);
DLL_LINKAGE void applyRateSnapshot(ServerCallback * server, const CBattleInfoCallback * battle,
	const battle::Units & affectedUnits, int32_t regenerationRate, SpellID regenerationSpell,
	const newHorizonsMetamagic::CastProfile * metamagic);
DLL_LINKAGE void filterMassTargets(spells::effects::Effects::EffectsToApply & effectsToApply);
}
