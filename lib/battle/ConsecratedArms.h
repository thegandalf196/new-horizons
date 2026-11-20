/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../Global.h"
#include <cstdint>
#include <array>
#include <optional>
#include <vector>

class CGHeroInstance;
class JsonNode;
struct Bonus;
namespace battle { class Unit; }

namespace consecratedArms
{
inline constexpr const char * SKILL = "new-horizons:divineMandate";
enum class Prayer : uint8_t
{
	DIVINE_PURPOSE, DAUNTLESS_ASSAULT, STEADFAST_SOUL,
	VICARIOUS_REDEMPTION, UNBROKEN_VOW, HEAVENS_JUDGMENT
};
enum class Perk : uint16_t
{
	ENDURING_PRAYER = 1 << 0, JOYFUL_DEVOTION = 1 << 1,
	FEARLESS_ADVANCE = 1 << 2, UNSHAKABLE_FAITH = 1 << 3,
	COMMUNION_OF_SAINTS = 1 << 4, RIGHTEOUS_SENTENCE = 1 << 5,
	FERVENT_INVOCATION = 1 << 6, SACRED_ACCORD = 1 << 7,
	ANTIPHONAL_PRAYER = 1 << 8, LIVING_LITURGY = 1 << 9
};
struct DLL_LINKAGE Policy
{
	int rank = 0;
	uint16_t perks = 0;
	int rankFaithBonus = 0;
	int ferventAdditionalFaith = 0;
	/// Captured version 4: the commander picks the prayer and its one recipient.
	/// Version 3 keeps its uniformly random prayers for every Order recipient.
	bool chosen = false;
	bool has(Perk perk) const;
	void validate() const;
};
struct DLL_LINKAGE Values
{
	int attributes = 0;
	int moraleLuck = 0;
	int retaliationReductionBP = 0;
	int resistanceBP = 0;
	int redemptionConversionBP = 0;
	int casualtyCeilingBP = 10000;
	int judgmentBP = 0;
};
struct DLL_LINKAGE ActivePrayer
{
	Prayer prayer;
	int potencyFaith = 0;
	int remainingRounds = 0;
	Policy policy;
};
/// Captured versions 3 (random prayers) and 4 (chosen prayers).
DLL_LINKAGE bool enabled(const JsonNode & rules);
DLL_LINKAGE bool chosenPrayers(const JsonNode & rules);
DLL_LINKAGE Policy capturePolicy(const CGHeroInstance * hero);
DLL_LINKAGE int actualFaith(const CGHeroInstance & hero);
DLL_LINKAGE int potencyFaith(int actualFaith, const Policy & policy);
DLL_LINKAGE int duration(int actualFaith, const Policy & policy);
DLL_LINKAGE Values values(int potencyFaith, const Policy & policy, int distinctPrayers);
/// Indices come from inclusive bounded authoritative draws: [0,5], then [0,4].
DLL_LINKAGE std::vector<Prayer> selectPrayers(const Policy & policy, int first, int second = 0);
DLL_LINKAGE const char * name(Prayer prayer);
DLL_LINKAGE const char * textID(Prayer prayer);
DLL_LINKAGE std::optional<Prayer> identity(const Bonus * bonus);
using PrayerSnapshot = std::array<std::optional<ActivePrayer>, 6>;
DLL_LINKAGE PrayerSnapshot activePrayers(const battle::Unit & unit);
DLL_LINKAGE std::optional<ActivePrayer> activePrayer(const battle::Unit & unit, Prayer prayer);
DLL_LINKAGE int activePrayerCount(const battle::Unit & unit);
DLL_LINKAGE Values prayerValues(const battle::Unit & unit, Prayer prayer);
DLL_LINKAGE std::vector<Bonus> prayerBonuses(Prayer prayer, int potencyFaith,
	const Policy & policy, int rounds, int distinctPrayers);
}
