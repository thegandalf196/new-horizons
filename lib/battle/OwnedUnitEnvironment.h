/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "UnitEnvironmentQueries.h"
#include "ProjectedUnitEnvironmentState.h"
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <vector>
class ProjectedBonusRoster;
namespace battle
{
class OwnedUnitBonusFrame;
class OwnedUnitEnvironmentPolicy;
class CUnitStateDetached;
/// Pure copied metadata + retained ordered owned-unit references. Not live admission.
/// Full frame/recipe owner must retain this environment through every descendant loan.
class DLL_LINKAGE OwnedUnitEnvironment final : public IUnitEnvironment, boost::noncopyable
{
public:
	struct Facts
	{
		BattleSideArray<std::optional<PlayerColor>> colors; // Presence is distinct from real NEUTRAL/defaultPlayerColor.
		BattleSideArray<std::optional<ProjectedUnitEnvironmentState::SideInputs>> inputs;
		BattleSideArray<std::optional<ProjectedUnitEnvironmentState::SideState>> states;
		std::optional<std::set<CreatureID>> skeletonUpgrades; // Exact direct-upgrade set, not transitive family.
		std::optional<SecondarySkill> necromancyLuckSource; // Numeric metadata, not Hero policy or an imported entity.
		std::optional<std::pair<int32_t, int32_t>> legacyLuckLimits; // Captured engine vector lengths, including concrete zero.
		std::optional<std::pair<int32_t, int32_t>> legacyMoraleLimits; // Captured engine vector lengths; zero is meaningful.
		std::optional<JsonNode> magicRules; // Engaged null is genuine legacy rules; disengaged is unknown.
		/// Opaque engine-only canonical unit-query dependency owner. Its presence
		/// grants no Hero metadata or source/capture authority; no public getters.
		std::shared_ptr<const OwnedUnitEnvironmentPolicy> enginePolicy;
		struct LegacyTimeStopIdentity
		{
			std::optional<SpellID> canonical; // Engaged domain + absent identity means canonical definition is known absent.
			std::map<SpellID, bool> classified; // Exact admitted IDs only; never a boolean snapshot of current stasis.
		};
		std::optional<LegacyTimeStopIdentity> legacyTimeStop; // Disengaged is UNBOUND, never absent/false.
	};
	/// References must point to independently owned units, never alias live nodes.
	/// This pure constructor is not proof of capture authority or source independence.
	OwnedUnitEnvironment(std::vector<std::shared_ptr<const Unit>> units, Facts facts);
	~OwnedUnitEnvironment();
	ProjectedUnitEnvironmentState::SideState & state(BattleSide side);
	std::optional<bool> unitLegacyTimeStopSpellMatch(const Unit * unit, SpellID spell) const override;
	bool unitHasAmmoCart(const Unit * unit) const override;
	PlayerColor unitEffectiveOwner(const Unit * unit) const override;
	std::optional<int> unitMagicResistance(const Unit * unit) const override;
	int unitSylvanTacticsSpeed(const Unit * unit, int turn = 0) const override;
	int unitSylvanTacticsInitiativePercent(const Unit * unit) const override;
	int unitFortuneSpeed(const Unit * unit) const override;
	int unitSpeedBonus(const Unit * unit) const override;
	bool unitSlowAffectsSpeed(const Unit * unit) const override;
	newHorizonsNecromancy::CombatPassives unitNecromancyPassives(const Unit * unit) const override;
	int unitAdditionalRetaliations(const Unit * unit) const override;
	int unitBloodragePainIncrement(const Unit * unit) const override;
	bool unitHasVeteranCohesion(const Unit * unit) const override;
	std::optional<std::pair<int32_t, int32_t>> unitMoraleLimits(const Unit * unit) const override;
	std::optional<std::pair<int32_t, int32_t>> unitLuckLimits(const Unit * unit) const override;
private:
	friend class OwnedUnitBonusFrame;
	/// Exact retained source occurrence + info/provider ancestry; not an ID-only permit.
	std::unique_ptr<OwnedUnitEnvironment> forDerivedSubject(const Unit & source,
		const CUnitStateDetached & derived, const IUnitInfo & info) const;
	OwnedUnitEnvironment(std::vector<std::shared_ptr<const Unit>> units, Facts facts, const Unit * derived);
	Units adjacentGeometry(const Unit & geometry) const; // Only the retaining FRAME calls this after exact ancestry validation.
	static Units adjacentInRoster(const ProjectedBonusRoster & roster, const Unit & geometry);
	static const ProjectedUnitEnvironmentState::SideInputs & capturedInputs(const Facts &, BattleSide);
	static const BattleSideArray<ProjectedUnitEnvironmentState::SideState> & capturedStates(const Facts &);
	class Data;
	std::unique_ptr<Data> data;
};
}
