/*
 * CArmedInstance.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CArmedInstance.h"

#include "CStackInstance.h"
#include "../CGHeroInstance.h"

#include "../../CPlayerState.h"
#include "../../entities/faction/CTown.h"
#include "../../entities/faction/CTownHandler.h"
#include "../../mapping/TerrainTile.h"
#include "../../GameLibrary.h"
#include "../../gameState/CGameState.h"

void CArmedInstance::randomizeArmy(FactionID type)
{
	for(auto & elem : stacks)
	{
		if(elem.second->randomStack)
		{
			int level = elem.second->randomStack->level;
			int upgrade = elem.second->randomStack->upgrade;
			elem.second->setType((*LIBRARY->townh)[type]->town->creatures[level][upgrade]);

			elem.second->randomStack = std::nullopt;
		}
		assert(elem.second->valid(false));
		assert(elem.second->getArmy() == this);
	}
}

CArmedInstance::CArmedInstance(IGameInfoCallback * cb)
	: CArmedInstance(cb, BonusNodeType::ARMY, false)
{
}

CArmedInstance::CArmedInstance(IGameInfoCallback * cb, BonusNodeType nodeType, bool isHypothetic)
	: CGObjectInstance(cb)
	, CBonusSystemNode(nodeType, isHypothetic)
	// Take troop-mixing freedom of Angelic Alliance or Temple of Loyalty into account.
	, alignmentMix(this, Selector::type()(BonusType::ALIGNMENT_MIX).Or(Selector::type()(BonusType::NONEVIL_ALIGNMENT_MIX)))
	, battle(nullptr)
{
}

bool CArmedInstance::canMixAlignment(EAlignment alignment) const
{
	// MOD COMPATIBILITY - deprecated NONEVIL_ALIGNMENT_MIX acts as ALIGNMENT_MIX for good and neutral alignments
	if((alignment == EAlignment::GOOD || alignment == EAlignment::NEUTRAL) && hasBonusOfType(BonusType::NONEVIL_ALIGNMENT_MIX))
		return true;

	return hasBonusOfType(BonusType::ALIGNMENT_MIX, BonusCustomSubtype::alignment(alignment));
}

const CGHeroInstance * CArmedInstance::moraleCommander() const
{
	return dynamic_cast<const CGHeroInstance *>(this);
}

void CArmedInstance::updateMoraleBonusFromArmy()
{
	if(!validTypes(false)) //object not randomized, don't bother
		return;

	auto b = getExportedBonusList().getFirst(Selector::sourceType()(BonusSource::ARMY).And(Selector::type()(BonusType::MORALE)));
	if(!b)
	{
		b = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE, BonusSource::ARMY, 0, BonusSourceID());
		addNewBonus(b);
	}

	//number of alignments and presence of undead
	std::set<FactionID> factions;
	std::set<FactionID> ordinaryFactions;
	const auto * commander = moraleCommander();
	const bool loyal = commander && commander->hasActivePerk(newHorizonsTraining::DIPLOMACY, newHorizonsTraining::LOYAL_MERCENARIES);
	bool hasUndead = false;

	for(const auto & slot : Slots())
	{
		const auto * creature = slot.second->getCreatureID().toEntity(LIBRARY);

		factions.insert(creature->getFactionID());
		if(!loyal || !slot.second->getTrainingReceipt().recruitedBy(commander->id))
			ordinaryFactions.insert(creature->getFactionID());

		// Check for undead flag instead of faction (undead mummies are neutral)
		if(!hasUndead)
		{
			//this is costly check, let's skip it at first undead
			hasUndead |= slot.second->hasBonusOfType(BonusType::UNDEAD);
		}
	}

	size_t factionsInArmy = factions.size(); //town garrison seems to take both sets into account

	if(alignmentMix.hasBonus())
	{
		//mixable alignments count as a single one, e.g. good and neutral for Angelic Alliance, or all of them for Temple of Loyalty
		size_t mixableFactions = 0;

		for(auto f : factions)
			if(canMixAlignment(LIBRARY->factions()->getById(f)->getAlignment()))
				mixableFactions++;

		if(mixableFactions > 0)
			factionsInArmy -= mixableFactions - 1;
	}

	// Loyal Mercenaries relieves only a negative faction-mix contribution.
	// The original faction set remains authoritative for genuine unity.
	size_t ordinaryCount = ordinaryFactions.size();
	if(alignmentMix.hasBonus())
	{
		size_t mixable = 0;
		for(const auto faction : ordinaryFactions)
			if(canMixAlignment(LIBRARY->factions()->getById(faction)->getAlignment())) ++mixable;
		if(mixable > 0) ordinaryCount -= mixable - 1;
	}
	MetaString bonusDescription;

	if(factionsInArmy == 1)
	{
		b->val = +1;
		bonusDescription.appendTextID("core.arraytxt.115"); //All troops of one alignment +1
	}
	else if(!factions.empty()) // no bonus from empty garrison
	{
		b->val = 2 - static_cast<si32>(factionsInArmy);
		if(loyal && b->val < 0)
			b->val = std::min<si32>(0, 2 - static_cast<si32>(ordinaryCount));
		bonusDescription.appendTextID("core.arraytxt.114"); //Troops of %d alignments %d
		bonusDescription.replaceNumber(factionsInArmy);
	}

	b->description = bonusDescription;

	nodeHasChanged();

	//-1 modifier for any Undead unit in army
	auto undeadModifier = getExportedBonusList().getFirst(Selector::source(BonusSource::ARMY, BonusCustomSource::undeadMoraleDebuff));
	if(hasUndead)
	{
		if(!undeadModifier)
		{
			undeadModifier = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MORALE, BonusSource::ARMY, -1, BonusCustomSource::undeadMoraleDebuff);
			undeadModifier->description.appendTextID("core.arraytxt.116");
			addNewBonus(undeadModifier);
		}
	}
	else if(undeadModifier)
		removeBonus(undeadModifier);
}

void CArmedInstance::armyChanged()
{
	updateMoraleBonusFromArmy();
}

CBonusSystemNode & CArmedInstance::whereShouldBeAttached(CGameState & gs)
{
	if(tempOwner.isValidPlayer())
		if(auto * where = gs.getPlayerState(tempOwner))
			return *where;

	return gs.globalEffects;
}

CBonusSystemNode & CArmedInstance::whatShouldBeAttached()
{
	return *this;
}

void CArmedInstance::attachToBonusSystem(CGameState & gs)
{
	whatShouldBeAttached().attachTo(whereShouldBeAttached(gs));
}

void CArmedInstance::restoreBonusSystem(CGameState & gs)
{
	whatShouldBeAttached().attachTo(whereShouldBeAttached(gs));
	for(const auto & elem : stacks)
		elem.second->artDeserializationFix(gs, elem.second.get());
}

void CArmedInstance::detachFromBonusSystem(CGameState & gs)
{
	whatShouldBeAttached().detachFrom(whereShouldBeAttached(gs));
}

void CArmedInstance::attachUnitsToArmy()
{
	assert(getArmy() != nullptr);

	for(const auto & elem : stacks)
		elem.second->setArmy(getArmy());
}

const IBonusBearer * CArmedInstance::getBonusBearer() const
{
	return this;
}

void CArmedInstance::serializeJsonOptions(JsonSerializeFormat & handler)
{
	CGObjectInstance::serializeJsonOptions(handler);
	CCreatureSet::serializeJson(handler, "army", 7);
}

TerrainId CArmedInstance::getCurrentTerrain() const
{
	if(anchorPos().isValid())
		return cb->getTile(visitablePos())->getTerrainID();
	else
		return TerrainId::NONE;
}
