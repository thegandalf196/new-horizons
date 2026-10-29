/*
 * BattleStackColorPresentationTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "../StdInc.h"
#include "../battle/BattleStackColorPresentation.h"
#include "../windows/NewHorizonsCreatureAbilityHelp.h"

#include <array>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool value, const char * message)
{
	if(!value)
	{
		std::cerr << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}
}

int main()
{
	// Opaque identity tokens: this presentation helper never dereferences units.
	const CStack * target = nullptr;
	const auto otherTarget = reinterpret_cast<const CStack *>(&target);
	const ColorRGBA blue(114, 186, 218, 140);
	const ColorRGBA transparent(0, 0, 0, 0);
	const ColorRGBA firstColor(180, 20, 30, 90);
	const ColorRGBA lastColor(30, 200, 40, 110);
	const auto ordinary = battleStackColorPresentation::appearance(target, "core:skeletonWarrior", false, {});
	require(ordinary.color == transparent && ordinary.transparency == 255,
		"Ordinary creatures retain the untinted opaque baseline");
	const auto armor = battleStackColorPresentation::appearance(target, "new-horizons:livingArmor", false, {});
	require(armor.color == blue && armor.transparency == 255,
		"Living Armor has its blue identity tint without Frozen");
	const std::array<BattleStackFilterEffect, 3> filters{{
		{target, nullptr, firstColor, 128, false},
		{target, nullptr, lastColor, 64, true},
		{otherTarget, nullptr, ColorRGBA(255, 255, 255, 255), 0, false}
	}};
	const auto filtered = battleStackColorPresentation::appearance(target, "new-horizons:livingArmor", false, filters);
	require(filtered.color == lastColor && filtered.transparency == 32,
		"Only matching filters apply, in order, with integer transparency multiplication");
	const std::array<BattleStackFilterEffect, 2> reversed{{filters[1], filters[0]}};
	const auto reverseFiltered = battleStackColorPresentation::appearance(target, "core:skeletonWarrior", false, reversed);
	require(reverseFiltered.color == firstColor && reverseFiltered.transparency == 32,
		"Reversing filter application changes the winning color");
	const auto frozen = battleStackColorPresentation::appearance(target, "core:skeletonWarrior", true, filters);
	require(frozen.color == blue && frozen.transparency == 32,
		"Actual Frozen takes last color precedence without discarding transparency");

	const auto help = [](int rounds)
	{
		return newHorizonsCreatureAbilityHelp::innateAbilities("new-horizons:livingArmor", nullptr, "", "", rounds, "", "");
	};
	require(help(0).empty(), "Combat-long zero lifetime must not create a creature ability row");
	const auto one = help(1);
	const auto three = help(3);
	require(one.size() == 1 && one[0].compact == "Temporary summon: 1 round remaining",
		"One remaining round uses singular wording");
	require(three.size() == 1 && three[0].compact == "Temporary summon: 3 rounds remaining",
		"Three remaining rounds use plural wording");
	require(one[0].details.starts_with(one[0].compact) && three[0].details.starts_with(three[0].compact),
		"Expanded ability help includes the actual remaining lifetime");
	const auto harvest = newHorizonsCreatureAbilityHelp::innateAbilities(
		"new-horizons:ghost", nullptr, "", "", 0, "Soul Harvest +%count%", "Gains %count% Ghost");
	require(harvest.size() == 1 && harvest[0].compact == "Soul Harvest +1"
		&& harvest[0].details == "Gains 1 Ghost" && !harvest[0].iconPath.empty(),
		"Static Ghost help uses the intrinsic growth constant without a hero or captured profile");
	require(!newHorizonsCreatureAbilityHelp::soulHarvest("core:wraith", "", ""),
		"Ordinary Wraiths do not claim Soul Harvest");
	std::cout << "Battle stack color and summon lifetime presentation PASS\n";
}
