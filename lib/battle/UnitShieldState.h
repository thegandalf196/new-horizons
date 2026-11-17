/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include <array>
#include <cstdint>
#include <optional>

class JsonSerializeFormat;
class JsonNode;
class IBattleState;
struct SetStackEffect;
namespace battle
{
class Unit;
class RedemptionEvent;
enum class ShieldSource : uint8_t { GUARDIAN_SPIRIT, VICARIOUS_REDEMPTION };
enum class DamageClass : uint8_t { UNCLASSIFIED, PHYSICAL, MAGICAL, HEALTH_COST };
enum class DamageOrigin : uint8_t { OTHER, CREATURE_STRIKE, HERO_SPELL, CREATURE_ABILITY, PERIODIC, CREATURE_SPELL };
/// Owned once per logical attack/spell and recipient, shared by every damage rider.
struct DLL_LINKAGE VowCasualtyQuota
{
	uint32_t recipient = 0;
	int32_t initialCount = 0;
	int32_t remainingCasualties = 0;
	static VowCasualtyQuota capture(uint32_t recipient, int32_t count, int casualtyCeilingBasisPoints);
	int64_t maximumBodyDamage(int32_t count, int32_t firstHP, int64_t maximumCreatureHP) const;
	void consume(int32_t before, int32_t after);
};
/// Transient producer facts, independent of the existing casualty-remains provenance.
struct DamageContext
{
	DamageClass damageClass = DamageClass::UNCLASSIFIED;
	DamageOrigin origin = DamageOrigin::OTHER;
	VowCasualtyQuota * vow = nullptr;
	RedemptionEvent * redemption = nullptr;
	bool bypassMagicalAbsorption = false;
};
struct ShieldPoolView
{
	ShieldSource source = ShieldSource::GUARDIAN_SPIRIT;
	int64_t remaining = 0;
	int64_t capacity = 0;
	bool physicalCreatureOnly = false;
};
struct ShieldSnapshot
{
	std::array<ShieldPoolView, 2> pools;
};
/// Redemption has no clock: its prayer marker owns expiry and refresh.
struct DLL_LINKAGE RedemptionShieldState
{
	int64_t remaining = 0;
	int64_t capacity = 0;
	/// Fractional HP in units of 1/20000, shared by full and half conversion.
	int32_t fractionalHP = 0;
	bool initialized = false;
	void clear() noexcept;
	void initialize(int64_t combatStartMaximumHP);
	int64_t gain(int64_t lostMaximumHP, int conversionBasisPoints, bool alliedHalfRate);
	int64_t absorb(int64_t & damage);
	bool empty() const noexcept;
	void validate() const;
	void serializeJson(JsonSerializeFormat & handler);
};
enum class DamageProvenance : uint8_t;
struct ShieldDamagePreview
{
	int64_t postShield = 0;
	int64_t appliedDamage = 0;
	int64_t healthLoss = 0;
	int64_t bodyHealthLoss = 0;
};
/// Pure value query; nullopt means the unit cannot expose an exact cohort casualty limit.
DLL_LINKAGE std::optional<ShieldDamagePreview> previewShieldDamage(const Unit & unit, int64_t incoming,
	DamageProvenance provenance, const DamageContext * context = nullptr, bool bypassTemporaryHP = false);
DLL_LINKAGE bool redemptionCovers(DamageContext damage);
DLL_LINKAGE bool hasConsecratedUnitState(const JsonNode & unit);
DLL_LINKAGE ShieldSnapshot shieldSnapshot(const Unit & unit);
DLL_LINKAGE void synchronizePrayerEffects(IBattleState & state, const SetStackEffect & effects);
}
