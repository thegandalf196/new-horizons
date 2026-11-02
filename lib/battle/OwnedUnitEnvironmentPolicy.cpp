/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedUnitEnvironmentPolicy.h"
#include "LiveUnitEnvironmentContext.h"
#include "UnitOwnershipQuery.h"
namespace battle
{
OwnedUnitEnvironmentPolicy::OwnedUnitEnvironmentPolicy(const CBattleInfoCallback & observer)
{
	const LiveUnitEnvironmentContext source(observer); // The same real registered callback perspective.
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & input = inputs.at(side);
		input.color = source.ownership().sidePlayer(side);
		input.ammoArtifact = source.sideHasAmmoArtifact(side);
		input.veteranCohesion = source.sideHasVeteranCohesion(side);
		input.necromancy = source.necromancyPassives(side);
		input.bloodrageRank = source.bloodrageRank(side);
		input.bloodrageCap = source.bloodrageCap(side);
		input.bloodrageSpeed = source.bloodrageSpeed(side);
		input.bloodrageRetaliations = source.bloodrageRetaliations(side);
		input.bloodragePain = source.bloodragePain(side);
		auto & state = states.at(side); state.fortune = source.fortune(side);
		state.bloodrageDamagePercent = source.bloodrageDamage(side);
	}
}
std::shared_ptr<const OwnedUnitEnvironmentPolicy> OwnedUnitEnvironmentPolicy::capture(const CBattleInfoCallback & observer)
{
	// Only the real factory invokes this during state/root/native admission.
	// Query dependencies stay private; UNKNOWN HeroKnowledge remains a separate
	// observer value and can never be replaced by these compatibility answers.
	return std::shared_ptr<const OwnedUnitEnvironmentPolicy>(new OwnedUnitEnvironmentPolicy(observer));
}
OwnedUnitEnvironmentPolicy::~OwnedUnitEnvironmentPolicy() = default;
}
