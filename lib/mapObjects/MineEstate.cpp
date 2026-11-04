/*
 * Part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "MiscObjects.h"
#include "CGHeroInstance.h"
#include "../CPlayerState.h"
#include "../CCreatureHandler.h"
#include "../StartInfo.h"
#include "../GameLibrary.h"
#include "../callback/IGameEventCallback.h"
#include "../callback/IGameInfoCallback.h"
#include "../callback/IGameRandomizer.h"
#include "../entities/ResourceTypeHandler.h"
#include "../entities/creature/NewHorizonsMusterRules.h"
#include "../mapObjectConstructors/CommonConstructors.h"
#include "../mapObjectConstructors/CObjectClassesHandler.h"
#include "../networkPacks/PacksForClient.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../networkPacks/StackLocation.h"
#include "../serializer/JsonSerializeFormat.h"
#include <vstd/RNG.h>
#include <limits>

//CGMine
std::shared_ptr<MineInstanceConstructor> CGMine::getResourceHandler() const
{
	const auto & baseHandler = getObjectHandler();
	const auto & ourHandler = std::dynamic_pointer_cast<MineInstanceConstructor>(baseHandler);
	return ourHandler;
}

void CGMine::onHeroVisit(IGameEventCallback & gameEvents, const CGHeroInstance * h) const
{
	auto relations = cb->getPlayerRelations(h->tempOwner, tempOwner);

	if(relations == PlayerRelations::SAME_PLAYER) //we're visiting our mine
	{
		deliverPendingMercenaries(gameEvents);
		gameEvents.showGarrisonDialog(id, h->id, true, MetaString());
		return;
	}
	else if (relations == PlayerRelations::ALLIES)//ally
		return;

	if(stacksCount()) //Mine is guarded
	{
		BlockingDialog ynd(true,false);
		ynd.player = h->tempOwner;
		// ownedGuardedMessage applies to any owned mine with guards.
		const bool useOwnedGuardedMessage = tempOwner != PlayerColor::NEUTRAL;
		const std::string guardedMessageTextID = useOwnedGuardedMessage ? getResourceHandler()->getOwnedGuardedMessageTextID() : getResourceHandler()->getOnGuardedMessageTextID();
		if(!guardedMessageTextID.empty())
			ynd.text.appendTextID(guardedMessageTextID);
		else
			ynd.text.appendTextID("core.advevent.187");

		gameEvents.showBlockingDialog(this, &ynd);
		return;
	}

	flagMine(gameEvents, h);
}

void CGMine::initObj(IGameRandomizer & gameRandomizer)
{
	const auto configuredGuards = getResourceHandler()->getGuards(cb, gameRandomizer);
	if(!configuredGuards.empty())
	{
		for(const auto & stack : configuredGuards)
		{
			auto guards = std::make_unique<CStackInstance>(cb, stack.getId(), stack.getCount());
			putStack(SlotID(stacksCount()), std::move(guards));
		}
	}

	if(isAbandoned())
	{
		assert(!abandonedMineResources.empty());
		if (!abandonedMineResources.empty())
		{
			producedResource = *RandomGeneratorUtil::nextItem(abandonedMineResources, gameRandomizer.getDefault());
		}
		else
		{
			logGlobal->error("Abandoned mine at (%s) has no valid resource candidates!", anchorPos().toString());
			producedResource = GameResID::GOLD;
		}
	}
	else
	{
		if(getResourceHandler()->getResourceType() == GameResID::NONE) // fallback
			producedResource = GameResID(getObjTypeIndex().getNum());
		else
			producedResource = getResourceHandler()->getResourceType();
	}
	producedQuantity = defaultResProduction();
}

bool CGMine::isAbandoned() const
{
	return subID.getNum() >= 7 && getResourceHandler()->getResourceType() == GameResID::NONE;
}

const IOwnableObject * CGMine::asOwnable() const
{
	return this;
}

std::vector<CreatureID> CGMine::providedCreatures() const
{
	return {};
}

ResourceSet CGMine::dailyIncome() const
{
	ResourceSet result;

	for (GameResID k : LIBRARY->resourceTypeHandler->getAllObjects())
		result[k] += valOfBonuses(BonusType::GENERATE_RESOURCE, BonusSubtypeID(k));

	result[producedResource] += defaultResProduction();

	const auto & playerSettings = cb->getPlayerSettings(getOwner());
	result.applyHandicap(playerSettings->handicap.percentIncome);

	return result;
}

MetaString CGMine::getObjectName() const
{
	if(getResourceHandler()->getResourceType() == GameResID::NONE || getObjTypeIndex() < GameConstants::RESOURCE_QUANTITY)
		return MetaString::createFromTextID("core.minename", getObjTypeIndex());
	else
		return MetaString::createFromTextID(getResourceHandler()->getNameTextID());
}

MetaString CGMine::getHoverText(PlayerColor player) const
{
	MetaString hoverName = CArmedInstance::getHoverText(player);

	if (tempOwner != PlayerColor::NEUTRAL)
	{
		hoverName.appendEOL();
		hoverName.appendRawString("(");
		hoverName.appendName(producedResource);
		hoverName.appendRawString(")");
	}

	if(stacksCount())
	{
		hoverName.appendEOL();
		hoverName.appendTextID("core.genrltxt.202"); //Guarded by
		hoverName.appendRawString(" ");
		hoverName.append(getArmyDescription());
	}
	return hoverName;
}

MetaString CGMine::getHoverText(const CGHeroInstance * hero) const
{
	auto text = getHoverText(hero->getOwner());
	if(hero->getOwner() == getOwner() && getPendingMercenaries() > 0)
	{
		text.appendEOL();
		text.appendTextID("new-horizons.economy.mercenaryCompany.pending");
		text.replaceNumber(getPendingMercenaries());
	}
	return text;
}

void CGMine::flagMine(IGameEventCallback & gameEvents, const CGHeroInstance * capturingHero) const
{
	const PlayerColor player = capturingHero->tempOwner;
	assert(tempOwner != player);
	gameEvents.setOwner(this, player); //not ours? flag it!

	ResourceSet landSurveyorProduction;
	const auto calendar = cb->getCalendar();
	const int week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	const bool landSurveyorTriggered = capturingHero->hasActivePerk("new-horizons:estates", "new-horizons:estates.landSurveyor")
		&& !capturingHero->hasUsedNewHorizonsLandSurveyor(week);
	if(landSurveyorTriggered)
	{
		gameEvents.setObjPropertyValue(capturingHero->id, ObjProperty::NEW_HORIZONS_LAND_SURVEYOR_LAST_WEEK, week);
		landSurveyorProduction = dailyIncome() * 3;
		gameEvents.giveResources(player, landSurveyorProduction);
	}

	InfoWindow iw;
	iw.type = EInfoWindowMode::AUTO;
	const auto descriptionTextID = getResourceHandler()->getDescriptionTextID();
	if(!descriptionTextID.empty())
		iw.text.appendTextID(descriptionTextID);
	else
		iw.text.appendTextID("core.mineevnt", producedResource.getNum());
	iw.player = player;
	iw.components.emplace_back(ComponentType::RESOURCE_PER_DAY, producedResource, getProducedQuantity());
	if(landSurveyorTriggered)
	{
		iw.text.appendEOL();
		iw.text.appendRawString("Land Surveyor: the mine immediately produced three days of output.");
		for(const auto resource : LIBRARY->resourceTypeHandler->getAllObjects())
			if(landSurveyorProduction[resource] != 0)
				iw.components.emplace_back(ComponentType::RESOURCE, resource, landSurveyorProduction[resource]);
	}
	gameEvents.showInfoDialog(&iw);
}

ui32 CGMine::defaultResProduction() const
{
	if(isAbandoned())
	{
		switch(producedResource.toEnum())
		{
		case EGameResID::WOOD:
		case EGameResID::ORE:
			return 2;
		case EGameResID::GOLD:
			return 1000;
		default:
			return 1;
		}
	}
	return getResourceHandler()->getDefaultQuantity();
}

ui32 CGMine::getProducedQuantity() const
{
	auto * playerSettings = cb->getPlayerSettings(getOwner());
	// always round up income - we don't want mines to always produce zero if handicap in use
	return vstd::divideAndCeil(producedQuantity * playerSettings->handicap.percentIncome, 100);
}

void CGMine::battleFinished(IGameEventCallback & gameEvents, const CGHeroInstance *hero, const BattleResult &result) const
{
	if(result.winner == BattleSide::ATTACKER) //attacker won
	{
		if(isAbandoned())
		{
			const auto onCaptureMessageTextID = getResourceHandler()->getOnCaptureMessageTextID();
			if(!onCaptureMessageTextID.empty())
			{
				InfoWindow iw;
				iw.type = EInfoWindowMode::AUTO;
				iw.player = hero->tempOwner;
				iw.text.appendTextID(onCaptureMessageTextID);
				gameEvents.showInfoDialog(&iw);
			}
		}
		flagMine(gameEvents, hero);
	}
}

void CGMine::blockingDialogAnswered(IGameEventCallback & gameEvents, const CGHeroInstance *hero, int32_t answer) const
{
	if(answer)
		gameEvents.startBattle(hero, this);
}

void CGMine::serializeJsonOptions(JsonSerializeFormat & handler)
{
	CArmedInstance::serializeJsonOptions(handler);
	serializeJsonOwner(handler);
	if(isAbandoned())
		handler.serializeIdArray<GameResID>("possibleResources", abandonedMineResources);
}

void CGMine::validateMercenaryState() const
{
	if(mercenaryLastWeek < -1 || pendingMercenaries < 0
		|| (pendingMercenaries > 0 && (mercenaryLastWeek < 0 || !pendingMercenaryOwner.isValidPlayer()
			|| pendingMercenaryOwner != getOwner()))
		|| (pendingMercenaries == 0 && pendingMercenaryOwner != PlayerColor::NEUTRAL))
		throw std::runtime_error("Invalid Mercenary Company mine receipt");
}

void CGMine::setPropertyDer(ObjProperty what, ObjPropertyID value)
{
	if(what == ObjProperty::OWNER && value.as<PlayerColor>() != getOwner())
	{
		// Debt belongs to the former owner; preserve the mine/week receipt on recapture.
		pendingMercenaries = 0;
		pendingMercenaryOwner = PlayerColor::NEUTRAL;
	}
	else if(what == ObjProperty::NEW_HORIZONS_MINE_MERCENARY_WEEK)
	{
		const int week = value.as<NumericID>().getNum();
		if(week < -1 || week < mercenaryLastWeek)
			throw std::runtime_error("Invalid Mercenary Company week");
		mercenaryLastWeek = week;
	}
	else if(what == ObjProperty::NEW_HORIZONS_MINE_MERCENARY_PENDING)
	{
		const int count = value.as<NumericID>().getNum();
		if(count < 0 || (count > 0 && (mercenaryLastWeek < 0 || !getOwner().isValidPlayer())))
			throw std::runtime_error("Invalid Mercenary Company pending grant");
		pendingMercenaries = count;
		pendingMercenaryOwner = count > 0 ? getOwner() : PlayerColor::NEUTRAL;
	}
	validateMercenaryState();
}

void CGMine::grantWeeklyMercenaries(IGameEventCallback & events, int32_t week, int32_t amount) const
{
	validateMercenaryState();
	if(week < 0 || amount < 0)
		throw std::runtime_error("Invalid weekly Mercenary Company grant");
	if(!getOwner().isValidPlayer() || week <= mercenaryLastWeek)
		return;
	if(amount > std::numeric_limits<int32_t>::max() - pendingMercenaries)
		throw std::overflow_error("Mercenary Company pending grant overflow");
	events.setObjPropertyValue(id, ObjProperty::NEW_HORIZONS_MINE_MERCENARY_WEEK, week);
	events.setObjPropertyValue(id, ObjProperty::NEW_HORIZONS_MINE_MERCENARY_PENDING, pendingMercenaries + amount);
	deliverPendingMercenaries(events);
}

void CGMine::deliverPendingMercenaries(IGameEventCallback & events) const
{
	validateMercenaryState();
	if(pendingMercenaries == 0)
		return;
	int32_t remaining = pendingMercenaries;
	// Merge existing Rogue guards first, then fill a free native army slot. No replacement.
	for(const auto & [slot, stack] : stacks)
	{
		if(stack->getCreatureID() != CreatureID(CreatureID::decode("core:rogue")))
			continue;
		const int64_t room = static_cast<int64_t>(std::numeric_limits<TQuantity>::max()) - stack->getCount();
		const auto delivered = static_cast<TQuantity>(std::min<int64_t>(remaining, room));
		if(delivered > 0 && events.changeStackCount(StackLocation(id, slot), delivered, ChangeValueMode::RELATIVE))
			remaining -= delivered;
		if(remaining == 0)
			break;
	}
	if(remaining > 0)
	{
		const auto slots = getFreeSlots();
		if(!slots.empty() && events.insertNewStack(StackLocation(id, slots.front()), CreatureID(CreatureID(CreatureID::decode("core:rogue"))).toCreature(), remaining))
			remaining = 0;
	}
	if(remaining != pendingMercenaries)
		events.setObjPropertyValue(id, ObjProperty::NEW_HORIZONS_MINE_MERCENARY_PENDING, remaining);
}
