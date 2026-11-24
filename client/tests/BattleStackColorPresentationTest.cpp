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

	const auto & concealedLook = battleStackColorPresentation::CONCEALED_LOOK;
	require(ordinary.overlayTransparency == 0 && filtered.overlayTransparency == 0,
		"No concealment means no second draw");
	const auto concealed = battleStackColorPresentation::appearance(target, "new-horizons:livingArmor", false, {}, &concealedLook);
	require(concealed.color == blue && concealed.transparency == 204
		&& concealed.overlayColor == concealedLook.overlayColor && concealed.overlayTransparency == 115 * 204 / 255,
		"Concealment keeps the creature's colours at about 80% opacity with a green layer at about 45%");
	const auto concealedSpell = battleStackColorPresentation::appearance(target, "core:skeletonWarrior", false, filters, &concealedLook);
	require(concealedSpell.color == lastColor && concealedSpell.transparency == 204 * 128 / 255 * 64 / 255
		&& concealedSpell.overlayTransparency == 0,
		"A spell colour wins over the green layer while every transparency still multiplies");
	const std::array<BattleStackFilterEffect, 1> fade{{{target, nullptr, transparent, 128, false}}};
	const auto concealedFade = battleStackColorPresentation::appearance(target, "core:skeletonWarrior", false, fade, &concealedLook);
	require(concealedFade.overlayTransparency == 115 * (128 * 204 / 255) / 255,
		"A transparency-only filter keeps the green layer and fades it with the stack");
	const auto concealedFrozen = battleStackColorPresentation::appearance(target, "core:skeletonWarrior", true, {}, &concealedLook);
	require(concealedFrozen.color == blue && concealedFrozen.transparency == 204 && concealedFrozen.overlayTransparency == 0,
		"Frozen keeps last color precedence over concealment");

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
	const std::array<SideInBattle::GatedDemonicStack, 2> arrivedGates{{
		{7, CreatureID(0), 6, true},
		{8, CreatureID(0), 20, false}
	}};
	const auto reinforcement = newHorizonsCreatureAbilityHelp::demonicGateReinforcement(7, arrivedGates);
	require(reinforcement && reinforcement->compact == "Temporary reinforcement"
		&& reinforcement->details == "Arrived through a Demonic Gate. Disappears after combat and cannot open Gates.",
		"An actual temporary arrived Gate record produces the combat-long reinforcement card");
	require(!newHorizonsCreatureAbilityHelp::demonicGateReinforcement(8, arrivedGates),
		"Historical owned-reserve arrival of the same creature must not claim temporary reinforcement");
	require(!newHorizonsCreatureAbilityHelp::demonicGateReinforcement(9, arrivedGates),
		"An unrelated stack must not borrow another stack's Gate provenance");
	require(!newHorizonsCreatureAbilityHelp::demonicGateReinforcement(7, {}),
		"Absent arrived records must not infer Gate reinforcement from other stack properties");
	std::cout << "Battle stack color, summon lifetime and Gate reinforcement presentation PASS\n";
}
