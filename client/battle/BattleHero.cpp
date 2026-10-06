/*
 * BattleHero.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleHero.h"

#include "BattleActionsController.h"
#include "BattleFieldController.h"
#include "BattleInterface.h"
#include "BattleRenderer.h"
#include "HeroInfoWindow.h"

#include "../GameEngine.h"
#include "../gui/CursorHandler.h"
#include "../gui/WindowHandler.h"
#include "render/CAnimation.h"
#include "render/Canvas.h"
#include "render/IRenderHandler.h"
#include "../windows/CSpellWindow.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

#include <cmath>

namespace
{
constexpr size_t CASTING_GLOW_FRAME_COUNT = 8;
constexpr size_t CAST_SPELL_GROUP = static_cast<size_t>(EHeroAnimType::CAST_SPELL);

struct CastingGlowDefinition
{
	std::array<ImagePath, CASTING_GLOW_FRAME_COUNT> frames;
	Point dimensions;
};

const JsonNode * newHorizonsMagicAssets()
{
	static const std::optional<JsonNode> metadata = []() -> std::optional<JsonNode>
	{
		try
		{
			const auto path = JsonPath::builtin("config/newHorizonsMagicAssets.json");
			if(!CResourceHandler::get()->existsResource(path))
				return std::nullopt;
			return JsonNode(path);
		}
		catch(const std::exception &)
		{
			return std::nullopt;
		}
	}();

	return metadata ? &*metadata : nullptr;
}

bool readPositiveDimension(const JsonNode & value, int & result)
{
	if(!value.isNumber())
		return false;

	const double number = value.Float();
	if(!std::isfinite(number) || std::floor(number) != number || number <= 0 || number > 4096)
		return false;

	result = static_cast<int>(number);
	return true;
}

std::optional<CastingGlowDefinition> loadCastingGlowDefinition(const std::string & defKey, const std::string & school)
{
	const auto * metadata = newHorizonsMagicAssets();
	if(!metadata || !metadata->isStruct())
		return std::nullopt;

	const auto & definitions = (*metadata)["castingGlows"];
	if(!definitions.isStruct())
		return std::nullopt;
	const auto & heroDefinition = definitions[defKey];
	if(!heroDefinition.isStruct())
		return std::nullopt;
	const auto & entry = heroDefinition[school];
	if(!entry.isStruct())
		return std::nullopt;
	const auto & framePaths = entry["frames"];
	const auto & dimensions = entry["dimensions"];
	if(!framePaths.isVector() || framePaths.Vector().size() != CASTING_GLOW_FRAME_COUNT
		|| !dimensions.isVector() || dimensions.Vector().size() != 2)
		return std::nullopt;

	CastingGlowDefinition result;
	int width = 0;
	int height = 0;
	if(!readPositiveDimension(dimensions.Vector()[0], width) || !readPositiveDimension(dimensions.Vector()[1], height))
		return std::nullopt;
	result.dimensions = Point(width, height);

	for(size_t frame = 0; frame < CASTING_GLOW_FRAME_COUNT; ++frame)
	{
		if(!framePaths.Vector()[frame].isString() || framePaths.Vector()[frame].String().empty())
			return std::nullopt;

		const auto path = ImagePath::fromJson(framePaths.Vector()[frame]);
		// Match the renderer's image lookup order for mounted module Images.
		if(path.empty() || (!CResourceHandler::get()->existsResource(path.addPrefix("SPRITES/"))
			&& !CResourceHandler::get()->existsResource(path.addPrefix("DATA/"))
			&& !CResourceHandler::get()->existsResource(path)))
			return std::nullopt;
		result.frames[frame] = path;
	}

	return result;
}

std::optional<std::string> sourceDefKey(const ImageLocator & locator)
{
	if(locator.image || !locator.defFile)
		return std::nullopt;

	std::string defKey = locator.defFile->getName();
	const auto separator = defKey.rfind('/');
	if(separator != std::string::npos)
		defKey.erase(0, separator + 1);

	if(defKey.ends_with(".DEF"))
		defKey.resize(defKey.size() - 4);

	static constexpr std::array<std::string_view, 18> supportedDefs = {
		"CH00", "CH01", "CH02", "CH03", "CH04", "CH05", "CH06", "CH07", "CH08",
		"CH09", "CH010", "CH11", "CH012", "CH013", "CH014", "CH015", "CH16", "CH17"
	};
	if(std::ranges::find(supportedDefs, defKey) == supportedDefs.end())
		return std::nullopt;

	return defKey;
}
}

const CGHeroInstance * BattleHero::instance() const
{
	return hero;
}

void BattleHero::setCastingGlowSchool(std::optional<std::string> school)
{
	activeCastingGlow.reset();
	if(!school || animation->size(CAST_SPELL_GROUP) != CASTING_GLOW_FRAME_COUNT)
		return;

	const auto firstLocator = animation->getImageLocator(0, CAST_SPELL_GROUP);
	const auto defKey = sourceDefKey(firstLocator);
	if(!defKey)
		return;

	const CastingGlowKey key{*defKey, *school};
	if(const auto found = castingGlowCache.find(key); found != castingGlowCache.end())
	{
		activeCastingGlow = found->second;
		return;
	}
	if(unavailableCastingGlows.contains(key))
		return;

	const auto definition = loadCastingGlowDefinition(key.first, key.second);
	if(!definition)
	{
		unavailableCastingGlows.insert(key);
		return;
	}

	const auto overlays = std::make_shared<CastingGlowFrames>();
	for(size_t frame = 0; frame < CASTING_GLOW_FRAME_COUNT; ++frame)
	{
		const auto source = animation->getImageLocator(frame, CAST_SPELL_GROUP);
		const auto frameDefKey = sourceDefKey(source);
		if(!frameDefKey || *frameDefKey != key.first || source.defGroup != static_cast<int>(CAST_SPELL_GROUP)
			|| source.defFrame != static_cast<int>(frame))
		{
			unavailableCastingGlows.insert(key);
			return;
		}

		const auto baseFrame = animation->getImage(frame, CAST_SPELL_GROUP, false);
		if(!baseFrame || baseFrame->dimensions() != definition->dimensions)
		{
			unavailableCastingGlows.insert(key);
			return;
		}

		ImageLocator overlayLocator(definition->frames[frame], EImageBlitMode::SIMPLE);
		overlayLocator.verticalFlip = defender;
		auto overlay = ENGINE->renderHandler().loadImage(overlayLocator);
		if(!overlay || overlay->dimensions() != definition->dimensions)
		{
			unavailableCastingGlows.insert(key);
			return;
		}
		(*overlays)[frame] = std::move(overlay);
	}

	activeCastingGlow = castingGlowCache.emplace(key, overlays).first->second;
}

void BattleHero::tick(uint32_t msPassed)
{
	size_t groupIndex = static_cast<size_t>(phase);

	float timePassed = msPassed / 1000.f;

	flagCurrentFrame += currentSpeed * timePassed;
	currentFrame += currentSpeed * timePassed;

	if(flagCurrentFrame >= flagAnimation->size(0))
		flagCurrentFrame -= flagAnimation->size(0);

	if(currentFrame >= animation->size(groupIndex))
	{
		currentFrame -= animation->size(groupIndex);
		switchToNextPhase();
	}
}

void BattleHero::render(Canvas & canvas)
{
	size_t groupIndex = static_cast<size_t>(phase);

	auto flagFrame = flagAnimation->getImage(flagCurrentFrame, 0, true);
	auto heroFrame = animation->getImage(currentFrame, groupIndex, true);

	Point heroPosition = pos.center() - parent->pos.topLeft() - heroFrame->dimensions() / 2;
	Point flagPosition = pos.center() - parent->pos.topLeft() - flagFrame->dimensions() / 2;

	if(defender)
		flagPosition += Point(-4, -41);
	else
		flagPosition += Point(4, -41);

	canvas.draw(flagFrame, flagPosition);
	canvas.draw(heroFrame, heroPosition);

	if(phase == EHeroAnimType::CAST_SPELL && activeCastingGlow)
	{
		const auto glowFrame = static_cast<size_t>(currentFrame);
		if(glowFrame < activeCastingGlow->size())
		{
			const auto & overlay = (*activeCastingGlow)[glowFrame];
			if(overlay && overlay->dimensions() == heroFrame->dimensions())
				canvas.draw(overlay, heroPosition);
		}
	}
}

void BattleHero::pause()
{
	currentSpeed = 0.f;
}

void BattleHero::play()
{
	//H3 speed: 10 fps ( 100 ms per frame)
	currentSpeed = 10.f;
}

float BattleHero::getFrame() const
{
	return currentFrame;
}

void BattleHero::collectRenderableObjects(BattleRenderer & renderer)
{
	auto hex = defender ? BattleHex(GameConstants::BFIELD_WIDTH-1) : BattleHex(0);

	renderer.insert(EBattleFieldLayer::HEROES, hex, [this](BattleRenderer::RendererRef canvas)
	{
		render(canvas);
	});
}

void BattleHero::onPhaseFinished(const std::function<void()> & callback)
{
	phaseFinishedCallback = callback;
}

void BattleHero::setPhase(EHeroAnimType newPhase)
{
	nextPhase = newPhase;
	switchToNextPhase(); //immediately switch to next phase and then restore idling phase
	nextPhase = EHeroAnimType::HOLDING;
}

void BattleHero::heroLeftClicked()
{
	if(owner.actionsController->heroSpellcastingModeActive()) //we are casting a spell
		return;

	if(!hero || !owner.makingTurn())
		return;

	const auto castProblem = owner.getBattle()->battleCanCastSpell(hero, spells::Mode::HERO);
	if(castProblem == ESpellCastProblem::OK || castProblem == ESpellCastProblem::CASTS_PER_TURN_LIMIT)
	{
		ENGINE->cursor().set(Cursor::Map::POINTER);
		ENGINE->windows().createAndPushWindow<CSpellWindow>(hero, owner.getCurrentPlayerInterface());
	}
}

void BattleHero::heroRightClicked() const
{
	if(settings["battle"]["stickyHeroInfoWindows"].Bool())
		return;

	Point windowPosition;
	if(ENGINE->screenDimensions().x < 1000)
	{
		windowPosition.x = (!defender) ? owner.fieldController->pos.left() + 1 : owner.fieldController->pos.right() - 79;
		windowPosition.y = owner.fieldController->pos.y + 135;
	}
	else
	{
		windowPosition.x = (!defender) ? owner.fieldController->pos.left() - 93 : owner.fieldController->pos.right() + 15;
		windowPosition.y = owner.fieldController->pos.y;
	}

	InfoAboutHero targetHero;
	if(owner.makingTurn() || settings["session"]["spectate"].Bool())
	{
		const auto * h = defender ? owner.defendingHeroInstance : owner.attackingHeroInstance;
		targetHero.initFromHero(h, InfoAboutHero::EInfoLevel::INBATTLE);
		ENGINE->windows().createAndPushWindow<HeroInfoWindow>(targetHero, &windowPosition);
	}
}

void BattleHero::switchToNextPhase()
{
	phase = nextPhase;
	if(phase != EHeroAnimType::CAST_SPELL)
		activeCastingGlow.reset();
	currentFrame = 0.f;

	auto copy = phaseFinishedCallback;
	phaseFinishedCallback.clear();
	copy();
}

BattleHero::BattleHero(const BattleInterface & owner, const CGHeroInstance * hero, bool defender)
	: defender(defender)
	, hero(hero)
	, owner(owner)
	, phase(EHeroAnimType::HOLDING)
	, nextPhase(EHeroAnimType::HOLDING)
	, currentSpeed(0.f)
	, currentFrame(0.f)
	, flagCurrentFrame(0.f)
{
	AnimationPath animationPath;

	if(!hero->getHeroType()->battleImage.empty())
		animationPath = hero->getHeroType()->battleImage;
	else if(hero->gender == EHeroGender::FEMALE)
		animationPath = hero->getHeroClass()->imageBattleFemale;
	else
		animationPath = hero->getHeroClass()->imageBattleMale;

	animation = ENGINE->renderHandler().loadAnimation(animationPath, EImageBlitMode::WITH_SHADOW);

	pos.w = 64;
	pos.h = 136;
	pos.x = owner.fieldController->pos.x + (defender ? (owner.fieldController->pos.w - pos.w) : 0);
	pos.y = owner.fieldController->pos.y;

	if(defender)
		animation->verticalFlip();

	if(defender)
		flagAnimation = ENGINE->renderHandler().loadAnimation(AnimationPath::builtin("CMFLAGR"), EImageBlitMode::COLORKEY);
	else
		flagAnimation = ENGINE->renderHandler().loadAnimation(AnimationPath::builtin("CMFLAGL"), EImageBlitMode::COLORKEY);

	flagAnimation->playerColored(hero->tempOwner);

	switchToNextPhase();
	play();

	addUsedEvents(TIME);
}
