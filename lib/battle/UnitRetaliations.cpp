/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "NewHorizonsMaintenanceSpecialties.h"
#include "NewHorizonsCreatureAbilitySuppression.h"
#include "../serializer/JsonSerializeFormat.h"
#include "../bonuses/BonusSelector.h"
#include <algorithm>
#include <limits>
namespace battle
{
///CRetaliations
CRetaliations::CRetaliations(const battle::Unit * Owner)
	: CAmmo(Owner, Selector::type()(BonusType::ADDITIONAL_RETALIATION).And(
		CSelector([](const Bonus * bonus) { return !newHorizonsMaintenanceSpecialties::isDefendRetaliationBonus(bonus); }))),
	totalCache(0),
	env(nullptr),
	noRetaliation(Owner, Selector::type()(BonusType::SIEGE_WEAPON).Or(Selector::type()(BonusType::NO_RETALIATION))),
	unlimited(Owner, Selector::type()(BonusType::UNLIMITED_RETALIATIONS))
{
}

bool CRetaliations::isLimited() const
{
	return !unlimited.hasBonus() || noRetaliation.hasBonus();
}

int32_t CRetaliations::total() const
{
	if(noRetaliation.hasBonus())
		return 0;

	// Legacy Heroic Spirit state remains readable, but no longer grants a
	// creature retaliation: the reworked perk grants a typed Hero Order action.
	const int64_t additionalRetaliations = (env
		? static_cast<int64_t>(env->unitAdditionalRetaliations(owner))
			+ env->unitNecromancyPassives(owner).additionalRetaliations : 0)
		+ newHorizonsMaintenanceSpecialties::defendRetaliations(*owner);
	if(newHorizonsCreatureAbilitySuppression::suppressionLevel(*owner) >= 2)
	{
		// The ordinary bonus cache normally preserves a larger observed cap for
		// the current round after Dispel. Mindbreaker is an active suppression,
		// not removal of the bonus: do not let that latch retain native extra
		// retaliations while the marker is present.
		const int64_t currentTotal = 1LL + totalProxy.getValue() + additionalRetaliations;
		return static_cast<int32_t>(std::clamp<int64_t>(currentTotal, 0,
			std::numeric_limits<int32_t>::max()));
	}

	//after dispel bonus should remain during current round
	int32_t val = 1 + totalProxy.getValue();
	vstd::amax(totalCache, val);
	// Keep the ordinary, cached allowance separate so a live controller or
	// Bloodrage threshold change can remove the derived retaliation immediately.
	return static_cast<int32_t>(std::min<int64_t>(std::numeric_limits<int32_t>::max(),
		static_cast<int64_t>(totalCache) + additionalRetaliations));
}

CRetaliations & CRetaliations::operator=(const CRetaliations & other)
{
	CAmmo::operator=(other);
	totalCache = other.totalCache;
	// Keep the destination's no-retaliation/unlimited bonus cache bindings.
	return *this;
}

void CRetaliations::reset()
{
	CAmmo::reset();
	totalCache = 0;
}

void CRetaliations::setEnv(const IUnitEnvironment * env_) noexcept
{
	env = env_;
}

void CRetaliations::serializeJson(JsonSerializeFormat & handler)
{
	CAmmo::serializeJson(handler);
	//we may be serialized in the middle of turn
	handler.serializeInt("totalCache", totalCache, 0);
}

}
