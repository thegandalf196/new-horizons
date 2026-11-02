/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedPreparationController.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/battle/OwnedBattleFrameBatch.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include <stdexcept>
namespace ownedPreparationIntegration
{
namespace
{
void require(bool value, const char * message) { if(!value) throw std::runtime_error(message); }
SourceObservations observation(const ownedPreparationAudit::Observation & value)
{
	return {value.battle, value.target, value.scalar, value.stopped, value.healingReduction, value.simulationRng};
}
}
Controller::Controller(Scenario scenario, std::shared_ptr<ownedPreparationAudit::Context> audit)
	: scenario(scenario), audit(std::move(audit)) { require(this->audit != nullptr, "Missing actual simulation audit"); }
Controller::~Controller() = default;
void Controller::initBattleInterface(std::shared_ptr<Environment> env, std::shared_ptr<CBattleCallback> cb)
{
	require(!environment && !callback && env && cb && cb->getPlayerID() == PlayerColor(0), "Expected first ordinary P0 installation");
	environment = std::move(env); callback = std::move(cb); playerID = PlayerColor(0); human = false;
}
bool Controller::awaitOwnedPreparationObservation(BattleID battle, uint32_t unit, int32_t serial)
{
	if(prepared || batch || assertions || done())
	{ fail("Observation must precede all owned preparation"); return false; }
	if(!audit->awaitReady(battle, unit, serial))
	{ fail("No matching quiescent simulation interval before owned capture"); return false; }
	return true;
}
void Controller::cancelOwnedPreparationObservation(const std::string & reason)
{
	audit->cancel(reason); // Completed evidence is unchanged; pending waits wake.
}
void Controller::prepareOwnedRecipientDecision(const std::shared_ptr<CBattleInfoCallback> & source,
	std::shared_ptr<const battle::OwnedBattleFrameBatch> frames)
{
	try
	{
		// This method runs ONLY in Session's real physical state/root/native lease.
		require(!prepared && !batch && !assertions && source && frames && environment && callback,
			"Expected exactly one actual under-lease preparation");
		const auto actual = callback->getBattle(frames->battle());
		require(actual.get() == source.get() && !actual.owner_before(source) && !source.owner_before(actual)
			&& source->getPlayerID() == PlayerColor(0) && frames->observer() == PlayerColor(0),
			"Prepared source is not this registered controller's exact callback owner");
		const auto & target = frames->frame(frames->activeUnitId())->unit();
		require(target.unitOwner() == PlayerColor(0) && target.unitSide() == BattleSide::ATTACKER
			&& target.unitType()->getJsonKey() == "core:angel" && target.getCount() == 2,
			"Integration requires the actual first P0 Angel2 activation");
		// Required companion contract: this constructor consumes all native inputs
		// NOW and closes its source/environment before returning. Main's legacy
		// constructor does not satisfy that contract and MUST NOT be substituted.
		prepared = std::make_shared<HypotheticBattle>(environment.get(), source, frames);
		assertions = std::make_shared<Assertions>(scenario, target.unitId()); batch = std::move(frames);
		callback.reset(); environment.reset(); // No native callback/Environment survives in this controller.
	}
	catch(const std::exception & error) { fail(error.what()); throw; }
}
void Controller::activeStackWithOwnedFrames(const BattleID & battle, const CStack * legacy,
	std::shared_ptr<const battle::OwnedBattleFrameBatch> frames)
{
	try
	{
		require(!legacy && prepared && assertions && frames == batch && frames->battle() == battle && !done(),
			"Unlocked consumer requires its exact prepared owned decision and no legacy actor");
		require(audit->awaitReady(battle, frames->activeUnitId(), frames->activationSerial()), "Simulation audit did not establish the exact quiescent interval");
		assertions->onOwnedRoot(prepared, frames);
		const auto evidence = assertions->evidence();
		require(evidence.status == Status::PROJECTION_VERIFIED,
			evidence.issues.empty() ? "Original owned Dust oracle did not complete" : evidence.issues.front().c_str());
		prepared.reset(); projectionVerified = true;
	}
	catch(const std::exception & error) { fail(error.what()); }
}
void Controller::completeOwnedPreparationObservation()
{
	try
	{
		require(projectionVerified && audit->clientSourceVerified(), "Missing actual client source/capture/analysis observation");
		audit->analysisComplete(); completed = true;
	}
	catch(const std::exception & error) { fail(error.what()); }
}
void Controller::fail(const std::string & reason)
{
	{ std::lock_guard lock(mutex); if(failure.empty()) failure = reason; }
	audit->cancel(reason); completed = true;
}
void Controller::activeStack(const BattleID &, const CStack *) { fail("Legacy dispatch cannot qualify genuine owned preparation"); }
void Controller::yourTacticPhase(const BattleID &, int) { fail("Fixture must begin without a Tactics phase"); }
std::string Controller::problem() const { std::lock_guard lock(mutex); return failure; }
AssertionEvidence Controller::finish() const
{
	const auto values = audit->observations();
	if(!assertions || !values) return {};
	auto result = assertions->finish(observation(values->first), observation(values->second));
	if(values->first.simulationRng.empty() || values->first.simulationRng != values->second.simulationRng)
	{
		result.status = Status::FAILED;
		result.issues.emplace_back("Quiescent authoritative simulation RNG is empty or changed");
	}
	return result;
}
}
