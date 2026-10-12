/*
 * NewHorizonsHeroLuck.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../Global.h"
#include <cstdint>
#include <stdexcept>

class CGHeroInstance;
class JsonNode;
class IGameSettings;
class CBattleInfoCallback;
struct HeroOrderState;
struct DoubleCommandState;
struct HeroActionAllowanceState;
enum class BattleSide : int8_t;

namespace newHorizonsHeroLuck
{
enum class Proc : uint8_t
{
	LuckyDay = 1,
	ErraticMana = 2,
	DruidicLuck = 4,
	HeroicSpirit = 8
};

/// One authoritative draw. Mirrors/children inherit it; prediction never rolls.
/// heroValue denotes Luck or Morale according to the containing packet field.
struct DLL_LINKAGE Receipt
{
	int32_t heroValue = 0;
	int32_t diceSize = 0;
	int32_t baseThreshold = 0;
	int32_t draw = 0;
	int64_t priorBias = 0;
	uint8_t procFlags = 0;

	bool triggered(int chanceScalePercent = 100) const;
	bool has(Proc proc) const { return (procFlags & static_cast<uint8_t>(proc)) != 0; }
	void mark(Proc proc) { procFlags |= static_cast<uint8_t>(proc); }
	void validate() const;
	bool operator==(const Receipt &) const = default;

	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validate();
		h & heroValue;
		h & diceSize;
		h & baseThreshold;
		h & draw;
		h & priorBias;
		h & procFlags;
		if(!h.saving)
			validate();
	}
};

DLL_LINKAGE bool hasLuckyDay(const CGHeroInstance * hero);
DLL_LINKAGE bool hasErraticMana(const CGHeroInstance * hero);
DLL_LINKAGE bool hasDruidicLuck(const CGHeroInstance * hero);
DLL_LINKAGE bool canRollLuck(const CGHeroInstance & hero, const IGameSettings & settings);
DLL_LINKAGE bool canRollMorale(const CGHeroInstance & hero, const IGameSettings & settings, const JsonNode & magicRules);
/// Stage all ordinary companion grants before admitting a possible Morale
/// Order. Capacity failure occurs before either RNG draw or authoritative apply.
DLL_LINKAGE bool projectOrderAllowance(const CBattleInfoCallback & battle, BattleSide side,
	const HeroOrderState & order, const DoubleCommandState * doubleCommand, HeroActionAllowanceState & projected);
DLL_LINKAGE void validateLuckContext(const Receipt & receipt, const CGHeroInstance & hero, const IGameSettings & settings);
DLL_LINKAGE void validateMoraleContext(const Receipt & receipt, const CGHeroInstance & hero,
	const IGameSettings & settings, const JsonNode & magicRules);
DLL_LINKAGE void validateRules(const JsonNode & rules);
DLL_LINKAGE int chanceScalePercent(const JsonNode & rules, Proc proc);
DLL_LINKAGE int orderMultiplierPercent(const JsonNode & rules);
DLL_LINKAGE int damageMultiplierPercent(const JsonNode & rules);
DLL_LINKAGE int manaRefundPercent(const JsonNode & rules);
/// Scale an already computed intrinsic Order value; downstream caps remain at consumers.
DLL_LINKAGE int orderPotency(int value, const JsonNode & rules, bool luckyDay);
}
