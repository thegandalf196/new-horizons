/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "OwnedUnitBonusFrame.h"
#include "../bonuses/ProjectedBonusView.h"
#include "../bonuses/RecipientBonusOverlay.h"
namespace battle
{
class OwnedUnitBonusFrame::Data
{
public:
	std::shared_ptr<const OwnedUnitCreatureResolver> creatures;
	Identity identity;
	std::shared_ptr<const OwnedRecipientBonusGraph> graph;
	std::map<CreatureID, FormSources> forms;
	std::vector<size_t> sourceLocalSpells;
	OwnedUnitEnvironment::Facts environment;
	std::vector<std::shared_ptr<const Unit>> peers;
	size_t recipientPosition;
	recipientBonusOverlay::Layer ledger; // Single pending recipient-effect source.
	class Info final : public IUnitInfo
	{
		const Data & data;
	public:
		explicit Info(const Data & data) : data(data) {}
		int32_t unitBaseAmount() const override { return data.identity.baseAmount; }
		uint32_t unitId() const override { return data.identity.id; }
		BattleSide unitSide() const override { return data.identity.side; }
		PlayerColor unitOwner() const override { return data.identity.owner; }
		SlotID unitSlot() const override { return data.identity.slot; }
		const CCreature * unitType() const override { return data.creatures->creature(data.identity.originalCreature); }
		const UnitCreatureResolver * unitCreatureResolver() const override { return data.creatures.get(); }
		std::optional<UnitPreparationHealthBasis> unitPreparationHealthBasis() const override { return data.identity.healthBasis; }
	} info;
	class View final : public ProjectedBonusView
	{
		const Data & data;
	public:
		explicit View(const Data & data) : data(data) {}
		const Unit * evaluatedUnit() const override { return data.subject.get(); }
		const Unit * completeOwnedFormSource() const override { return data.subject.get(); }
		void onBattleFormChanged(const CUnitState & unit) const override;
		TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override;
		TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override;
		TConstBonusListPtr ownedNativeFormBonuses(const CUnitState & form, const CSelector & selector, bool unstacked) const override;
		std::vector<Bonus> sourceLocalSpellEffects() const override;
		int32_t getTreeVersion() const override { return 0; } // Noncacheable explicit query, never native generation.
	} view;
	std::shared_ptr<CUnitStateDetached> subject;
	std::unique_ptr<OwnedUnitEnvironment> env; // Retire before subject/view/info/payload/descriptor owners.
	Data(Identity identity, std::shared_ptr<const OwnedUnitCreatureResolver> creatures,
		std::shared_ptr<const OwnedRecipientBonusGraph> graph, std::map<CreatureID, FormSources> forms,
		std::vector<size_t> spells, OwnedUnitEnvironment::Facts environment,
		std::vector<std::shared_ptr<const Unit>> peers, size_t recipientPosition, recipientBonusOverlay::Layer ledger);
	void validate() const;
	void validateSnapshot(const JsonNode & snapshot) const;
	OwnedRecipientBonusGraph::RootQuery context(const CUnitState & unit, bool native) const;
	BonusList raw(const std::vector<size_t> & records) const;
	TConstBonusListPtr query(const CSelector & selector, bool unstacked) const;
	void requireRecord(size_t index) const;
};
}
