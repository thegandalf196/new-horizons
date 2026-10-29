/*
 * CreaturePic.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "MiscWidgets.h"
#include "Images.h"
#include "TextControls.h"
#include "../GameEngine.h"
#include "../CMT.h"
#include "../battle/BattleStackColorPresentation.h"
#include "render/Canvas.h"
#include "render/Colors.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/entities/faction/CTownHandler.h"

CCreaturePic::CCreaturePic(int x, int y, const CCreature * cre, bool Big, bool Animated)
{
	OBJECT_CONSTRUCTION;
	pos.x+=x;
	pos.y+=y;

	auto faction = cre->getFactionID();

	assert(LIBRARY->townh->size() > faction);

	if (cre->animDefName.empty())
	{
		ENGINE->dispatchMainThread([cre]()
		{
			handleFatalError("Creature " + cre->getJsonKey() + " has no valid combat animation!", false);
		});
		return;
	}

	if(Big)
		bg = std::make_shared<CPicture>((*LIBRARY->townh)[faction]->creatureBg130);
	else
		bg = std::make_shared<CPicture>((*LIBRARY->townh)[faction]->creatureBg120);
	anim = std::make_shared<CCreatureAnim>(0, 0, cre->animDefName, CShowableAnim::CREATURE_PREVIEW_MODE);
	anim->clipRect(cre->isDoubleWide()?170:150, 155, bg->pos.w, bg->pos.h);
	anim->setEffectColor(battleStackColorPresentation::appearance(nullptr, cre->getJsonKey(), false, {}).color);
	anim->startPreview(cre->hasBonusOfType(BonusType::SIEGE_WEAPON));

	amount = std::make_shared<CLabel>(bg->pos.w, bg->pos.h, FONT_MEDIUM, ETextAlignment::BOTTOMRIGHT, Colors::WHITE);

	pos.w = bg->pos.w;
	pos.h = bg->pos.h;
}

void CCreaturePic::show(Canvas & to)
{
	// redraw everything in a proper order
	bg->showAll(to);
	anim->show(to);
	amount->showAll(to);
}

void CCreaturePic::setAmount(int newAmount)
{
	if(newAmount != 0)
		amount->setText(std::to_string(newAmount));
	else
		amount->setText("");
}
