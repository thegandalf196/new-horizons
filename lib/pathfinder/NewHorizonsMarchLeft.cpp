/*
 * NewHorizonsMarchLeft.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsMarchLeft.h"

#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../mapObjects/army/CStackInstance.h"
#include "../GameLibrary.h"
#include "../texts/CGeneralTextHandler.h"

#include <boost/algorithm/string/replace.hpp>

#include <algorithm>
#include <cstdint>

namespace newHorizonsMarchLeft
{
int marchLeft(int remainingMovement, int fullDailyMovement)
{
	if(fullDailyMovement <= 0)
		return FULL;
	const std::int64_t remaining = std::clamp(remainingMovement, 0, fullDailyMovement);
	// Rounded down, so a hop never hides a step.
	return static_cast<int>(remaining * FULL / fullDailyMovement);
}

int movementAfterReceiving(int remainingMovement, int fullDailyMovement, int marchLeft)
{
	if(marchLeft >= FULL)
		return remainingMovement;
	const std::int64_t left = std::max(0, marchLeft);
	// Rounded up: a hero who takes back troops he marched himself keeps exactly his movement.
	const std::int64_t allowed = (static_cast<std::int64_t>(std::max(0, fullDailyMovement)) * left + FULL - 1) / FULL;
	return static_cast<int>(std::min<std::int64_t>(remainingMovement, allowed));
}

bool appliesTo(const CGHeroInstance & hero)
{
	return hero.usesNewHorizonsMovement();
}

int fullDailyMovement(const CGHeroInstance & hero)
{
	return hero.movementPointsLimit();
}

void recordMarch(CGHeroInstance & hero, int day)
{
	if(!appliesTo(hero))
		return;
	const int left = marchLeft(hero.movementPointsRemaining(), fullDailyMovement(hero));
	if(left >= FULL)
		return;
	for(const auto & slot : hero.Slots())
		slot.second->lowerMarchLeft(day, left);
}

void recordReserveWithdrawal(CGHeroInstance & hero, const std::map<CreatureID, TQuantity> & nextReserve, int day)
{
	if(!appliesTo(hero))
		return;
	const int left = marchLeft(hero.movementPointsRemaining(), fullDailyMovement(hero));
	if(left >= FULL)
		return;
	for(const auto & [creature, count] : hero.getDemonicReserve())
	{
		const auto next = nextReserve.find(creature);
		if(next != nextReserve.end() && next->second >= count)
			continue;
		for(const auto & slot : hero.Slots())
			if(slot.second->getCreatureID() == creature)
				slot.second->lowerMarchLeft(day, left);
	}
}

bool hasMarchedToday(const CGHeroInstance & hero, int day)
{
	return std::ranges::any_of(hero.Slots(), [day](const auto & slot)
	{
		return slot.second->getMarchLeft(day) < FULL;
	});
}

std::optional<int> limitedMovement(const CGHeroInstance & hero, int marchLeft)
{
	if(marchLeft >= FULL || !appliesTo(hero))
		return std::nullopt;
	const int remaining = hero.movementPointsRemaining();
	const int limited = movementAfterReceiving(remaining, fullDailyMovement(hero), marchLeft);
	if(limited >= remaining)
		return std::nullopt;
	return limited;
}

int percentLeft(int marchLeft)
{
	return (std::clamp(marchLeft, 0, FULL) * 100 + FULL / 2) / FULL;
}

std::string withMarchLeft(std::string hoverText, const CStackInstance * stack, int day)
{
	const int left = stack ? stack->getMarchLeft(day) : FULL;
	if(left >= FULL)
		return hoverText;
	auto suffix = LIBRARY->generaltexth->translate("new-horizons.marchLeft.hoverSuffix");
	boost::replace_all(suffix, "%PERCENT%", std::to_string(percentLeft(left)));
	return hoverText + suffix;
}

std::vector<TransferEffect> projectTransfers(const std::vector<StackTransfer> & transfers, int day)
{
	std::map<const CGHeroInstance *, int> received;
	for(const auto & transfer : transfers)
	{
		if(!transfer.from || !transfer.to || transfer.from == transfer.to || !transfer.from->hasStackAtSlot(transfer.fromSlot))
			continue;
		const auto * hero = dynamic_cast<const CGHeroInstance *>(transfer.to);
		if(!hero)
			continue;
		const auto & moving = transfer.from->getStack(transfer.fromSlot);
		int left = moving.getMarchLeft(day);
		if(!transfer.swap && transfer.toSlot.validSlot() && hero->hasStackAtSlot(transfer.toSlot)
			&& hero->getStack(transfer.toSlot).getCreatureID() == moving.getCreatureID())
			left = std::min(left, hero->getStack(transfer.toSlot).getMarchLeft(day));
		const auto [entry, added] = received.try_emplace(hero, left);
		if(!added)
			entry->second = std::min(entry->second, left);
	}
	std::vector<TransferEffect> effects;
	for(const auto & [hero, left] : received)
		if(const auto limited = limitedMovement(*hero, left))
			effects.push_back({hero, left, hero->movementPointsRemaining(), *limited});
	return effects;
}
}
