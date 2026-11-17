/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ForecastNativeReadView.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/OwnedUnitStateLoanSource.h"
#include "../../lib/battle/UnitEnvironmentQueries.h"
#include "../../lib/battle/UnitOwnershipQuery.h"
#include "../../lib/battle/UnitMagicResistanceQuery.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
#include "../../lib/bonuses/ProjectedBonusView.h"
#include "../../lib/bonuses/BonusDependencyQueryFrame.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/OwnedFrozenScoringSource.h"
namespace forecast
{
class NativeReadView::Group final : public std::enable_shared_from_this<Group>
{
public:
	class Info final : public battle::IUnitInfo
	{
		const NativeReadDescriptor & data;
	public:
		explicit Info(const NativeReadDescriptor & data) : data(data) {}
		int32_t unitBaseAmount() const override { return data.identity.baseAmount; }
		uint32_t unitId() const override { return data.identity.id; }
		BattleSide unitSide() const override { return data.identity.side; }
		PlayerColor unitOwner() const override { return data.identity.owner; }
		SlotID unitSlot() const override { return data.identity.slot; }
		const CCreature * unitType() const override { return data.creatures->creature(data.identity.originalCreature); }
		const battle::UnitCreatureResolver * unitCreatureResolver() const override { return data.creatures.get(); }
		std::optional<battle::UnitPreparationHealthBasis> unitPreparationHealthBasis() const override { return data.healthBasis; }
	};
	class Receiver;
	class Context final : public battle::IUnitEnvironment, public battle::UnitEnvironmentContext,
		public battle::UnitOwnershipQuery, public battle::UnitMagicResistanceQuery,
		public battle::OwnedUnitStateLoanSource
	{
		Group & group;
		const Receiver * replacement;
		battle::UnitEnvironmentQueries queries;
	public:
		Context(Group &, const Receiver * replacement = nullptr);
		const battle::OwnedUnitStateLoanSource * ownedUnitStateLoanSource() const noexcept override { return this; }
		bool available() const override { return true; }
		void requireSubject(const battle::Unit &) const override;
		const battle::UnitOwnershipQuery & ownership() const override { return *this; }
		battle::Units unitsIf(const battle::UnitFilter &) const override;
		PlayerColor sidePlayer(BattleSide) const override;
		bool sideHasAmmoArtifact(BattleSide) const override;
		bool sideHasVeteranCohesion(BattleSide) const override;
		SylvanLuckState fortune(BattleSide) const override;
		int bloodrageDamage(BattleSide) const override;
		int bloodrageRank(BattleSide) const override;
		int bloodrageCap(BattleSide) const override;
		int bloodrageSpeed(BattleSide) const override;
		int bloodrageRetaliations(BattleSide) const override;
		int bloodragePain(BattleSide) const override;
		int magicResistance(const battle::Unit *) const override;
		newHorizonsNecromancy::CombatPassives necromancyPassives(BattleSide) const override;
		newHorizonsNecromancy::CombatPassives filterNecromancyPassives(
			const newHorizonsNecromancy::CombatPassives &, CreatureID) const override;
		newHorizonsElementalAttunement::Profile elementalAttunementProfile(BattleSide) const override;
		uint8_t elementalAttunementMask(BattleSide) const override;
		bool knowsElementalAttunementProfile(BattleSide) const override;
		const JsonNode & magicRules() const override;
		bool unitHasAmmoCart(const battle::Unit *) const override;
		PlayerColor unitEffectiveOwner(const battle::Unit *) const override;
		std::optional<int> unitMagicResistance(const battle::Unit *) const override;
		int unitSylvanTacticsSpeed(const battle::Unit *, int turn) const override;
		int unitSylvanTacticsInitiativePercent(const battle::Unit *) const override;
		int unitFortuneSpeed(const battle::Unit *) const override;
		int unitSpeedBonus(const battle::Unit *) const override;
		int unitWarfareStatBonus(const battle::Unit *) const override;
		bool unitSlowAffectsSpeed(const battle::Unit *) const override;
		newHorizonsNecromancy::CombatPassives unitNecromancyPassives(const battle::Unit *) const override;
		int unitAdditionalRetaliations(const battle::Unit *) const override;
		newHorizonsElementalAttunement::UnitPassives unitElementalAttunement(const battle::Unit *) const override;
		int unitBloodragePainIncrement(const battle::Unit *) const override;
		bool unitHasVeteranCohesion(const battle::Unit *) const override;
		std::optional<std::pair<int32_t, int32_t>> unitMoraleLimits(const battle::Unit *) const override;
		std::optional<std::pair<int32_t, int32_t>> unitLuckLimits(const battle::Unit *) const override;
		std::shared_ptr<battle::CUnitState> acquireOwnedState(const battle::CUnitState &) const override;
	protected:
		battle::Units adjacentUnits(const battle::Unit &) const override;
		bool newHorizonsRulesActive() const override;
	};
	class Receiver
	{
		friend class NativeReadView;
		class Provider final : public ProjectedBonusView
		{
			friend class NativeReadView;
			Receiver & receiver;
			mutable std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> frame;
		public:
			Provider(Receiver &, std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame>);
			const battle::Unit * evaluatedUnit() const override;
			const battle::Unit * completeOwnedFormSource() const override { return evaluatedUnit(); }
			void onBattleFormChanged(const battle::CUnitState &) const override;
			TConstBonusListPtr getAllBonuses(const CSelector &, const std::string &) const override;
			TConstBonusListPtr getUnstackedBonuses(const CSelector &) const override;
			bool mayHaveBonusType(BonusType) const override;
			int32_t getTreeVersion() const override { return 0; }
			std::vector<Bonus> sourceLocalSpellEffects() const override;
			std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> current() const;
			const std::shared_ptr<const frozenScoring::OwnedSource> & scoring() const;
		} provider;
	public:
		Group & group;
		size_t slot;
		bool ready = false;
		bool stateConstructed = false;
		Info info;
		std::unique_ptr<battle::CUnitStateDetached> state;
		std::unique_ptr<Context> loanContext;
		Receiver(Group &, size_t, std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame>);
		void restore(const battle::UnitStateValueSnapshot &, bool loan);
		std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> frame() const { return provider.current(); }
		bool owns(const battle::Unit &) const noexcept;
	};
	struct Tables
	{
		std::vector<NativeReadOccurrence> occurrences;
		std::shared_ptr<const NativeReadEnvironment> environment;
		Tables(std::vector<NativeReadOccurrence>, std::shared_ptr<const NativeReadEnvironment>);
		struct SealedSuccessor {};
		Tables(std::vector<NativeReadOccurrence>, std::shared_ptr<const NativeReadEnvironment>, SealedSuccessor);
	};
	std::shared_ptr<const Tables> tables;
	const std::vector<NativeReadOccurrence> & occurrences;
	const std::shared_ptr<const NativeReadEnvironment> & environment;
	std::vector<int32_t> retaliationCaches;
	mutable std::vector<std::unique_ptr<Receiver>> views;
	mutable NativeReadWork counters;
	std::shared_ptr<NativeReadWork> decisionWork;
	Context context;
	Group(std::vector<NativeReadOccurrence>, std::shared_ptr<const NativeReadEnvironment>,
		std::shared_ptr<NativeReadWork>);
	Group(std::shared_ptr<const Tables>, std::shared_ptr<NativeReadWork>);
	explicit Group(const Group &); // Explicit branch fork; no native receiver/provider/HP clone.
	int32_t currentRetaliationCache(size_t) const;
	std::shared_ptr<const battle::UnitStateValueSnapshot> currentValues(size_t) const;
	const Receiver & view(size_t) const;
	std::shared_ptr<battle::CUnitState> loan(const battle::CUnitState &, size_t) const;
	std::shared_ptr<battle::CUnitState> loanSnapshot(const battle::UnitStateValueSnapshot &, size_t,
		std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame>) const;
	const battle::ProjectedUnitEnvironmentState::SideInputs & inputs(BattleSide) const;
	const battle::ProjectedUnitEnvironmentState::SideState & sideState(BattleSide) const;
	void record(uint64_t NativeReadWork::* field) const noexcept;
};
}
