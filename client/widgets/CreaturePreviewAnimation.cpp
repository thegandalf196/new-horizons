/*
 * CreaturePreviewAnimation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "Images.h"
#include "render/CAnimation.h"
#include "../../lib/CRandomGenerator.h"

CCreatureAnim::CCreatureAnim(int x, int y, const AnimationPath & name, ui8 flags, ECreatureAnimType type):
	CShowableAnim(x, y, name, flags | CREATURE_MODE, 100, size_t(type)) // H3 uses 100 ms per frame, irregardless of battle speed settings
{
	xOffset = 0;
	yOffset = 0;
}

void CCreatureAnim::loopPreview(bool warMachine)
{
	std::vector<ECreatureAnimType> available;

	static const ECreatureAnimType creaPreviewList[] = {
		ECreatureAnimType::HOLDING,
		ECreatureAnimType::HITTED,
		ECreatureAnimType::DEFENCE,
		ECreatureAnimType::ATTACK_FRONT,
		ECreatureAnimType::SPECIAL_FRONT
	};
	static const ECreatureAnimType machPreviewList[] = {
		ECreatureAnimType::HOLDING,
		ECreatureAnimType::MOVING,
		ECreatureAnimType::SHOOT_UP,
		ECreatureAnimType::SHOOT_FRONT,
		ECreatureAnimType::SHOOT_DOWN
	};

	auto & previewList = warMachine ? machPreviewList : creaPreviewList;

	for (auto & elem : previewList)
		if (anim->size(size_t(elem)))
			available.push_back(elem);

	size_t rnd = CRandomGenerator::getDefault().nextInt((int)available.size() * 2 - 1);

	if (rnd >= available.size())
	{
		ECreatureAnimType type;
		if ( anim->size(size_t(ECreatureAnimType::MOVING)) == 0 )//no moving animation present
			type = ECreatureAnimType::HOLDING;
		else
			type = ECreatureAnimType::MOVING;

		//display this anim for ~1 second (time is random, but it looks good)
		for (size_t i=0; i< 12/anim->size(size_t(type)) + 1; i++)
			addLast(type);
	}
	else
		addLast(available[rnd]);
}

void CCreatureAnim::addLast(ECreatureAnimType newType)
{
	auto currType = ECreatureAnimType(group);

	if (currType != ECreatureAnimType::MOVING && newType == ECreatureAnimType::MOVING)//starting moving - play init sequence
	{
		queue.push( ECreatureAnimType::MOVE_START );
	}
	else if (currType == ECreatureAnimType::MOVING && newType != ECreatureAnimType::MOVING )//previous anim was moving - finish it
	{
		queue.push( ECreatureAnimType::MOVE_END );
	}
	if (newType == ECreatureAnimType::TURN_L || newType == ECreatureAnimType::TURN_R)
		queue.push(newType);

	queue.push(newType);
}

void CCreatureAnim::reset()
{
	//if we are in the middle of rotation - set flag
	if (group == size_t(ECreatureAnimType::TURN_L) && !queue.empty() && queue.front() == ECreatureAnimType::TURN_L)
		rotate(true);
	if (group == size_t(ECreatureAnimType::TURN_R) && !queue.empty() && queue.front() == ECreatureAnimType::TURN_R)
		rotate(false);

	while (!queue.empty())
	{
		ECreatureAnimType at = queue.front();
		queue.pop();
		if (set(size_t(at)))
			return;
	}
	if  (callback)
		callback();
	while (!queue.empty())
	{
		ECreatureAnimType at = queue.front();
		queue.pop();
		if (set(size_t(at)))
			return;
	}
	set(size_t(ECreatureAnimType::HOLDING));
}

void CCreatureAnim::startPreview(bool warMachine)
{
	callback = std::bind(&CCreatureAnim::loopPreview, this, warMachine);
}

void CCreatureAnim::clearAndSet(ECreatureAnimType type)
{
	while (!queue.empty())
		queue.pop();
	set(size_t(type));
}
