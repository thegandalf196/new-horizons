/*
 * BattleHero.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "BattleConstants.h"

#include "../gui/CIntObject.h"
#include "../../lib/FunctionList.h"
#include "../../lib/constants/EntityIdentifiers.h"

#include <array>
#include <map>
#include <optional>
#include <set>
#include <string>

class CGHeroInstance;

class CAnimation;
class BattleInterface;
class BattleRenderer;
class IImage;

/// Hero battle animation
class BattleHero : public CIntObject
{
	using CastingGlowKey = std::pair<std::string, std::string>;
	using CastingGlowFrames = std::array<std::shared_ptr<IImage>, 8>;

	bool defender;

	CFunctionList<void()> phaseFinishedCallback;

	std::shared_ptr<CAnimation> animation;
	std::shared_ptr<CAnimation> flagAnimation;
	std::map<CastingGlowKey, std::shared_ptr<const CastingGlowFrames>> castingGlowCache;
	std::set<CastingGlowKey> unavailableCastingGlows;
	std::shared_ptr<const CastingGlowFrames> activeCastingGlow;

	const CGHeroInstance * hero; //this animation's hero instance
	const BattleInterface & owner; //battle interface to which this animation is assigned

	EHeroAnimType phase; //stage of animation
	EHeroAnimType nextPhase; //stage of animation to be set after current phase is fully displayed

	float currentSpeed;
	float currentFrame; //frame of animation
	float flagCurrentFrame;

	void switchToNextPhase();

	void render(Canvas & canvas); //prints next frame of animation to to
public:
	const CGHeroInstance * instance() const;
	/// Select a transient school overlay for the next CAST_SPELL phase, if a supported variant is installed.
	void setCastingGlowSchool(std::optional<std::string> school);

	void setPhase(EHeroAnimType newPhase); //sets phase of hero animation

	void collectRenderableObjects(BattleRenderer & renderer);
	void tick(uint32_t msPassed) override;

	float getFrame() const;
	void onPhaseFinished(const std::function<void()> &);

	void pause();
	void play();

	void heroLeftClicked();
	void heroRightClicked() const;

	BattleHero(const BattleInterface & owner, const CGHeroInstance * hero, bool defender);
};
