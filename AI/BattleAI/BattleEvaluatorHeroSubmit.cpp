/*
 * BattleEvaluatorHeroSubmit.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "HeroActionTiming.h"
#include "HeroOrderCandidateRules.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/spells/CSpell.h"

// Existing saved-rule predicates owned by BattleEvaluator.cpp.
bool isCanonicalFireWall(const CBattleInfoCallback & battle, const CSpell * spell);
bool isCanonicalHeavenlyGale(const CSpell * spell);
bool isCanonicalCrusade(const CSpell * spell);
using heroOrderCandidates::commandTargetIsLegal;

bool BattleEvaluator::submitHeroAction(const PossibleSpellcast & castToPerform,
	heroActionTiming::Summary * timing)
{
	if(castToPerform.command != HeroCommand::NONE)
	{
		if(!commandTargetIsLegal(*cb->getBattle(battleID), side, castToPerform.command,
			castToPerform.commandTargets))
			return false;

		if(castToPerform.command == HeroCommand::FOCUS_FIRE)
		{
			const auto targetId = castToPerform.focusFire.value().targetUnitId;
			if(!cb->getBattle(battleID)->battleCanConfirmHeroCommand(side, castToPerform.command, targetId))
				return false;
			heroActionTiming::SubmitScope submit(timing);
			cb->battleMakeSpellAction(battleID,
				BattleAction::makeTargetedHeroCommand(side, castToPerform.command, targetId));
		}
		else
		{
			BattleAction action;
			if(castToPerform.commandTargets.size() == 1)
				action = BattleAction::makeTargetedHeroCommand(side, castToPerform.command,
					castToPerform.commandTargets.front());
			else if(castToPerform.commandTargets.size() == 2)
				action = BattleAction::makePairedHeroCommand(side, castToPerform.command,
					castToPerform.commandTargets.front(), castToPerform.commandTargets.back());
			else
				action = BattleAction::makeHeroCommand(side, castToPerform.command);
			heroActionTiming::SubmitScope submit(timing);
			cb->battleMakeSpellAction(battleID, action);
		}
		activeActionMade = true;
		return true;
	}
	BattleAction spellcast;
	spellcast.actionType = EActionType::HERO_SPELL;
	spellcast.spell = castToPerform.spell->id;
	spellcast.spellOvercharge = castToPerform.spellOvercharge;
	spellcast.spellSelectiveDispel = castToPerform.spellSelectiveDispel;
	spellcast.spellCureAffliction = castToPerform.spellCureAffliction;
	spellcast.spellCurePhysicalAffliction = castToPerform.spellCurePhysicalAffliction;
	spellcast.spellPurifyChoices = castToPerform.spellPurifyChoices;
	spellcast.spellMassSlow = castToPerform.spellMassSlow;
	spellcast.spellShadowGiftSacrificePercent = castToPerform.spellShadowGiftSacrificePercent;
	spellcast.metamagicFollowup = castToPerform.metamagicFollowup;
	if(isCanonicalFireWall(*cb->getBattle(battleID), castToPerform.spell)
		&& castToPerform.spellFireWallDirection != BattleHex::NONE
		&& !castToPerform.dest.empty())
	{
		// Evaluation keeps the complete footprint so delayed damage and
		// friendly exposure can be scored.  The wire action intentionally
		// carries only the start hex plus the direction; the server expands
		// and validates the line authoritatively.
		spellcast.aimToHex(castToPerform.dest.front().hexValue);
		spellcast.spellFireWallDirection = castToPerform.spellFireWallDirection;
	}
	else if(isCanonicalHeavenlyGale(castToPerform.spell)
		|| isCanonicalCrusade(castToPerform.spell) || castToPerform.dest.empty())
		// Mass spells use AimType::NOTHING in mechanics and are enumerated as
		// an empty candidate. The action protocol still requires one destination
		// entry; INVALID is the shared NO_LOCATION sentinel, not a unit target.
		spellcast.aimToHex(BattleHex::INVALID);
	else
		spellcast.setTarget(castToPerform.dest);
	spellcast.side = side;
	spellcast.stackNumber = -1;
	{
		heroActionTiming::SubmitScope submit(timing);
		cb->battleMakeSpellAction(battleID, spellcast);
	}
	activeActionMade = true;

	return true;
}
