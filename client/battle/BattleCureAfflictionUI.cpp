/*
 * BattleCureAfflictionUI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleInterface.h"
#include "BattleActionsController.h"
#include "CureAfflictionSession.h"
#include "CureAfflictionPresenter.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "../windows/GUIClasses.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/texts/CGeneralTextHandler.h"

std::shared_ptr<CObjectListWindow> showCureAfflictionModal(
	const std::vector<std::string> & names, std::function<void(int)> confirm,
	std::function<void()> cancel)
{
	auto window = std::make_shared<CObjectListWindow>(names, nullptr, "Cure", "Choose a physical affliction to remove.", confirm);
	window->onExit = std::move(cancel);
	ENGINE->windows().pushWindow(window);
	return window;
}

void BattleInterface::installCureAfflictionUI()
{
	if(!actionsController)
		return;

	// The factory belongs to this controller and is invoked synchronously. Only
	// the delayed modal callbacks escape it; those hold weak session identity.
	actionsController->setCureAfflictionPicker([this](const BattleAction & pending, const CStack * initialTarget)
	{
		const auto owner = CPlayerInterface::battleInt;
		if(owner.get() != this)
			return false;
		const BattleID localBattleID = getBattleID();
		const auto callback = curInt && curInt->cb ? curInt->cb->getBattle(localBattleID) : nullptr;
		if(!callback || !callback->getBattle() || !initialTarget || !currentHero()
			|| !newHorizonsMagic::cureEnabled(callback->getBattle()->getMagicRules(), pending.spell))
			return false;

		const auto choices = newHorizonsMagic::cureAfflictions(callback->getBattle()->getMagicRules(), initialTarget);
		const bool frozenChoice = newHorizonsFrozen::chancePercent(callback->getBattle()->getMagicRules()) > 0
			&& initialTarget->alive() && newHorizonsFrozen::isFrozen(*initialTarget);
		if(choices.empty() && !frozenChoice)
			return false; // Ordinary healing needs no additional choice.

		std::vector<std::string> names;
		for(const auto choice : choices)
			names.push_back(choice.toSpell()->getNameTranslated());
		if(frozenChoice)
			names.push_back(LIBRARY->generaltexth->translate("new-horizons.combat.frozen.label"));
		const auto targetID = initialTarget->unitId();
		const auto heroID = currentHero()->id;
		const CureAfflictionSession session(owner, actionsController->getCastingSession());
		auto confirm = [session, pending, localBattleID, targetID, heroID, choices, frozenChoice](int selected)
		{
			session.confirm(CPlayerInterface::battleInt, [&](BattleInterface & owner)
			{
				if(!owner.curInt || !owner.curInt->cb || selected < 0
					|| static_cast<size_t>(selected) >= choices.size() + (frozenChoice ? 1 : 0))
					return;
				const auto callback = owner.curInt->cb->getBattle(localBattleID);
				const auto * hero = owner.currentHero();
				const auto * target = callback ? callback->battleGetUnitByID(targetID) : nullptr;
				if(!callback || !callback->getBattle() || !hero || hero->id != heroID || !target)
					return;

				const auto * spell = pending.spell.toSpell();
				spells::BattleCast preview(callback.get(), hero, spells::Mode::HERO, spell);
				preview.setMetamagicFollowup(pending.metamagicFollowup);
				const bool removeFrozen = static_cast<std::size_t>(selected) == choices.size();
				preview.setCureAffliction(removeFrozen ? SpellID(SpellID::NONE) : choices[selected]);
				preview.setCurePhysicalAffliction(removeFrozen ? "frozen" : "");
				auto mechanics = spell->battleMechanics(&preview);
				spells::detail::ProblemImpl problem;
				battle::Target targetCheck;
				targetCheck.emplace_back(target, target->getPosition());
				if(!mechanics->canBeCast(problem) || !mechanics->canBeCastAt(targetCheck, problem))
				{
					owner.curInt->showInfoDialog("This affliction can no longer be cured.");
					return;
				}

				BattleAction action = pending;
				action.target.clear();
				action.aimToUnit(target);
				action.spellCureAffliction = removeFrozen ? SpellID(SpellID::NONE) : choices[selected];
				action.spellCurePhysicalAffliction = removeFrozen ? "frozen" : "";
				owner.curInt->cb->battleMakeSpellAction(localBattleID, action);
			});
		};
		auto cancel = [session]
		{
			session.cancel(CPlayerInterface::battleInt);
		};
		if(cureAfflictionPresenter)
			cureAfflictionPresenter->show(names, *initialTarget, std::move(confirm), std::move(cancel));
		else
			showCureAfflictionModal(names, std::move(confirm), std::move(cancel));
		return true;
	});
}
