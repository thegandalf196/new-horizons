/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeSpellFactory.h"
#include "Damage.h"
#include "Heal.h"
#include "LifeDrain.h"
#include "Timed.h"
#include "StatusHelpers.h"
#include "SummoningAndRelocation.h"
#include "StructureCommon.h"
#include "../../../scripting/ScriptTypeDescription.h"

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> create(const ScriptTypeDescription & description)
{
	using namespace spells::effects::native;
	if(description.kind != ScriptKind::SPELL_EFFECT) return {};
	if(description.sourcePath == "spells/timed")
	{
		const std::vector<ScriptSource> expected{{"core", "spells/patches/timedShield"}, {"core", "spells/patches/timedBind"}};
		if(description.patches != expected) return {};
		return std::make_shared<Timed>();
	}
	if(!description.patches.empty()) return {};
	if(description.sourcePath == "spells/damage") return std::make_shared<Damage>();
	if(description.sourcePath == "spells/heal") return std::make_shared<Heal>();
	if(description.sourcePath == "spells/lifeDrain") return std::make_shared<LifeDrain>();
	if(description.sourcePath == "spells/unitEffect") return std::make_shared<UnitEffect>();
	if(description.sourcePath == "spells/spellEffect") return std::make_shared<SpellEffect>();
	if(auto effect = createStatusEffect(description)) return effect;
	if(auto effect = createSingleTargetStatusEffect(description)) return effect;
	if(auto effect = createUtilityEffect(description)) return effect;
	if(auto effect = createLocationUtilityEffect(description)) return effect;
	if(auto effect = createCreatureUtilityEffect(description)) return effect;
	if(auto effect = createSummoningAndRelocationEffect(description)) return effect;
	if(auto effect = createStructureEffect(description)) return effect;
	if(auto effect = createObstacleEffect(description)) return effect;
	return {};
}
}
