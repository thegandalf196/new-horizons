/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BonusEnum.h"
#include "../battle/BattleBonusAcceptance.h"
#include "../constants/Enumerations.h"
#include <map>
#include "../constants/EntityIdentifiers.h"
#include <optional>
#include <vector>
/// Value-only capture inputs. No source/node/library identities are retained.
struct DLL_LINKAGE OwnedBonusEvaluationFacts
{
	struct ArmySlot
	{
		CreatureID creature;
		FactionID faction;
		int level;
		int count;
	};
	BonusNodeType nodeType = BonusNodeType::UNKNOWN;
	PlayerColor owner = PlayerColor::CANNOT_DETERMINE;
	std::optional<int> heroLevel;
	std::optional<int> stackCount;
	std::optional<int> stableStackLevel;
	std::optional<std::vector<ArmySlot>> armySlots;
	struct CreatureContext
	{
		struct Creature { CreatureID id; int level; EAlignment alignment; };
		std::optional<Creature> creature; // Engaged domain with empty value means genuine absence.
		std::optional<FactionID> faction;
		std::map<CreatureID, bool> familyUpgrades;
		std::map<CreatureID, FactionID> sourceFactions;
	};
	struct RankContext
	{
		struct Instance { bool commander; int rank; };
		std::optional<Instance> instance;
	};
	struct TerrainContext
	{
		std::optional<TerrainId> adventure;
		std::optional<TerrainId> query;
		std::optional<TerrainId> battle;
		std::optional<TerrainId> legacyFallback;
		bool explicitNativeContext = false; // Copied branch fact, never native-read authority.
		std::map<TerrainId, bool> nativeMembership;
	};
	/// Outer absence means unbound; inner absence above means captured NOT_APPLICABLE.
	std::optional<CreatureContext> creatureContext;
	std::optional<RankContext> rankContext;
	std::optional<TerrainContext> terrainContext;
	std::optional<battle::BonusAcceptanceFacts> acceptance;
	/// Required applicable facts cannot be replaced by zero or a live fallback.
	void validate() const;
};
