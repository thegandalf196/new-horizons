/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../../combatScripts/nativeEvents/NativeEventCommon.h"
#include "../../../bonuses/BonusList.h"
#include "../Effect.h"

namespace spells::effects::native::status
{
DLL_LINKAGE bool isTemporary(const Bonus &);
JsonNode bonus(const std::string & type, int64_t value, const std::string & source,
	int turns, const std::string & subtype = {}, bool debuff = false);
void add(const nativeEvents::Context &, const battle::Unit &, JsonNode data, bool cumulative = false);
BonusList previous(const battle::Unit &, BonusType, const std::string & source, const std::string & subtype = {});
void refresh(const nativeEvents::Context &, const battle::Unit &, BonusType, const std::string & source, const std::string & subtype = {});
bool reject(const spells::Mechanics *, spells::Problem *);
}

struct ScriptTypeDescription;
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createStatusEffect(const ScriptTypeDescription &);
std::shared_ptr<spells::effects::Effect> createSingleTargetStatusEffect(const ScriptTypeDescription &);
std::shared_ptr<spells::effects::Effect> createUtilityEffect(const ScriptTypeDescription &);
std::shared_ptr<spells::effects::Effect> createLocationUtilityEffect(const ScriptTypeDescription &);
std::shared_ptr<spells::effects::Effect> createCreatureUtilityEffect(const ScriptTypeDescription &);
}
