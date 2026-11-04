/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "../battle/BattleSide.h"
#include <cstdint>

class CGHeroInstance;
class JsonNode;

namespace newHorizonsMetamagic
{
inline constexpr int32_t MAXIMUM_MARKS = 5;
inline constexpr int32_t QUALIFYING_MARKS = 3;
inline constexpr int32_t MAXIMUM_POINTS = 12;

/// Read-only facts captured from one hero's own saved Skill/perk registry.
/// These are not another saved resource, action allowance or bonus graph.
struct DLL_LINKAGE Profile
{
	int32_t rank = 0;
	bool pointPolicy = false;
	bool arcaneCatalyst = false;
	bool arcaneShield = false;
	bool manaConduit = false;
	bool arcaneFeedback = false;
	bool spellFracture = false;
	bool arcaneWellspring = false;
	bool arcaneAttunement = false;
	bool arcaneEcho = false;
	bool arcaneSingularity = false;
	bool arcaneConjuration = false;
	bool lingeringMagic = false;
	bool manaAttunement = false;
	bool arcaneAmplification = false;
	bool residualMagic = false;
	bool arcaneSaturation = false;
	bool arcaneVulnerability = false;
	bool arcaneFortification = false;
	bool perfectImprint = false;
	bool arcanePerfection = false;
	int32_t adeptAdditionalMarks = 0;
	int32_t adeptAdditionalPoints = 0;
	int32_t wisdomEffectivenessBonus = 0; // Immutable cast-only additive contribution.
};

/// One cast's immutable hero-side facts. Recipient counters stay in UnitState.
struct DLL_LINKAGE CastProfile
{
	Profile perks;
	const CGHeroInstance * hero = nullptr;
	BattleSide heroSide = BattleSide::NONE;
	int32_t points = 0;
	bool singularityReached = false;
	int32_t chaosShieldLimitBasisPoints = 8000; // Actual originating hero's Paradox cap.
};

/// Absence retains the historical additional-action implementation.
DLL_LINKAGE bool usesPoints(const JsonNode & capturedPerkRules);
DLL_LINKAGE bool usesArcaneMarks(const JsonNode & capturedPerkRules);
DLL_LINKAGE Profile profile(const CGHeroInstance * hero);

/// Current-policy scalar rules. Completion gains exclude every automatic cast.
DLL_LINKAGE int32_t pointsAtRoundStart(const Profile & profile, int32_t previous, int32_t round);
DLL_LINKAGE int32_t pointsAfterResolvedSpell(const Profile & profile, int32_t previous,
	bool ownHero, bool manualHeroCast);
DLL_LINKAGE int32_t manaConduitRecovery(const Profile & profile, int32_t points);
DLL_LINKAGE int32_t attunementInitiative(const Profile & profile, int32_t points);
DLL_LINKAGE int64_t arcaneShieldCapacity(const Profile & profile, int32_t points, int64_t maximumAggregateHealth);
DLL_LINKAGE int32_t singularityManaCost(int32_t ordinary, const Profile & profile, bool reached);
DLL_LINKAGE int64_t echoDamage(int64_t originalDamage);
DLL_LINKAGE int32_t castCounter(const CastProfile & cast, int32_t recipientMarks);

/// All queries use marks that existed BEFORE this spell resolves.
DLL_LINKAGE int32_t effectivenessBonusPercent(const Profile & profile, int32_t marks);
DLL_LINKAGE int32_t marksAfterResolvedSpell(const Profile & profile, int32_t marks);
DLL_LINKAGE int32_t initialMarks(const Profile & profile);
DLL_LINKAGE int32_t summonedMarks(const Profile & profile);
DLL_LINKAGE int32_t additionalDuration(const Profile & profile, int32_t marks);
DLL_LINKAGE int32_t resistanceReductionPoints(const Profile & profile, int32_t marks);
DLL_LINKAGE int32_t enemySpellDamageReductionPercent(const Profile & profile, int32_t marks);

/// Ordinary integer floors are retained; overflow saturates the value domain.
DLL_LINKAGE int64_t scaleMagnitude(int64_t ordinary, const Profile & profile, int32_t marks);
DLL_LINKAGE int64_t spellDamageOutput(int64_t ordinary, const Profile & profile, int32_t marks,
	int32_t defensiveReductionPercent, int64_t offensiveDamageCap, int32_t additionalBonusPercent = 0);
DLL_LINKAGE int32_t scaleSignedBonus(int32_t ordinary, const Profile & profile, int32_t marks);
/// Zero/negative duration sentinels remain unchanged. The +2 rider is added last.
DLL_LINKAGE int32_t scaleDuration(int32_t ordinary, const Profile & profile, int32_t marks);

/// Exact 15% * qualifying/all fraction, followed by ordinary final upward Mana
/// rounding. Zero-target/zero-cost casts remain unchanged; no percent is floored.
DLL_LINKAGE int32_t adjustedManaCost(int32_t ordinary, const Profile & profile,
	uint32_t qualifying, uint32_t affected);
/// Scalar monotonic search only: no target enumeration, bonuses or simulation.
/// Includes surcharge before the same final discount. -1 means even zero is unaffordable.
DLL_LINKAGE int32_t maximumAffordableOvercharge(int32_t availableMana, int32_t ordinaryBaseCost,
	int32_t formulaMaximum, const Profile & profile, uint32_t qualifying, uint32_t affected,
	bool singularityReached = false);
}
