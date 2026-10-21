/*
 * HeroActionSelectionState.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "ProjectedTargetHitFixture.h"
#include "HeroQueueBonusSnapshot.h"
#include "../../lib/bonuses/BonusSelector.h"
#include <sstream>

namespace heroSelectionTest
{
struct StatePart
{
	std::string name;
	std::vector<std::byte> bytes;
	std::string json;
};

// Failure-only description; equality still compares every original byte.
inline std::string firstDifference(const std::vector<StatePart> & before,
	const std::vector<StatePart> & after)
{
	std::ostringstream out;
	for(size_t i = 0; i < std::min(before.size(), after.size()); ++i)
	{
		const auto & a = before[i];
		const auto & b = after[i];
		if(a.name == b.name && a.bytes == b.bytes)
			continue;
		size_t offset = 0;
		while(offset < std::min(a.bytes.size(), b.bytes.size()) && a.bytes[offset] == b.bytes[offset])
			++offset;
		out << "component=" << a.name << " -> " << b.name << " byte=" << offset
			<< " sizes=" << a.bytes.size() << '/' << b.bytes.size();
		if(!a.json.empty() || !b.json.empty())
			out << " JSON before=" << a.json.substr(0, 2048) << " after=" << b.json.substr(0, 2048);
		return out.str();
	}
	if(before.size() != after.size())
		out << "component count=" << before.size() << '/' << after.size();
	return out.str();
}

inline std::vector<std::byte> snapshot(const CBattleInfoCallback & view,
	const GameRandomizer & nativeRng, const CRandomGenerator & fixtureRng,
	std::vector<StatePart> * parts = nullptr)
{
	std::vector<std::byte> bytes;
	if(parts)
		parts->clear();
	const auto append = [&](std::string name, const auto & value, std::string json = "")
	{
		const auto more = projectedHitTest::serialized(value);
		bytes.insert(bytes.end(), more.begin(), more.end());
		if(parts)
			parts->push_back({std::move(name), more, std::move(json)});
	};
	for(const auto * unit : view.battleGetAllUnits(false))
	{
		const auto id = "unit/" + std::to_string(unit->unitId());
		const auto state = unit->acquireState()->save();
		append(id + "/state", state, parts ? state.toCompactString() : std::string{});
		append(id + "/bonuses", heroQueueTest::bonusSnapshots(*unit->getAllBonuses(Selector::all)));
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto id = side == BattleSide::ATTACKER ? "attacker/" : "defender/";
		append(std::string(id) + "orders", view.getBattle()->getHeroOrderStates(side));
		append(std::string(id) + "allowances", view.getBattle()->getHeroActionAllowances(side));
		const auto * hero = view.battleGetFightingHero(side);
		append(std::string(id) + "mana", hero ? hero->getManaAvailable() : int64_t{-1});
	}
	append("round", view.battleGetRound());
	append("active", view.battleActiveUnit() ? view.battleActiveUnit()->unitId() : std::numeric_limits<uint32_t>::max());
	append("nativeRng", nativeRng);
	append("fixtureRng", fixtureRng);
	return bytes;
}
}
