/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "UnitEnvironmentQueries.h"
#include "ProjectedUnitEnvironmentState.h"
#include <memory>
#include <optional>
#include <set>
#include <vector>
namespace battle
{
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
		std::optional<JsonNode> magicRules; // Engaged null is genuine legacy rules; disengaged is unknown.
	};
	/// References must point to independently owned units, never alias live nodes.
	/// This pure constructor is not proof of capture authority or source independence.
	OwnedUnitEnvironment(std::vector<std::shared_ptr<const Unit>> units, Facts facts);
	~OwnedUnitEnvironment();
	ProjectedUnitEnvironmentState::SideState & state(BattleSide side);
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
private:
	class Data;
	std::unique_ptr<Data> data;
};
}
