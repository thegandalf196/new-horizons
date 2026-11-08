/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once

struct lua_State;
struct DamageAttackInfo;
namespace nativeDamage { struct Facts; }

namespace nativeDamageOracle
{
/// Constructs one registry-held invocation after preparing all mock observations.
using PrepareInvocation = int (*)(lua_State *, int, int, DamageAttackInfo &, const nativeDamage::Facts &);
void benchmark(lua_State * state, int script, int adapter, PrepareInvocation prepare);
}
