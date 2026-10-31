/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "CUnitState.h"
#include "OwnedUnitCreatureResolver.h"
#include "OwnedUnitEnvironment.h"
#include "../bonuses/OwnedRecipientBonusGraph.h"
#include <map>
#include <limits>
namespace battle
{
/// Pure owned fixture/preparation frame; no live-source capture or command dispatch.
/// Unit/BonusList/shared Bonus returns are BORROWS: retain frame/Scratch through all uses.
/// Generic acquireState() descendants also borrow; use Scratch::nested() for retained isolated loans.
class DLL_LINKAGE OwnedUnitBonusFrame final : public std::enable_shared_from_this<OwnedUnitBonusFrame>, boost::noncopyable
{
public:
	struct Identity
	{
		int32_t baseAmount = -1;
		uint32_t id = 0;
		BattleSide side = BattleSide::NONE;
		PlayerColor owner = PlayerColor::CANNOT_DETERMINE;
		SlotID slot = SlotID(-1);
		CreatureID originalCreature = CreatureID::NONE;
		UnitPreparationHealthBasis healthBasis{-1, -1};
	};
	struct FormSources
	{
		OwnedRecipientBonusGraph::RootQuery full;
		OwnedRecipientBonusGraph::RootQuery native; // Exact hypothetical native-form source recipe, not effective values.
	};
	struct Inputs
	{
		Identity identity;
		std::shared_ptr<const OwnedUnitCreatureResolver> creatures;
		std::vector<OwnedRecipientBonusGraph::NodeInput> nodes;
		size_t root = 0;
		std::vector<std::shared_ptr<const Bonus>> extraRecords;
		std::map<CreatureID, FormSources> forms;
		std::vector<size_t> sourceLocalSpells; // Explicit raw export handles, NEVER inferred from effective records.
		OwnedUnitEnvironment::Facts environment;
		/// Complete ordered immutable owned peers; exactly one NULL recipient slot.
		/// Caller must retain each peer's descriptor/bonus/context owners; not live aliases.
		std::vector<std::shared_ptr<const Unit>> environmentUnits;
		size_t recipientPosition = std::numeric_limits<size_t>::max();
		JsonNode state;
	};
	class DLL_LINKAGE Scratch final
	{
		friend class OwnedUnitBonusFrame;
		std::shared_ptr<const OwnedUnitBonusFrame> owner;
		std::unique_ptr<OwnedUnitEnvironment> environment;
		std::shared_ptr<CUnitState> value;
		Scratch(std::shared_ptr<const OwnedUnitBonusFrame> owner,
			std::unique_ptr<OwnedUnitEnvironment> environment, std::shared_ptr<CUnitState> value);
	public:
		Scratch(const Scratch &) = delete;
		Scratch & operator=(const Scratch &) = delete;
		Scratch(Scratch &&) noexcept = default;
		Scratch & operator=(Scratch && other) noexcept;
		CUnitState & unit() const;
		Scratch nested() const;
	};
	static std::shared_ptr<OwnedUnitBonusFrame> fromOwnedInputs(Inputs inputs);
	~OwnedUnitBonusFrame();
	const CUnitState & unit() const;
	JsonNode save() const;
	void load(const JsonNode & snapshot);
	void removeRecords(const std::vector<size_t> & records);
	std::shared_ptr<OwnedUnitBonusFrame> fork() const;
	/// Freezes a private scalar/ledger frame FIRST, so later parent edits cannot leak.
	Scratch scratch() const;
private:
	static Units adjacentEnvironment(const OwnedUnitEnvironment & environment, const Unit & geometry);
	class Data;
	std::unique_ptr<Data> data;
	OwnedUnitBonusFrame() = default;
	static std::unique_ptr<OwnedUnitEnvironment> derivedEnvironment(const OwnedUnitBonusFrame & source, const CUnitStateDetached & derived);
	static BonusList queryGraph(const OwnedRecipientBonusGraph & graph, const OwnedRecipientBonusGraph::RootQuery & query,
		const BonusList & raw, const CSelector & selector, bool unstacked, const Unit & unit);
};
}
