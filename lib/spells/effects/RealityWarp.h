/*
 * RealityWarp.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "Effect.h"

namespace spells::effects
{
/// Reciprocal exchange of captured effects; never replays their original casts.
class DLL_LINKAGE RealityWarpEffect final : public Effect
{
	bool validPair(const Mechanics * mechanics, const Target & target) const;

public:
	void adjustTargetTypes(std::vector<TargetType> & types, const Mechanics * mechanics) const override;
	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & target) const override;
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override;
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override;
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override;
	Target filterTarget(const Mechanics * mechanics, const Target & target) const override;
	Target transformTarget(const Mechanics * mechanics, const Target & aimPoint, const Target & spellTarget) const override;

protected:
	void initImpl(JsonNode data) override;
};
}
