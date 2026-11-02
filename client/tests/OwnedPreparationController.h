/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "OwnedPreparationAssertions.h"
#include "../../lib/callback/CBattleGameInterface.h"
#include "../../server/tests/OwnedPreparationAudit.h"
#include <atomic>
class HypotheticBattle;
namespace ownedPreparationIntegration
{
/// Dedicated integration executable only. This is the ACTUAL registered willing
/// P0 controller, initialized by ordinary CClient interface installation. It has
/// no factory/session/import API and never submits a gameplay action.
class DLL_LINKAGE Controller final : public CBattleGameInterface
{
	const Scenario scenario;
	std::shared_ptr<ownedPreparationAudit::Context> audit;
	std::shared_ptr<Environment> environment; // Only before synchronous preparation.
	std::shared_ptr<CBattleCallback> callback; // Only before synchronous preparation.
	std::shared_ptr<HypotheticBattle> prepared;
	std::shared_ptr<Assertions> assertions;
	std::shared_ptr<const battle::OwnedBattleFrameBatch> batch;
	std::atomic<bool> completed = false, projectionVerified = false;
	mutable std::mutex mutex;
	std::string failure;
	void fail(const std::string & reason);
public:
	Controller(Scenario scenario, std::shared_ptr<ownedPreparationAudit::Context> audit);
	~Controller() override;
	bool usesOwnedRecipientPreparation() const override { return true; }
	void initBattleInterface(std::shared_ptr<Environment>, std::shared_ptr<CBattleCallback>) override;
	bool awaitOwnedPreparationObservation(BattleID battle, uint32_t unit, int32_t serial) override;
	void cancelOwnedPreparationObservation(const std::string & reason) override;
	void completeOwnedPreparationObservation() override;
	void prepareOwnedRecipientDecision(const std::shared_ptr<CBattleInfoCallback> & source,
		std::shared_ptr<const battle::OwnedBattleFrameBatch> frames) override;
	void activeStackWithOwnedFrames(const BattleID &, const CStack *,
		std::shared_ptr<const battle::OwnedBattleFrameBatch> frames) override;
	void activeStack(const BattleID &, const CStack *) override;
	void yourTacticPhase(const BattleID &, int) override;
	bool done() const { return completed.load(); }
	std::string problem() const;
	AssertionEvidence finish() const;
};
}
