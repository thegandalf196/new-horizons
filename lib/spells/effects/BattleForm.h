/*
 * BattleForm.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include "Effect.h"

#include "../../battle/BattleHex.h"

#include <vector>

namespace spells
{
namespace effects
{

/// Replaces a battle unit's effective creature form while preserving its identity and Health state.
class DLL_LINKAGE BattleFormEffect final : public Effect
{
	int32_t duration = 2;

	bool hasUsableForms(const Mechanics * mechanics, const battle::Unit * unit) const;
	bool hasLegalPlacementForEveryForm(const Mechanics * mechanics, const battle::Unit * unit) const;

public:
	struct DLL_LINKAGE BattleFormCandidate
	{
		CreatureID creature;
		BattleHex landing;
	};

	std::vector<BattleFormCandidate> formsForTarget(const Mechanics * mechanics, const battle::Unit * unit) const;
	int32_t getDuration() const { return duration; }

	void adjustAffectedHexes(BattleHexArray & hexes, const Mechanics * mechanics, const Target & spellTarget) const override;
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override;
	bool applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const override;
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override;
	Target filterTarget(const Mechanics * mechanics, const Target & target) const override;
	Target transformTarget(const Mechanics * mechanics, const Target & aimPoint, const Target & spellTarget) const override;

protected:
	void initImpl(JsonNode data) override;
};

}
}
