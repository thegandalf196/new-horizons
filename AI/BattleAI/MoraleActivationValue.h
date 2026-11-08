/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
class CBattleInfoCallback;
struct MoraleSuppressionState;
namespace battle { class Unit; }
namespace moraleActivationValue
{
float expectedMoraleActivationChange(int morale);
float expectedMoraleActivationChange(const CBattleInfoCallback &, const battle::Unit *);
/// Existing bounded suppression arithmetic. Caller chooses owned or scratch state.
float projectDelta(float before, float after, float horizon, MoraleSuppressionState &);
}
