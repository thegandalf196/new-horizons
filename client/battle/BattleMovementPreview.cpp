/*
 * BattleMovementPreview.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleMovementPreview.h"
#include "BattleActionsController.h"
#include "BattleInterface.h"
#include "BattleStacksController.h"
#include "../GameInstance.h"

#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/texts/MetaString.h"

#include <algorithm>
#include <set>

static size_t overwatchReactorCount(const CBattleInfoCallback & battle,
	const battle::Unit * mover, const BattleHex & destination)
{
	if(!mover || !destination.isAvailable() || destination == mover->getPosition())
		return 0;
	const auto path = mover->hasBonusOfType(BonusType::FLYING)
		? BattleHexArray{destination} : battle.getPath(mover->getPosition(), destination, mover).first;
	std::set<uint32_t> reactors;
	auto from = mover->getPosition();
	for(size_t index = path.size(); index > 0; --index)
	{
		const auto to = path[index - 1];
		for(const auto id : battle.battleGetOverwatchReactors(mover, from, to))
			reactors.insert(id);
		from = to;
	}
	return reactors.size();
}

std::string battleMovementPreview::overwatchWarning(const CBattleInfoCallback & battle,
	const battle::Unit * mover, const BattleHex & destination)
{
	const auto reactors = overwatchReactorCount(battle, mover, destination);
	if(reactors == 0)
		return {};
	return "\nOverwatch hazard: " + std::to_string(reactors)
		+ " ready shooter(s) may react at 50% damage before arrival.";
}

static std::string previewQuantity(const DamageRange & range, const std::string & textID)
{
	const auto quantity = range.min == range.max ? std::to_string(range.min)
		: std::to_string(range.min) + "-" + std::to_string(range.max);
	auto text = MetaString::createFromTextID(textID + (range.max == 1 ? ".1" : ""));
	text.replaceTokenRawString("%d", quantity);
	return text.toString(&GAME->translator());
}

std::string BattleActionsController::skirmisherStatusMessage(const BattleHex & targetHex)
{
	const auto * attacker = owner.stacksController->getActiveStack();
	if(!skirmisherTargetHex.isValid())
		return "Skirmisher: choose an enemy stack. It will move up to half Speed and shoot at 75% damage.";
	const auto * selectedTarget = owner.getBattle()->battleGetStackByPos(skirmisherTargetHex);
	if(!attacker || !selectedTarget)
		return "Skirmisher: choose a highlighted firing destination.";
	const auto & legalFiringHexes = getSkirmisherLegalFiringHexes();
	if(!vstd::contains(legalFiringHexes, targetHex))
		return "Skirmisher: choose a highlighted firing destination.";
	const auto distance = skirmisherFiringDistancesCache[targetHex.toInt()];
	BattleAttackInfo estimate(attacker, selectedTarget, static_cast<int>(distance), true);
	estimate.attackerPos = targetHex;
	estimate.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;
	int rangedAttacks = attacker->getTotalAttacks(true);
	if(const auto * hero = owner.getBattle()->battleGetFightingHero(attacker->unitSide()))
		rangedAttacks += hero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(attacker->creatureId()));
	if(attacker->shots.isLimited())
		rangedAttacks = std::min(rangedAttacks, attacker->shots.available());
	const int shotsAfterAction = std::max(0, attacker->shots.available() - rangedAttacks);
	const auto estimateResult = owner.getBattle()->battleEstimateDamage(estimate);
	const auto reactors = overwatchReactorCount(*owner.getBattle(), attacker, targetHex);
	const std::string attackCount = std::to_string(rangedAttacks)
		+ (rangedAttacks == 1 ? " shot" : " shots");
	const std::string hazard = reactors == 0 ? std::string()
		: " Overwatch: " + std::to_string(reactors) + " may fire at 50%.";
	return "Move " + std::to_string(distance) + "; " + attackCount + " at 75% damage." + hazard
		+ "\nEst/attack: " + previewQuantity(estimateResult.damage, "vcmi.battleWindow.damageEstimation.damage")
		+ "; " + previewQuantity(estimateResult.kills, "vcmi.battleWindow.damageEstimation.kills")
		+ "; " + previewQuantity({shotsAfterAction, shotsAfterAction}, "vcmi.battleWindow.damageEstimation.shots") + ".";
}
