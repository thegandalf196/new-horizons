/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CHeroWindow.h"
#include "CommandPresentation.h"
#include "HeroExperienceBar.h"
#include "SpellPointPresentation.h"
#include "../GameEngine.h"
#include "../render/IRenderHandler.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/pathfinder/NewHorizonsMovement.h"
#include "../../lib/pathfinder/TurnInfo.h"
#include <boost/algorithm/string/replace.hpp>

void CHeroWindow::refreshCommandPresentation()
{
	refreshHero(isActive());
}

void CHeroWindow::refreshLegacyCommandReadout()
{
	const auto command = curHero->getArmyCommand();
	legacyCommandArea->setEnabled(command.has_value());
	legacyLeadershipLabel->setEnabled(command.has_value());
	legacyLeadershipImage->setEnabled(command.has_value());
	if(command)
	{
		legacyCommandArea->text = commandPresentation::commandHelp(*curHero);
		auto label = LIBRARY->generaltexth->translate("new-horizons.command.value");
		boost::replace_all(label, "%COMMAND%", std::to_string(command->effective()));
		legacyCommandArea->hoverText = label;
		legacyLeadershipLabel->setText(label);
	}
}

void CHeroWindow::refreshCommandReadout()
{
	const auto value = curHero->getArmyCommand();
	commandArea->text = commandPresentation::commandHelp(*curHero);
	commandArea->hoverText = commandArea->text;
	commandValue->setText(value ? std::to_string(value->effective()) : "--");
}

void CHeroWindow::alignAttributeReadouts()
{
	const auto centerGroup = [this](const std::shared_ptr<CLabel> & base,
		const std::shared_ptr<CLabel> & growth, int center, int y)
	{
		const int baseWidth = static_cast<int>(base->getWidth());
		const int growthWidth = static_cast<int>(growth->getWidth());
		const int gap = growthWidth ? 4 : 0;
		const int left = center - (baseWidth + gap + growthWidth) / 2;
		base->moveTo(pos.topLeft() + Point(left + baseWidth / 2, y));
		growth->moveTo(pos.topLeft() + Point(left + baseWidth + gap + growthWidth / 2, y));
	};
	for(size_t i = 0; i < primSkillValues.size(); ++i)
		centerGroup(primSkillValues[i], growthValues[i], 277 + static_cast<int>(i) * 82, 80);
}

