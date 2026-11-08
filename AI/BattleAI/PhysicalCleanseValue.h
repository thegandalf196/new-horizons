/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <map>
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace physicalCleanseAI
{
/// One immutable selector invocation. Never reused across worlds or mutations.
class MobilityQuery
{
	const CBattleInfoCallback * source = nullptr;
	std::map<const battle::Unit *, float> values;
public:
	float crippleValue(const CBattleInfoCallback &, const battle::Unit &);
};
}
