/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "BattleSide.h"
#include "../constants/EntityIdentifiers.h"
#include <cstdint>
#include <string_view>

class CGHeroInstance;
class CBattleInfoCallback;
class JsonNode;
class ServerCallback;
namespace newHorizonsHeroes { struct LeadershipSlotCapacity; }
namespace battle { class Unit; }

namespace newHorizonsElementalAttunement
{
inline constexpr auto SKILL_ID = "new-horizons:elementalRebirth";
inline constexpr auto POLICY_KEY = "elementalAttunementPolicy";
inline constexpr uint8_t ALL_ELEMENTS = 15;

/// Immutable hero-side inputs. The four battle-owned activation flags are separate.
struct DLL_LINKAGE Profile
{
	int rank = 0;
	bool magmaWeapons = false;
	bool stormBolts = false;
	bool elementalConduit = false;
	bool waterVeil = false;
	bool greaterEssence = false;
	bool stoneboundMagic = false;
	int stoneboundBasisPoints = 0;
	bool primalVigilance = false;
	bool elementalCataclysm = false;
	bool operator==(const Profile &) const = default;
	void validate() const;
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validate();
		h & rank; h & magmaWeapons; h & stormBolts; h & elementalConduit; h & waterVeil;
		h & greaterEssence; h & stoneboundMagic; h & stoneboundBasisPoints;
		h & primalVigilance; h & elementalCataclysm;
		if(!h.saving) validate();
	}
};

/// Computed recipient facts; neither a buff record nor an additional combat counter.
struct UnitPassives
{
	bool attuned = false;
	int meleeDamagePercent = 0;
	int rangedDamagePercent = 0;
	int spellReductionBasisPoints = 0;
	int physicalReductionBasisPoints = 0;
	int maximumHealthPercent = 0;
	int additionalRetaliations = 0;
	bool operator==(const UnitPassives &) const = default;
};

DLL_LINKAGE bool usesRules(const JsonNode & capturedPerkRules);
DLL_LINKAGE bool usesRules(const CGHeroInstance * hero);
DLL_LINKAGE void validatePolicy(const JsonNode & capturedPerkRules);
DLL_LINKAGE void validatePolicySerialization(const JsonNode & capturedPerkRules, bool supported);
DLL_LINKAGE void validateSpellMetadata(const JsonNode & capturedMagicRules);
DLL_LINKAGE bool hasSpellMetadata(const JsonNode & capturedMagicRules);
DLL_LINKAGE void validateSpellMetadataSerialization(const JsonNode & capturedMagicRules, bool supported);

/// Air=1, Fire=2, Water=4, Earth=8; shared with SpellDamageElement, not Schools.
DLL_LINKAGE uint8_t spellElementMask(const JsonNode & capturedMagicRules, SpellID spell);
DLL_LINKAGE bool elementalAreaDamage(const JsonNode & capturedMagicRules, SpellID spell);
DLL_LINKAGE uint8_t creatureFamilyMask(CreatureID creature);
DLL_LINKAGE bool eligibleCreature(CreatureID creature);
DLL_LINKAGE uint8_t activatedMask(uint8_t priorMask, uint8_t elementMask);
DLL_LINKAGE uint8_t completedHeroCastMask(const CBattleInfoCallback & battle,
	BattleSide side, SpellID spell, uint8_t priorMask);
/// Called only after a validated, nonautomatic owning Hero cast has resolved.
DLL_LINKAGE void publishCompletedHeroCast(ServerCallback & server,
	const CBattleInfoCallback & battle, BattleSide side, SpellID spell);
DLL_LINKAGE int stoneboundReductionBasisPoints(int finalSpellPower);
/// Extra physical damage routes only; ordinary attacks already use their payload.
DLL_LINKAGE int64_t stoneboundPhysicalDamage(int64_t ordinary, int reductionBasisPoints);
DLL_LINKAGE Profile captureProfile(const CGHeroInstance * hero);
DLL_LINKAGE UnitPassives passivesForCreatureKey(const Profile & profile,
	uint8_t activeElements, std::string_view creatureKey);
DLL_LINKAGE UnitPassives passivesForCreature(const Profile & profile,
	uint8_t activeElements, CreatureID creature);
DLL_LINKAGE int spellDamageBonusPercent(const Profile & profile,
	const JsonNode & capturedMagicRules, SpellID spell);
DLL_LINKAGE bool perfectConvergence(const CGHeroInstance * hero);
DLL_LINKAGE uint32_t maximumHealth(const battle::Unit & unit, int ordinary);
DLL_LINKAGE void applyLeadershipPolicy(newHorizonsHeroes::LeadershipSlotCapacity & capacity,
	const CGHeroInstance & hero, CreatureID creature);
}
