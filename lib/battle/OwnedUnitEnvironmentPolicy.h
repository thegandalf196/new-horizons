/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "ProjectedUnitEnvironmentState.h"
#include <memory>
class CBattleInfoCallback;
namespace battle
{
class OwnedBattleFrameCapture;
class OwnedUnitEnvironment;
class CommittedElementalEffects;
/// INTERNAL immutable dependencies of canonical unit-environment formulas.
/// No Hero pointer, callback, world, policy/state getter or candidate metadata.
/// UNKNOWN HeroKnowledge is stored separately by the observer's Batch.
class DLL_LINKAGE OwnedUnitEnvironmentPolicy final : boost::noncopyable
{
	friend class OwnedUnitEnvironment;
	friend class OwnedBattleFrameCapture;
	BattleSideArray<ProjectedUnitEnvironmentState::SideInputs> inputs;
	BattleSideArray<ProjectedUnitEnvironmentState::SideState> states;
	/// Only admitted exact-subject queries can consume these current numeric effects.
	std::shared_ptr<const CommittedElementalEffects> committedElementalEffects;

	explicit OwnedUnitEnvironmentPolicy(const CBattleInfoCallback & observer);
	static std::shared_ptr<const OwnedUnitEnvironmentPolicy> capture(const CBattleInfoCallback & observer);
public:
	~OwnedUnitEnvironmentPolicy();
};
}
