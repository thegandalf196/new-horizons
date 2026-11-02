/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#if !defined(NH_OWNED_PREPARATION_INTEGRATION) || !NH_OWNED_PREPARATION_INTEGRATION
#error Owned preparation assertions belong only to the dedicated integration executable
#endif
#include "../../AI/BattleAI/OwnedPreparationObserver.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/json/JsonNode.h"
#include <cstddef>
#include <optional>
#include <mutex>
#include <string>
#include <vector>
namespace ownedPreparationIntegration
{
enum class Scenario { ORDERED_REMOVAL, INCOMING_CLOCK };
/// COMPLETE is scoped to this component; it never retires separate native or lifecycle requirements.
enum class Status { NOT_OBSERVED, PROJECTION_VERIFIED, INCOMPLETE, FAILED, COMPLETE };
/// Values captured by the genuine owner in its stable source interval, NOT a
/// callback/world/capability. Empty observations cannot establish native isolation.
/// The harness owns source sampling, epoch/barrier and task completion.
struct SourceObservations
{
	BattleID battle = BattleID::NONE;
	uint32_t target = 0;
	std::optional<JsonNode> nativeState;
	std::optional<bool> nativeTimeStopped;
	std::optional<int> nativeHealingReduction;
	std::optional<std::vector<std::byte>> authoritativeSimulationRng;
};
struct AssertionEvidence
{
	Status status = Status::NOT_OBSERVED;
	std::string originalCase;
	BattleID battle = BattleID::NONE;
	uint32_t target = 0;
	std::optional<JsonNode> capturedState;
	std::vector<std::string> issues;
	std::vector<std::string> coverageNotes;
};
/// Receives ONLY the actual production assertion child and Batch. Does not
/// retain HB/environment/source pointers, seed a Batch, submit actions or read native state.
class Assertions final : public Observer
{
	Scenario scenario;
	uint32_t target = 0;
	AssertionEvidence observed;
	mutable std::mutex evidenceMutex;
public:
	Assertions(Scenario scenario, uint32_t target);
	void onOwnedRoot(const std::shared_ptr<HypotheticBattle> & current,
		const std::shared_ptr<const battle::OwnedBattleFrameBatch> & source) override;
	AssertionEvidence evidence() const;
	/// Harness combines its own before/after observations with completed owned
	/// assertions. Removal requires the actual simulation stream. Fixture-only RNG
	/// remains a separate native-regression oracle; incoming has no RNG oracle.
	AssertionEvidence finish(const SourceObservations & before, const SourceObservations & after) const;
};
}
