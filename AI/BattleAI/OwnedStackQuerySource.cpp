/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedStackQuerySource.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/OwnedFrameBranchQueries.h"
#include "../../lib/bonuses/ProjectedBonusView.h"
#include <stdexcept>

OwnedStackQuerySource::OwnedStackQuerySource(const HypotheticBattle & owner,
	const StackWithBonuses & subject, std::shared_ptr<battle::OwnedUnitBonusFrame> frame)
	: owner(owner), subject(subject), frame(std::move(frame))
{
	initialUnit(this->frame);
}
const battle::CUnitState & OwnedStackQuerySource::initialUnit(
	const std::shared_ptr<battle::OwnedUnitBonusFrame> & frame)
{
	if(!frame)
		throw std::invalid_argument("Owned stack query requires its retained Frame");
	return frame->unit();
}
const battle::OwnedUnitBonusFrame & OwnedStackQuerySource::current() const
{
	// localInit queries health before its explicit state copy. Keep the original
	// captured basis until that copy completes; never synchronize reset zeros.
	if(initialized)
		owner.synchronizeOwnedFrame(subject, *frame);
	return *frame;
}
const battle::OwnedUnitBonusFrame & OwnedStackQuerySource::current(const battle::Unit * requested) const
{
	if(requested != &subject)
		throw std::invalid_argument("Owned stack environment requires its exact bound subject");
	return current();
}
std::shared_ptr<battle::CUnitState> OwnedStackQuerySource::acquireState() const
{
	current();
	return battle::OwnedFrameBranchQueries::acquireState(frame);
}
std::vector<Bonus> OwnedStackQuerySource::localSpellEffects() const
{
	const auto * provider = dynamic_cast<const ProjectedBonusView *>(current().unit().getBonusBearer());
	if(!provider)
		throw std::logic_error("Owned frame lost its exact projected source-local provider");
	return provider->sourceLocalSpellEffects();
}
bool OwnedStackQuerySource::unitHasAmmoCart(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitHasAmmoCart(&basis.unit());
}
PlayerColor OwnedStackQuerySource::unitEffectiveOwner(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitEffectiveOwner(&basis.unit());
}
std::optional<int> OwnedStackQuerySource::unitMagicResistance(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitMagicResistance(&basis.unit());
}
int OwnedStackQuerySource::unitSylvanTacticsSpeed(const battle::Unit * unit, int turn) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitSylvanTacticsSpeed(&basis.unit(), turn);
}
int OwnedStackQuerySource::unitSylvanTacticsInitiativePercent(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitSylvanTacticsInitiativePercent(&basis.unit());
}
int OwnedStackQuerySource::unitFortuneSpeed(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitFortuneSpeed(&basis.unit());
}
int OwnedStackQuerySource::unitSpeedBonus(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitSpeedBonus(&basis.unit());
}
bool OwnedStackQuerySource::unitSlowAffectsSpeed(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitSlowAffectsSpeed(&basis.unit());
}
newHorizonsNecromancy::CombatPassives OwnedStackQuerySource::unitNecromancyPassives(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitNecromancyPassives(&basis.unit());
}
int OwnedStackQuerySource::unitAdditionalRetaliations(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitAdditionalRetaliations(&basis.unit());
}
int OwnedStackQuerySource::unitBloodragePainIncrement(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitBloodragePainIncrement(&basis.unit());
}
bool OwnedStackQuerySource::unitHasVeteranCohesion(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitHasVeteranCohesion(&basis.unit());
}
std::optional<std::pair<int32_t, int32_t>> OwnedStackQuerySource::unitMoraleLimits(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitMoraleLimits(&basis.unit());
}
std::optional<std::pair<int32_t, int32_t>> OwnedStackQuerySource::unitLuckLimits(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitLuckLimits(&basis.unit());
}

std::optional<bool> OwnedStackQuerySource::unitLegacyTimeStopSpellMatch(const battle::Unit * unit, SpellID spell) const
{
	const auto & basis = current(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitLegacyTimeStopSpellMatch(&basis.unit(), spell);
}

newHorizonsElementalAttunement::UnitPassives OwnedStackQuerySource::unitElementalAttunement(const battle::Unit * unit) const
{
	const auto & basis = current(unit);
	const auto side = unit->unitSide();
	if((side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& owner.elementalAttunementProfileKnown.at(side))
		return owner.unitElementalAttunement(unit);
	return battle::OwnedFrameBranchQueries::environment(basis).unitElementalAttunement(&basis.unit());
}