void CHeroWindow::refreshMiddlePaneReadout()
{
	if(!curHero)
		return;

	// Row 1: Experience
	if(xpProgressBar && expValue)
	{
		const auto curExp = curHero->exp;
		const auto curLvlExp = LIBRARY->heroh->reqExp(curHero->level);
		const auto nextLvlExp = LIBRARY->heroh->reqExp(curHero->level + 1);
		const auto progress = curExp > curLvlExp ? curExp - curLvlExp : 0;
		const auto span = nextLvlExp > curLvlExp ? nextLvlExp - curLvlExp : 1;
		expValue->setText(std::to_string(progress) + " / " + std::to_string(span));
		xpProgressBar->setProgress(static_cast<double>(progress) / static_cast<double>(span));
		if(expArea)
		{
			expArea->text = "Experience: " + std::to_string(curHero->exp)
				+ "\nCurrent level progress: " + std::to_string(progress) + " / " + std::to_string(span)
				+ "\nNext level requires " + std::to_string(nextLvlExp) + " total experience ("
				+ std::to_string(nextLvlExp > curHero->exp ? nextLvlExp - curHero->exp : 0) + " needed).";
			expArea->hoverText = expArea->text;
		}
	}

	// Row 2: Leadership
	refreshCommandReadout();

	// Row 2: Mana
	if(manaValue)
	{
		manaValue->setText(spellPointPresentation::readout(curHero->getManaAvailable(),
			curHero->manaLimit(), curHero->getBufferSpellPoints()));
		if(spellPointsArea)
		{
			spellPointsArea->text = spellPointPresentation::tooltip(curHero->getManaAvailable(),
				curHero->manaLimit(), curHero->getBufferSpellPoints());
			spellPointsArea->hoverText = spellPointsArea->text;
		}
	}

	// Row 2: Mana Regen
	if(manaRegenValue)
	{
		const auto regain = curHero->manaRegain();
		manaRegenValue->setText("+" + std::to_string(regain) + " / day");
		if(manaRegenArea)
		{
			manaRegenArea->text = "Mana Regeneration: +" + std::to_string(regain) + " per day.\nPoints of mana naturally recovered at the start of each day.";
			manaRegenArea->hoverText = manaRegenArea->text;
		}
	}

	// Row 3: Movement
	if(movementValue)
	{
		const bool embarked = curHero->inBoat();
		if(movementDynamicImage)
		{
			movementDynamicImage->setAnimationPath(AnimationPath::builtin(embarked
				? "NH_perk_navigation" : "NH_hero_movement_44"), 0);
		}
		if(movementDynamicLabel)
		{
			movementDynamicLabel->setText(embarked ? "Sea Movement" : "Movement");
		}
		movementValue->setText(std::to_string(curHero->movementPointsRemaining()) + " / " + std::to_string(curHero->movementPointsLimit()));
		if(movementArea)
		{
			movementArea->text = (embarked ? "Sea Movement points remaining / current limit: " : "Movement points remaining / current limit: ")
				+ std::to_string(curHero->movementPointsRemaining()) + " / " + std::to_string(curHero->movementPointsLimit()) + ".";
			const auto landMovement = newHorizonsMovement::currentDailyMovementBreakdown(curHero, false);
			const auto seaMovement = newHorizonsMovement::currentDailyMovementBreakdown(curHero, true);
			if(landMovement && seaMovement)
			{
				const auto signedValue = [](const std::int64_t v)
				{
					return v > 0 ? "+" + std::to_string(v) : std::to_string(v);
				};
				const auto appendLimitBreakdown = [&signedValue, this](const char * label,
					const newHorizonsMovement::DailyMovementBreakdown & breakdown)
				{
					movementArea->text += "\n" + std::string(label) + " maximum: " + std::to_string(breakdown.limit)
						+ ". Inputs in order (including Logistics and other active bonuses): 200 base; base adjustment " + signedValue(breakdown.baseAdjustment)
						+ "; " + signedValue(breakdown.percentageToBase) + "% to base; flat " + signedValue(breakdown.flat)
						+ "; " + signedValue(breakdown.percentageToAll) + "% to all.";
					if(breakdown.lowerBound)
						movementArea->text += " Minimum bound: " + std::to_string(*breakdown.lowerBound) + ".";
					if(breakdown.upperBound)
						movementArea->text += " Maximum bound: " + std::to_string(*breakdown.upperBound) + ".";
				};

				appendLimitBreakdown("Land", *landMovement);
				appendLimitBreakdown("Sea", *seaMovement);
				if(curHero->hasActivePerk("new-horizons:logistics", "new-horizons:logistics.navigation"))
					movementArea->text += "\nSea includes Navigation's +25% in its percent-to-base total.";

				const auto turnInfo = curHero->getTurnInfo(0);
				movementArea->text += "\nSteps: 10 orthogonal / 14 diagonal; final tile cost rounds up after modifiers.";
				movementArea->text += "\nTerrain: x1.40 non-native, x1.80 Desert, x1.00 native. Native means the hero or entire army qualifies; one native stack alone does not suffice.";
				if(turnInfo->hasNewHorizonsPathfinding())
					movementArea->text += " Active Pathfinding halves only the terrain surcharge.";
				if(turnInfo->hasNewHorizonsWayfarer())
					movementArea->text += " Active Wayfarer caps the terrain multiplier at x1.25 after Pathfinding.";
				movementArea->text += "\nRoad: x0.67.";
				if(turnInfo->hasNewHorizonsRoadmaster())
					movementArea->text += " Active Roadmaster further multiplies road cost by x0.75.";
				movementArea->text += "\nSpecial travel: Water Walk/Fly steps use x1.50. Creature Speed is not a Movement modifier.";
			}
			movementArea->hoverText = movementArea->text;
		}
	}

	// Row 3: Vision
	if(visionValue)
	{
		const auto sight = curHero->getSightRadius();
		visionValue->setText(std::to_string(sight));
		if(visionArea)
		{
			visionArea->text = "Vision radius: " + std::to_string(sight) + " tiles.\nScouting distance for clearing fog of war and discovering adventure map objects.";
			visionArea->hoverText = visionArea->text;
		}
	}
}

void CHeroWindow::addCommandIcon()
{
}
