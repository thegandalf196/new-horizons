/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../GameConstants.h"
#include <string>
class CCreature;
namespace battle
{
/// Internal unit metadata/key owner, not a registry, native import or bonus graph.
class DLL_LINKAGE UnitCreatureResolver
{
public:
	virtual ~UnitCreatureResolver() = default;
	virtual const CCreature * creature(CreatureID id) const = 0;
	virtual int32_t decode(const std::string & key) const = 0;
	virtual std::string encode(int32_t id) const = 0;
};
}
