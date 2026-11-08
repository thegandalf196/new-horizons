/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionsController.h"
#include "BattleFieldController.h"
#include "BattleInterface.h"
#include "BattlePointBlankPresentation.h"
#include "BattleStacksController.h"
#include "../GameEngine.h"
#include "../gui/CursorHandler.h"
#include "../../lib/CStack.h"

void BattleActionsController::actionSetCursor(PossiblePlayerBattleAction action, const BattleHex & targetHex)
{
	if(action.get() == PossiblePlayerBattleAction::SKIRMISHER_ATTACK)
	{
		ENGINE->cursor().set(Cursor::Combat::SHOOT);
		return;
	}
	switch (action.get())
	{
		case PossiblePlayerBattleAction::CHOOSE_TACTICS_STACK:
			ENGINE->cursor().set(Cursor::Combat::POINTER);
			return;

		case PossiblePlayerBattleAction::MOVE_TACTICS:
		case PossiblePlayerBattleAction::MOVE_STACK:
			if (owner.stacksController->getActiveStack()->hasBonusOfType(BonusType::FLYING))
				ENGINE->cursor().set(Cursor::Combat::FLY);
			else
				ENGINE->cursor().set(Cursor::Combat::MOVE);
			return;

		case PossiblePlayerBattleAction::ATTACK:
		case PossiblePlayerBattleAction::LONG_WEAPON_ATTACK:
		case PossiblePlayerBattleAction::WALK_AND_ATTACK:
		case PossiblePlayerBattleAction::ATTACK_AND_RETURN:
		{
			static const std::map<BattleHex::EDir, Cursor::Combat> sectorCursor = {
				{BattleHex::TOP_LEFT,     Cursor::Combat::HIT_SOUTHEAST},
				{BattleHex::TOP_RIGHT,    Cursor::Combat::HIT_SOUTHWEST},
				{BattleHex::RIGHT,        Cursor::Combat::HIT_WEST     },
				{BattleHex::BOTTOM_RIGHT, Cursor::Combat::HIT_NORTHWEST},
				{BattleHex::BOTTOM_LEFT,  Cursor::Combat::HIT_NORTHEAST},
				{BattleHex::LEFT,         Cursor::Combat::HIT_EAST     },
				{BattleHex::TOP,          Cursor::Combat::HIT_SOUTH    },
				{BattleHex::BOTTOM,       Cursor::Combat::HIT_NORTH    }
			};

			auto direction = owner.fieldController->selectAttackDirection(targetHex);

			assert(sectorCursor.count(direction) > 0);
			if (sectorCursor.count(direction))
				ENGINE->cursor().set(sectorCursor.at(direction));

			return;
		}

		case PossiblePlayerBattleAction::SHOOT:
			if(pointBlankPresentation::setCursor(*owner.getBattle(), owner.stacksController->getActiveStack(), targetHex))
				return;
			if (owner.getBattle()->battleHasShootingPenalty(owner.stacksController->getActiveStack(), targetHex))
				ENGINE->cursor().set(Cursor::Combat::SHOOT_PENALTY);
			else
				ENGINE->cursor().set(Cursor::Combat::SHOOT);
			return;

		case PossiblePlayerBattleAction::DEMONIC_GATE:
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
			return;

		case PossiblePlayerBattleAction::AIMED_SPELL_CREATURE:
		case PossiblePlayerBattleAction::ANY_LOCATION:
		case PossiblePlayerBattleAction::WALK_AND_SPELLCAST:
		case PossiblePlayerBattleAction::RANDOM_GENIE_SPELL:
		case PossiblePlayerBattleAction::FREE_LOCATION:
		case PossiblePlayerBattleAction::OBSTACLE:
		case PossiblePlayerBattleAction::LIFE_DRAIN:
			ENGINE->cursor().set(Cursor::Spellcast::SPELL);
			return;

		case PossiblePlayerBattleAction::TELEPORT:
			if(!selectedStack)
				ENGINE->cursor().set(Cursor::Spellcast::SPELL);
			else
				ENGINE->cursor().set(Cursor::Combat::TELEPORT);
			return;

		case PossiblePlayerBattleAction::SACRIFICE:
			if(!selectedStack)
				ENGINE->cursor().set(Cursor::Spellcast::SPELL);
			else
				ENGINE->cursor().set(Cursor::Combat::SACRIFICE);
			return;

		case PossiblePlayerBattleAction::HEAL:
			ENGINE->cursor().set(Cursor::Combat::HEAL);
			return;

		case PossiblePlayerBattleAction::CATAPULT:
			ENGINE->cursor().set(Cursor::Combat::SHOOT_CATAPULT);
			return;

		case PossiblePlayerBattleAction::CREATURE_INFO:
			ENGINE->cursor().set(Cursor::Combat::QUERY);
			return;
		case PossiblePlayerBattleAction::HERO_INFO:
			ENGINE->cursor().set(Cursor::Combat::HERO);
			return;
	}
	assert(0);
}
