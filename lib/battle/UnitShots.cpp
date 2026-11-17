/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "NewHorizonsFocusMagic.h"
#include "../bonuses/Bonus.h"

namespace battle
{
///CShots
CShots::CShots(const battle::Unit * Owner)
	: CAmmo(Owner, Selector::type()(BonusType::SHOTS)),
	shooter(Owner, Selector::type()(BonusType::SHOOTER)),
	disruptedSupply(Owner, CSelector([](const Bonus * bonus)
	{
		return bonus && bonus->type == BonusType::NONE && bonus->source == BonusSource::OTHER
			&& bonus->stacking == DISRUPTED_SUPPLY && bonus->duration == BonusDuration::ONE_BATTLE && bonus->val == 1;
	}))
{
}

CShots & CShots::operator=(const CShots & other)
{
	// A state copy transfers usage, not its source unit's cache target or environment.
	CAmmo::operator=(other);
	return *this;
}

bool CShots::isLimited() const
{
	if(newHorizonsFocusMagic::converted(*owner) || disruptedSupply.hasBonus())
		return true;
	return !shooter.hasBonus() || !env->unitHasAmmoCart(owner);
}

void CShots::setEnv(const IUnitEnvironment * env_) noexcept
{
	env = env_;
}

int32_t CShots::total() const
{
	if(newHorizonsFocusMagic::converted(*owner))
		return static_cast<int32_t>(std::min<int64_t>(std::numeric_limits<int32_t>::max(),
			static_cast<int64_t>(used) + newHorizonsFocusMagic::charges(*owner)));
	if(shooter.hasBonus())
		return CAmmo::total();
	else
		return 0;
}

}
