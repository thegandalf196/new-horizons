/*
 * ProjectedUnitEnvironmentState.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include <array>
#include "SylvanLuckState.h"
#include "BattleSide.h"
#include "../GameConstants.h"
#include "../json/JsonNode.h"

class IBattleInfo;
namespace battle
{
/// One private copy of existing environment-visible side state. No extra event
/// counter or automatic gameplay transitions; the future world owner must apply
/// the ORIGINAL sidecar events at their original packet/entry boundaries.
class DLL_LINKAGE ProjectedUnitEnvironmentState
{
public:
	struct SideInputs
	{
		PlayerColor color;
		bool ammoArtifact = false;
		bool veteranCohesion = false;
		int bloodrageRank = 0;
		int bloodrageCap = 0;
		int bloodrageSpeed = 0;
		int bloodrageRetaliations = 0;
		int bloodragePain = 0;
	};
	struct SideState
	{
		SylvanLuckState fortune;
		int bloodrageDamagePercent = 0;
	};
private:
	BattleSideArray<SideInputs> captured;
	BattleSideArray<SideState> states;
	JsonNode capturedRules;
	ProjectedUnitEnvironmentState() = default;
public:
	/// Source is the authoritative, full-info battle, not a hidden player view.
	/// Capture may allocate/throw before any live or projected unit is mutated.
	static ProjectedUnitEnvironmentState capture(const IBattleInfo & source);
	const SideInputs & inputs(BattleSide side) const;
	const SideState & state(BattleSide side) const;
	SideState & state(BattleSide side);
	const JsonNode & magicRules() const;
};
}
