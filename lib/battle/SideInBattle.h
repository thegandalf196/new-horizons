/*
 * SideInBattle.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../GameConstants.h"
#include "HeroCommand.h"
#include "FocusFireState.h"
#include "../callback/GameCallbackHolder.h"

class CGHeroInstance;
class CGTownInstance;
class CArmedInstance;

struct DLL_LINKAGE SideInBattle : public GameCallbackHolder
{
	using GameCallbackHolder::GameCallbackHolder;

	PlayerColor color = PlayerColor::CANNOT_DETERMINE;
	ObjectInstanceID heroID; //may be empty if army is not commanded by hero
	ObjectInstanceID armyObjectID; //adv. map object with army that participates in battle; may be same as hero

	bool heroCommandUsed = false;
	// Decode-only compatibility slot. New Horizons no longer emits or exposes
	// doctrines; this member remains so old HERO_COMMANDS records retain their
	// wire position while loading.
	HeroCommand activeDoctrine = HeroCommand::NONE;
	HeroCommand activeOrder = HeroCommand::NONE;
	std::optional<HeroOrderState> orderState;
	std::optional<FocusFireState> focusFire;
	uint32_t castSpellsCount = 0; //how many spells each side has been cast this turn
	bool temporalFieldUsed = false; // saved once-per-combat Sorcery Mass Slow budget
	bool counterspellArmed = false; // saved Sorcery Counterspell ward, until the next hero action or enemy hero spell
	std::vector<SpellID> usedSpellsHistory; //every time hero casts spell, it's inserted here -> eagle eye skill
	int32_t enchanterCounter = 0; //tends to pass through 0, so sign is needed
	int32_t initialMana = 0;
	int32_t additionalMana = 0;

	void init(const CGHeroInstance * Hero, const CArmedInstance * Army, const CGTownInstance * town);
	const CArmedInstance * getArmy() const;
	const CGHeroInstance * getHero() const;

	template <typename Handler> void serialize(Handler &h)
	{
		if(h.saving && temporalFieldUsed && !h.hasFeature(Handler::Version::NEW_HORIZONS_TEMPORAL_FIELD))
			throw std::runtime_error("Cannot discard consumed Temporal Field battle state");
		if(h.saving && counterspellArmed && !h.hasFeature(Handler::Version::NEW_HORIZONS_COUNTERSPELL))
			throw std::runtime_error("Cannot discard armed Counterspell battle state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS)
			&& (focusFire || activeOrder == HeroCommand::FOCUS_FIRE))
			throw std::runtime_error("Cannot discard New Horizons targeted command state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS) && orderState)
			throw std::runtime_error("Cannot discard New Horizons canonical Order state");
		h & color;
		h & heroID;
		h & armyObjectID;
		h & castSpellsCount;
		h & usedSpellsHistory;
		h & enchanterCounter;
		h & initialMana;
		h & additionalMana;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_TEMPORAL_FIELD))
		{
			h & temporalFieldUsed;
		}
		else if(!h.saving)
		{
			temporalFieldUsed = false;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_COUNTERSPELL))
		{
			h & counterspellArmed;
		}
		else if(!h.saving)
		{
			counterspellArmed = false;
		}
		if(h.hasFeature(Handler::Version::HERO_COMMANDS))
		{
			h & heroCommandUsed;
			HeroCommand legacyDoctrine = HeroCommand::NONE;
			h & legacyDoctrine;
			h & activeOrder;
			if(!h.saving)
			{
				// A legacy doctrine may have consumed the old round action, but it
				// must not become current gameplay after a load. Keep that budget
				// bit while dropping only the obsolete Doctrine identity. Preserve
				// the raw Order temporarily so BattleInfo can remove any matching
				// legacy round bonuses before clearing a decode-only Order ID.
				activeDoctrine = HeroCommand::NONE;
			}
		}
		else if(!h.saving)
		{
			heroCommandUsed = false;
			activeDoctrine = HeroCommand::NONE;
			activeOrder = HeroCommand::NONE;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS))
		{
			h & focusFire;
		}
		else if(!h.saving)
		{
			focusFire.reset();
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CANONICAL_ORDERS))
		{
			h & orderState;
		}
		else if(!h.saving)
		{
			orderState.reset();
		}
	}
};
