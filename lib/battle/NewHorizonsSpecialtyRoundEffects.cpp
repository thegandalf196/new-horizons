/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyRoundEffects.h"
#include "SpecialtyEventPolicy.h"
#include "CUnitState.h"
#include "CBattleInfoCallback.h"
#include "../json/JsonNode.h"
#include <limits>
namespace newHorizonsSpecialtyEvents
{
int32_t roundReceiptFromJson(const JsonNode & snapshot, int32_t absent)
{
	const auto & state = snapshot["state"];
	if(!state.isStruct() || !state.Struct().contains("ordinarySpecialtyLastRound")) return absent;
	const auto & value = state["ordinarySpecialtyLastRound"];
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < -1
		|| value.Integer() > std::numeric_limits<int32_t>::max())
		throw std::runtime_error("Invalid ordinary specialty round receipt");
	return static_cast<int32_t>(value.Integer());
}
bool needsRoundEffects(const CBattleInfoCallback & battle, const battle::Unit & unit, int32_t round)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(&unit);
	if(!state || round < 1 || state->ordinarySpecialtyLastRound >= round
		|| state->isClone() || state->getPhantomInitialIntegrity() > 0) return false;
	using namespace detail;
	return (nativeFamily(*state, Family::Water)
		&& coefficient(battle, unit, "core:kalt", "risingTideCreaturesPerRound", true) > 0)
		|| (nativeFamily(*state, Family::WalkingDead)
			&& coefficient(battle, unit, "core:straker", "undyingHealingHP", true) > 0);
}
bool applyRound(const CBattleInfoCallback & battle, battle::CUnitState & unit, int32_t round)
{
	if(!needsRoundEffects(battle, unit, round)) return false;
	unit.ordinarySpecialtyLastRound = round;
	// A completed round boundary is consumed even while this native stack is
	// dead, held in Stasis, or represented by a borrowed body. No later backpay.
	if(!unit.alive() || unit.isGhost() || unit.isTimeStopped() || unit.hasBattleForm()) return true;
	using namespace detail;
	const int growth = family(unit, Family::Water)
		? coefficient(battle, unit, "core:kalt", "risingTideCreaturesPerRound", true) : 0;
	const int healing = family(unit, Family::WalkingDead)
		? coefficient(battle, unit, "core:straker", "undyingHealingHP", true) : 0;
	if(growth > 0) unit.health.appendTemporaryCreatures(growth);
	if(healing > 0)
	{
		const auto capacity = std::max<int64_t>(0, int64_t(unit.unitBaseAmount()) * unit.getMaxHealth()
			- unit.health.getCreatureHealthAvailable());
		int64_t amount = std::min<int64_t>(healing, capacity);
		unit.heal(amount, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	}
	return true;
}
}
