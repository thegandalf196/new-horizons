/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
class CBattleInfoCallback;
class DamageCache;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace passiveDivineFuryAI
{
/// Called only at ordinary primary / normal retaliation sites before the
/// physical blow consumes its Order. Collateral/reactions never call this seam.
bool admitted(const CBattleInfoCallback &, const BattleAttackInfo &);
/// One native Holy Wrath numerical preview against the legal surviving primary
/// after physical damage. No castEval, random draw, state change or recursive proc.
/// Repeated procs use independent nominal expected damage; downstream death,
/// payment/readiness and interruption sequences remain unprojected.
float expectedValue(const std::shared_ptr<CBattleInfoCallback> &, const battle::Unit & victim,
	const BattleAttackInfo &, bool underOrderBeforeStrike, DamageCache &);
}
