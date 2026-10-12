/*
 * NewHorizonsHeroLuck.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsHeroLuck.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "HeroActionAllowanceState.h"
#include "NewHorizonsHeroicSpirit.h"
#include "NewHorizonsDivineMandate.h"

#include "../json/JsonNode.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../IGameSettings.h"
#include "../spells/NewHorizonsMagic.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
int setting(const JsonNode & rules, const char * block, const char * key, int fallback, int maximum)
{
	const auto & node = rules[block][key];
	if(node.isNull())
		return fallback;
	if(!node.isNumber() || !std::isfinite(node.Float()) || node.Float() < 0
		|| node.Float() > maximum || std::floor(node.Float()) != node.Float())
		throw std::runtime_error("Invalid Hero Luck or Morale numeric setting");
	return static_cast<int>(node.Integer());
}
}

namespace newHorizonsHeroLuck
{
void Receipt::validate() const
{
	if(heroValue <= 0 || diceSize <= 0 || baseThreshold <= 0 || baseThreshold > diceSize
		|| draw <= 0 || draw > diceSize || priorBias < std::numeric_limits<int32_t>::min()
		|| priorBias > std::numeric_limits<int32_t>::max() || (procFlags & ~uint8_t{15}) != 0)
		throw std::runtime_error("Invalid Hero Luck or Morale draw receipt");
}

bool Receipt::triggered(int chanceScalePercent) const
{
	validate();
	if(chanceScalePercent < 0 || chanceScalePercent > 1000)
		throw std::runtime_error("Invalid Hero Luck or Morale chance scale");
	if(chanceScalePercent == 0)
		return false;
	const int64_t threshold = std::min<int64_t>(diceSize,
		static_cast<int64_t>(baseThreshold) * chanceScalePercent / 100);
	if(threshold <= 0)
		return false;
	return static_cast<int64_t>(draw) * diceSize - priorBias <= threshold * diceSize;
}

bool hasLuckyDay(const CGHeroInstance * hero)
{
	// The identifier is frozen for compatibility; its authored effect changed.
	return hero && hero->hasActivePerk("new-horizons:luck", "new-horizons:luck.luckyRecovery");
}

bool hasErraticMana(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:luck", "new-horizons:luck.gambler");
}

bool hasDruidicLuck(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:luck", "new-horizons:luck.twistOfFate");
}

bool projectOrderAllowance(const CBattleInfoCallback & battle, BattleSide side,
	const HeroOrderState & order, const DoubleCommandState * doubleCommand, HeroActionAllowanceState & projected)
{
	projected = battle.getBattle()->getHeroActionAllowances(side);
	const auto selected = battle.battleGetOrderActionAllowance(side);
	if(!selected)
		throw std::runtime_error("Order has no accepted action allowance");
	const auto mandate = battle.battleGetDivineMandateStatus(side);
	const bool mandateAvailable = mandate.active && mandate.completedPairs < mandate.maximumPairs;
	const auto filter = [&battle, side, mandateAvailable](const HeroActionAllowanceState::Grant & grant)
	{
		return battle.getBattle()->getCrisisCommandState().allowsGrant(side, grant)
			&& (grant.source != HeroActionAllowanceState::GrantSource::DIVINE_MANDATE || mandateAvailable);
	};
	const auto receipt = projected.consumeAllowance(selected->grantId,
		HeroActionAllowanceState::ActionKind::ORDER, battle.battleGetRound(), filter);
	if(!receipt)
		throw std::runtime_error("Order allowance changed during projection");
	const auto * hero = battle.battleGetFightingHero(side);
	const auto recipients = newHorizonsDivineMandate::needsRecipientCapture(hero)
		? newHorizonsDivineMandate::sharedPurposeOrderRecipients(battle, side, order)
		: std::vector<uint32_t>{};
	DivineMandateTransition::applyAcceptedAction(projected, *receipt, battle.battleGetRound(),
		false, mandate.maximumPairs, recipients);
	if(doubleCommand && !battle.getBattle()->getDoubleCommandState(side).orderPending())
		projected.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
			HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND, battle.battleGetRound());
	const bool eligible = newHorizonsHeroicSpirit::canRollOrderMorale(hero, *receipt);
	if(eligible && !projected.canResolveHeroicSpirit(*receipt))
		throw std::runtime_error("Heroic Spirit Order cannot fit in the remaining action allowance identity space");
	if(eligible)
	{
		// Reserve the possible success on a second detached ledger too, so
		// count/identity constraints fail before RNG even if this draw would fail.
		auto success = projected;
		if(!success.resolveHeroicSpirit(*receipt, true))
			throw std::runtime_error("Heroic Spirit Order capacity cannot be reserved");
	}
	return eligible;
}

bool canRollLuck(const CGHeroInstance & hero, const IGameSettings & settings)
{
	const int value = hero.luckVal();
	const auto curve = settings.getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE);
	if(value <= 0 || curve.empty())
		return false;
	const int threshold = curve[std::min<size_t>(curve.size(), value) - 1];
	const int dice = settings.getInteger(EGameSettings::COMBAT_LUCK_DICE_SIZE);
	return threshold > 0 && dice > 0 && threshold <= dice;
}

bool canRollMorale(const CGHeroInstance & hero, const IGameSettings & settings, const JsonNode & magicRules)
{
	const int value = hero.moraleVal();
	if(value <= 0)
		return false;
	const auto curve = settings.getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE);
	const auto chance = newHorizonsMagic::moraleChance(magicRules, value);
	const auto configuredDice = chance ? newHorizonsMagic::moraleDiceSize(magicRules) : std::nullopt;
	if(!chance && curve.empty())
		return false;
	const int threshold = chance.value_or(curve.empty() ? 0 : curve[std::min<size_t>(curve.size(), value) - 1]);
	const int dice = configuredDice.value_or(settings.getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE));
	return threshold > 0 && dice > 0 && threshold <= dice;
}

void validateLuckContext(const Receipt & receipt, const CGHeroInstance & hero, const IGameSettings & settings)
{
	receipt.validate();
	const int value = hero.luckVal();
	const auto curve = settings.getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE);
	if(value <= 0 || curve.empty() || receipt.heroValue != value
		|| receipt.diceSize != settings.getInteger(EGameSettings::COMBAT_LUCK_DICE_SIZE)
		|| receipt.baseThreshold != curve[std::min<size_t>(curve.size(), value) - 1])
		throw std::runtime_error("Hero Luck receipt differs from its captured actor settings");
}

void validateMoraleContext(const Receipt & receipt, const CGHeroInstance & hero,
	const IGameSettings & settings, const JsonNode & magicRules)
{
	receipt.validate();
	const int value = hero.moraleVal();
	const auto curve = settings.getVector(EGameSettings::COMBAT_GOOD_MORALE_CHANCE);
	const auto chance = newHorizonsMagic::moraleChance(magicRules, value);
	const auto dice = chance ? newHorizonsMagic::moraleDiceSize(magicRules) : std::nullopt;
	if(value <= 0 || (!chance && curve.empty()) || receipt.heroValue != value
		|| receipt.diceSize != dice.value_or(settings.getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE))
		|| receipt.baseThreshold != chance.value_or(curve.empty() ? 0
			: curve[std::min<size_t>(curve.size(), value) - 1]))
		throw std::runtime_error("Hero Morale receipt differs from its captured actor settings");
}

void validateRules(const JsonNode & rules)
{
	for(const char * block : {"heroLuck", "heroMorale"})
	{
		const auto & node = rules[block];
		if(node.isNull())
			continue;
		if(!node.isStruct())
			throw std::runtime_error("Invalid Hero Luck or Morale settings block");
		const std::vector<std::string> required = std::string(block) == "heroMorale"
			? std::vector<std::string>{"heroicSpiritChanceScalePercent"}
			: std::vector<std::string>{"luckyDayChanceScalePercent", "erraticManaChanceScalePercent",
				"druidicLuckChanceScalePercent", "orderMultiplierPercent", "damageMultiplierPercent", "manaRefundPercent"};
		for(const auto & key : required)
			if(!node.Struct().contains(key) || node[key].isNull())
				throw std::runtime_error("Incomplete Hero Luck or Morale settings block");
		for(const auto & [key, value] : node.Struct())
		{
			if(std::string(block) == "heroMorale")
			{
				if(key != "heroicSpiritChanceScalePercent")
					throw std::runtime_error("Unknown Hero Morale setting");
			}
			else if(key != "luckyDayChanceScalePercent" && key != "erraticManaChanceScalePercent"
				&& key != "druidicLuckChanceScalePercent" && key != "orderMultiplierPercent"
				&& key != "damageMultiplierPercent" && key != "manaRefundPercent")
				throw std::runtime_error("Unknown Hero Luck setting");
		}
	}
	for(const auto proc : {Proc::LuckyDay, Proc::ErraticMana, Proc::DruidicLuck, Proc::HeroicSpirit})
		chanceScalePercent(rules, proc);
	orderMultiplierPercent(rules);
	damageMultiplierPercent(rules);
	manaRefundPercent(rules);
}

int chanceScalePercent(const JsonNode & rules, Proc proc)
{
	switch(proc)
	{
	case Proc::LuckyDay: return setting(rules, "heroLuck", "luckyDayChanceScalePercent", 100, 1000);
	case Proc::ErraticMana: return setting(rules, "heroLuck", "erraticManaChanceScalePercent", 100, 1000);
	case Proc::DruidicLuck: return setting(rules, "heroLuck", "druidicLuckChanceScalePercent", 100, 1000);
	case Proc::HeroicSpirit: return setting(rules, "heroMorale", "heroicSpiritChanceScalePercent", 100, 1000);
	}
	throw std::runtime_error("Unknown Hero Luck or Morale proc");
}

int orderMultiplierPercent(const JsonNode & rules) { return setting(rules, "heroLuck", "orderMultiplierPercent", 200, 1000); }
int damageMultiplierPercent(const JsonNode & rules) { return setting(rules, "heroLuck", "damageMultiplierPercent", 200, 1000); }
int manaRefundPercent(const JsonNode & rules) { return setting(rules, "heroLuck", "manaRefundPercent", 50, 100); }

int orderPotency(int value, const JsonNode & rules, bool luckyDay)
{
	if(!luckyDay)
		return value;
	const int64_t scaled = static_cast<int64_t>(value) * orderMultiplierPercent(rules) / 100;
	return static_cast<int>(std::clamp<int64_t>(scaled,
		std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
}
}
