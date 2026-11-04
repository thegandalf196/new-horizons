/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../GameConstants.h"
#include "BattleHex.h"
#include "BattleSide.h"
#include "../constants/EntityIdentifiers.h"
#include <optional>
#include <vector>

class BattleAction;
class CBattleInfoCallback;
class CGHeroInstance;
class JsonNode;
namespace battle { class Unit; }

namespace newHorizonsDemonicGating
{
inline constexpr const char * POLICY_KEY = "demonicGatingPolicy";
inline constexpr int POLICY_VERSION = 2;

/// Immutable result of native legality and captured-policy evaluation. It
/// changes no state, reserves no hex, consumes no action and draws no RNG.
struct GatePlan
{
	CreatureID creature;
	TQuantity count = 0;
	BattleHex position;
	uint32_t sourceUnitId = 0;
	int32_t arrivalRound = 0;
	bool temporary = false;
	bool atEndOfRound = false;
	bool chainGateAccelerated = false;
	bool freeActivation = false;
};

DLL_LINKAGE bool usesTemporaryGating(const CGHeroInstance * hero);
/// Pre-gate movement belongs only to historical captured policies.
DLL_LINKAGE bool usesMobileGate(const CGHeroInstance * hero);
DLL_LINKAGE bool usesTemporaryGating(const JsonNode & capturedPerkRules);
DLL_LINKAGE void validatePolicy(const JsonNode & capturedPerkRules);
DLL_LINKAGE void validatePolicySerialization(const JsonNode & capturedPerkRules, bool supported);
/// The source's current type/count and original body identity are authoritative.
DLL_LINKAGE bool eligibleOriginalStack(const CBattleInfoCallback & battle,
	const battle::Unit & source, BattleSide side);
DLL_LINKAGE std::vector<CreatureID> availableTypes(const CBattleInfoCallback & battle,
	const battle::Unit & source, BattleSide side);
DLL_LINKAGE TQuantity summonedCount(const CGHeroInstance & hero, TQuantity currentCount);
DLL_LINKAGE int placementRange(const CGHeroInstance & hero);
DLL_LINKAGE bool footprintHasObstacle(const CBattleInfoCallback & battle,
	BattleHex position, bool doubleWide, BattleSide side);
/// Complete pre-commit request validation, including a declared Mobile Gate
/// path. Optional no-movement uses the ordinary one-destination representation.
DLL_LINKAGE std::optional<GatePlan> plan(const CBattleInfoCallback & battle, const BattleAction & action);
}
