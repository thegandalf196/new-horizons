/*
 * BattleEffectsController.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleEffectsController.h"

#include "BattleAnimationClasses.h"
#include "BattleFieldController.h"
#include "BattleInterface.h"
#include "BattleRenderer.h"
#include "BattleStacksController.h"
#include "BattleWindow.h"
#include "CreatureAnimation.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "media/ISoundPlayer.h"
#include "render/CAnimation.h"
#include "render/Canvas.h"
#include "render/Graphics.h"
#include "render/IRenderHandler.h"
#include "render/IImage.h"

#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../GameInstance.h"
#include "../../lib/logging/CLogger.h"

#include <atomic>

namespace
{
CLogger * heroProcLogger()
{
	static auto * logger = CLogger::getLogger(CLoggerDomain("animation.heroProc"));
	return logger;
}

bool reserveHeroProcTrace()
{
	if(!heroPositiveProcPresentation::traceEnabled())
		return false;
	const auto limit = BattleAnimationTiming::limit();
	static std::atomic<uint64_t> attempts{0};
	const auto index = attempts.fetch_add(1, std::memory_order_relaxed);
	if(index == limit)
		heroProcLogger()->debug("NH_HERO_PROC cap=%u reached; subsequent events counted but not logged", limit);
	return index < limit;
}
}

bool heroPositiveProcPresentation::traceEnabled()
{
	return BattleAnimationTiming::requested() && heroProcLogger()->isDebugEnabled();
}

void heroPositiveProcPresentation::trace(TraceContext & context, const char * event,
	int animation, int frame, int frames, int stage, int x, int y, bool imageAvailable)
{
	if(!reserveHeroProcTrace())
		return;
	const auto now = BattleAnimationTiming::steadyMicroseconds();
	if(!context.scene)
		context.scene = now;
	heroProcLogger()->debug("NH_HERO_PROC event=%s steady_us=%lld scene=%lld ordinal=%llu battle=%d side=%d round=%d sourceKind=%s source=%d flags=%u effect=%d animation=%d frame=%d frames=%d stage=%d pos_x=%d pos_y=%d imageAvailable=%d luckReceiptPresent=%d moraleReceiptPresent=%d luckHeroValue=%d luckBaseThreshold=%d luckDiceSize=%d moraleHeroValue=%d moraleBaseThreshold=%d moraleDiceSize=%d moraleGrantId=%u luckyDayPolicyScale=%d erraticManaPolicyScale=%d druidicLuckPolicyScale=%d heroicSpiritPolicyScale=%d",
		event, now, context.scene, static_cast<unsigned long long>(context.ordinal), context.battle,
		context.side, context.round, context.spell ? "spell" : "order", context.source, context.flags,
		context.effect, animation, frame, frames, stage, x, y, imageAvailable,
		context.luckReceiptPresent,
		context.moraleReceiptPresent,
		context.luckHeroValue,
		context.luckBaseThreshold,
		context.luckDiceSize,
		context.moraleHeroValue,
		context.moraleBaseThreshold,
		context.moraleDiceSize,
		context.moraleGrantId,
		context.luckyDayPolicyScale,
		context.erraticManaPolicyScale,
		context.druidicLuckPolicyScale,
		context.heroicSpiritPolicyScale);
}

BattleEffectsController::BattleEffectsController(BattleInterface & owner):
	owner(owner)
{
	loadColorMuxers();
}

void BattleEffectsController::displayEffect(EBattleEffect effect, const BattleHex & destTile)
{
	displayEffect(effect, AudioPath(), destTile);
}

void BattleEffectsController::displayEffect(EBattleEffect effect, const AudioPath & soundFile, const BattleHex & destTile, float transparencyFactor)
{
	size_t effectID = static_cast<size_t>(effect);

	BattleHexArray tiles;
	tiles.insert(destTile);
	displayAnimation(AnimationPath::builtinTODO(graphics->battleACToDef[effectID][0]), soundFile, tiles, transparencyFactor);
}

void BattleEffectsController::displayAnimation(const AnimationPath & animation, const AudioPath & soundFile, const BattleHexArray & destTiles, float transparencyFactor)
{
	ENGINE->sound().playSound( soundFile );

	owner.stacksController->addNewAnim(new EffectAnimation(owner, animation, destTiles, 0, transparencyFactor));
}

void BattleEffectsController::displayHeroPositiveEffect(EBattleEffect effect, Point anchor,
	const std::optional<heroPositiveProcPresentation::TraceContext> & trace)
{
	if(effect != EBattleEffect::GOOD_LUCK && effect != EBattleEffect::GOOD_MORALE)
		return;

	const auto path = AnimationPath::builtinTODO(graphics->battleACToDef[static_cast<size_t>(effect)][0]);
	const auto animation = ENGINE->renderHandler().loadAnimation(path, EImageBlitMode::SIMPLE);
	const auto first = animation->getImage(0, 0, true);
	if(!first)
	{
		if(trace)
		{
			auto missing = *trace;
			heroPositiveProcPresentation::trace(missing, "unavailable");
		}
		return;
	}

	ENGINE->sound().playSound(AudioPath::builtin(effect == EBattleEffect::GOOD_LUCK ? "GOODLUCK" : "GOODMRLE"));
	auto * effectAnimation = new EffectAnimation(owner, path,
		anchor - first->dimensions() / 2, EffectAnimation::FORCE_ON_TOP);
	effectAnimation->setHeroCueTrace(trace);
	owner.stacksController->addNewAnim(effectAnimation);
}

void BattleEffectsController::battleAnimationPlayed(const BattleAnimationPlayed & pack)
{
	if(pack.animation.empty())
	{
		if(!pack.sound.empty())
			ENGINE->sound().playSound(pack.sound);
		return;
	}

	BattleHexArray tiles;

	for(const auto & target : pack.targets)
	{
		// a unit may have moved since the pack was sent, so its current position wins over the
		// hex recorded back then
		const auto * unit = target.unitID < 0 ? nullptr : owner.getBattle()->battleGetUnitByID(target.unitID);
		const BattleHex & tile = unit ? unit->getPosition() : target.tile;

		if(tile.isValid())
			tiles.insert(tile);
	}

	if(tiles.empty())
		return;

	// queued into the same stage as the hit animations of the pack that follows, so that both start
	// on the same frame instead of one after the other
	if(pack.deferred)
	{
		owner.addToAnimationStage(EAnimationEvents::HIT, [this, animation = pack.animation, sound = pack.sound, tiles, transparency = pack.transparency](){
			displayAnimation(animation, sound, tiles, transparency);
		});
		return;
	}

	owner.checkForAnimations();
	displayAnimation(pack.animation, pack.sound, tiles, pack.transparency);
	owner.waitForAnimations();
}

void BattleEffectsController::battleTriggerEffect(const BattleTriggerEffect & bte)
{
	owner.checkForAnimations();

	const CStack * stack = owner.getBattle()->battleGetStackByID(bte.stackID);
	if(!stack)
	{
		logGlobal->error("Invalid stack ID %d", bte.stackID);
		return;
	}
	//don't show animation when no HP is regenerated
	switch(bte.effect)
	{
		case BonusType::HP_REGENERATION:
			displayEffect(EBattleEffect::REGENERATION, AudioPath::builtin("REGENER"), stack->getPosition(), 0.5);
			break;
		case BonusType::MANA_DRAIN:
			displayEffect(EBattleEffect::MANA_DRAIN, AudioPath::builtin("MANADRAI"), stack->getPosition());
			break;
		case BonusType::POISON:
			displayEffect(EBattleEffect::POISON, AudioPath::builtin("POISON"), stack->getPosition());
			break;
		case BonusType::FEARFUL:
			displayEffect(EBattleEffect::FEAR, AudioPath::builtin("FEAR"), stack->getPosition(), 0.5);
			break;
		case BonusType::MORALE:
		{
			MetaString hlp = MetaString::createFromTextID("core.genrltxt.33");
			hlp.replaceName(stack->unitType()->getId(), stack->getCount());
			displayEffect(EBattleEffect::GOOD_MORALE, AudioPath::builtin("GOODMRLE"), stack->getPosition());
			owner.appendBattleLog(hlp.toString(&GAME->translator()));
			break;
		}
		default:
			return;
	}
	owner.waitForAnimations();
}

void BattleEffectsController::startAction(const BattleAction & action)
{
	owner.checkForAnimations();

	const CStack *stack = owner.getBattle()->battleGetStackByID(action.stackNumber);

	switch(action.actionType)
	{
	case EActionType::WAIT:
		owner.appendBattleLog(stack->formatGeneralMessage(136, &GAME->translator()));
		break;
	case EActionType::BAD_MORALE:
		owner.appendBattleLog(stack->formatGeneralMessage(-34, &GAME->translator()));
		displayEffect(EBattleEffect::BAD_MORALE, AudioPath::builtin("BADMRLE"), stack->getPosition());
		owner.stacksController->addNewAnim(new HittedAnimation(owner, stack)); // H3: unit flinches when it fails morale
		break;
	}

	owner.waitForAnimations();
}

void BattleEffectsController::collectRenderableObjects(BattleRenderer & renderer)
{
	for (auto & elem : battleEffects)
	{
		renderer.insert( EBattleFieldLayer::EFFECTS, elem.tile, [&elem](BattleRenderer::RendererRef canvas)
		{
			int currentFrame = static_cast<int>(floor(elem.currentFrame));
			currentFrame %= elem.animation->size();

			auto img = elem.animation->getImage(currentFrame, static_cast<size_t>(elem.type));
			img->setAlpha(255 * elem.transparencyFactor);

			canvas.draw(img, elem.pos);
			if(elem.heroCueTrace && elem.heroCueTrace->markFirstDrawReturned())
				heroPositiveProcPresentation::trace(*elem.heroCueTrace, "firstDrawReturn", elem.effectID,
					currentFrame, static_cast<int>(elem.animation->size()), -1, elem.pos.x, elem.pos.y, static_cast<bool>(img));
		});
	}
}

void BattleEffectsController::loadColorMuxers()
{
	const JsonNode config = JsonUtils::assembleFromFiles("config/battleEffects.json");

	for(auto & muxer : config["colorMuxers"].Struct())
	{
		ColorMuxerEffect effect;
		std::string identifier = muxer.first;

		for (const JsonNode & entry : muxer.second.Vector() )
		{
			effect.timePoints.push_back(entry["time"].Float());
			effect.effectColors.push_back(ColorRGBA(255*entry["color"][0].Float(), 255*entry["color"][1].Float(), 255*entry["color"][2].Float(), 255*entry["color"][3].Float()));
			effect.transparency.push_back(entry["alpha"].Float() * 255);
		}
		colorMuxerEffects[identifier] = effect;
	}
}

const ColorMuxerEffect & BattleEffectsController::getMuxerEffect(const std::string & name)
{
	static const ColorMuxerEffect emptyEffect;

	if (colorMuxerEffects.count(name))
		return colorMuxerEffects[name];

	logAnim->error("Failed to find color muxer effect named '%s'!", name);
	return emptyEffect;
}
