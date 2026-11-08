/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleHeroActionWindow.h"
#include "BattleActionsController.h"
#include "BattleInterface.h"
#include "NewHorizonsOrderPresentation.h"
#include "../CPlayerInterface.h"
#include "../widgets/Buttons.h"
#include "../widgets/TextControls.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsCommandResources.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

using namespace newHorizonsOrderPresentation;

namespace
{
std::string commandUnavailableReason(BattleInterface & owner)
{
	const auto callback = owner.getBattle();
	const auto side = callback->battleGetMySide();
	if(!callback->battleUsesHeroCommands())
		return "Orders are not enabled in this battle.";
	if(!owner.currentHero())
		return "No commanding hero is available.";
	if(!newHorizonsCommand::available(*callback, side))
		return "No Command remains. Command is not replenished during combat.";
	if(owner.curInt->isAutoFightOn)
		return "Autofight controls this battle.";
	if(owner.isInTacticsMode())
		return "Orders are unavailable during tactics.";
	if(owner.actionsController->heroSpellcastingModeActive())
		return "Finish or cancel spell targeting first.";
	if(!owner.makingTurn())
		return "It is not your turn.";
	if(callback->battleHeroActionAllowanceCounts(side).heroActions == 0)
		return "The shared Hero Action has already been spent this round.";
	return {};
}
}

bool BattleHeroActionWindow::refreshCommandButtons(const std::shared_ptr<BattleInterface> & owner,
	const CGHeroInstance * hero)
{
	const auto callback = owner->getBattle();
	const auto side = callback->battleGetMySide();
	const auto & rules = callback->getBattle()->getHeroCommandRules();
	const auto commonReason = commandUnavailableReason(*owner);
	const bool commonAvailable = commonReason.empty();
	bool anyCommand = false;
	for(size_t i = 0; i < commands.size(); ++i)
	{
		auto & entry = commands[i];
		const bool supported = heroCommands::supportedByRules(rules, entry.first);
		const bool targeted = isTargeted(entry.first);
		// Resource and ordinary Hero Action checks precede target enumeration.
		const bool targetReady = commonAvailable && supported && targeted
			&& callback->battleCanBeginHeroCommand(side, entry.first);
		const bool hasTargets = commonAvailable && supported && targeted
			&& !callback->battleGetHeroCommandTargets(side, entry.first).empty();
		const bool available = commonAvailable && supported
			&& (targeted ? targetReady : callback->battleCanUseHeroCommand(side, entry.first));
		const bool protectPairUnavailable = entry.first == HeroCommand::PROTECT && targeted && supported
			&& commonAvailable && !targetReady;
		entry.second->block(!available && !protectPairUnavailable);
		entry.second->addUsedEvents(SHOW_POPUP);
		anyCommand |= available;
		std::string reason = commonReason;
		if(reason.empty() && !supported)
			reason = "This Order is not available in this battle.";
		if(reason.empty() && !available)
			reason = unavailableOrderReason(*callback, side, entry.first, hasTargets);
		const auto revisedPreview = commonAvailable
			? revisedOrderPreview(*callback, side, entry.first, hero) : RevisedOrderPreview{};
		if(!revisedPreview.label.empty() && i < effectLabels.size()
			&& effectLabels[i]->getText() != revisedPreview.label)
			effectLabels[i]->setText(revisedPreview.label);
		const auto & display = commandDisplay(entry.first);
		entry.second->setHelp(CButton::tooltip(display.name,
			std::string(display.description) + (reason.empty() ? "\n\nReady: choose this Order." : "\n\nDisabled: " + reason)
			+ revisedPreview.help
			+ revisedActiveHelp(callback->battleGetHeroOrderState(side, entry.first), callback->battleGetRound())
			+ combinedArmsHelp(rules, entry.first, hero)
			+ "\nCosts 1 Command and the shared Hero Action; no Mana."));
		entry.second->setBorderColor(protectPairUnavailable ? std::optional<ColorRGBA>(Colors::ORANGE)
			: callback->battleGetHeroOrderState(side, entry.first) ? std::optional<ColorRGBA>(Colors::YELLOW)
			: std::nullopt);
	}
	return anyCommand;
}

void BattleHeroActionWindow::refresh()
{
	const auto owner = currentBattle();
	if(cancel)
		cancel->block(false);
	if(!owner)
	{
		if(spellButton)
			spellButton->block(true);
		for(auto & entry : commands)
		{
			entry.second->block(true);
			entry.second->setBorderColor(std::nullopt);
			entry.second->setHelp(CButton::tooltip(HeroCommandUI::name(entry.first),
				"Battle no longer available. Close this window."));
		}
		if(targetReadback && targetReadback->getText() != "Battle no longer available")
			targetReadback->setText("Battle no longer available");
		setStateText("Battle no longer available. Close this window.");
		return;
	}
	const auto callback = owner->getBattle();
	const auto side = callback->battleGetMySide();
	const auto * hero = owner->currentHero();
	const auto & rules = callback->getBattle()->getHeroCommandRules();
	const auto & resources = callback->getBattle()->getCommandResources(side);
	const std::string poolText = "Command " + std::to_string(resources.remaining) + " / "
		+ std::to_string(resources.maximum) + " | Each Order costs 1 Command and 1 Hero Action";
	if(commandPool && commandPool->getText() != poolText)
		commandPool->setText(poolText);
	if(hero)
	{
		const auto preparedHold = callback->battlePrepareHeroOrderState(side, HeroCommand::HOLD_THE_LINE, {});
		refreshEffects(*hero, rules,
			preparedHold ? preparedHold->divineMandateEfficiencyBonusPercent() : 0);
	}
	const bool canAct = owner->makingTurn() && !owner->curInt->isAutoFightOn && !owner->isInTacticsMode()
		&& !owner->actionsController->heroSpellcastingModeActive();
	const auto spellProblem = hero ? callback->battleCanCastSpell(hero, spells::Mode::HERO) : ESpellCastProblem::INVALID;
	const bool canSpell = hero && (spellProblem == ESpellCastProblem::OK
		|| spellProblem == ESpellCastProblem::CASTS_PER_TURN_LIMIT);
	if(spellButton)
		spellButton->block(!canAct || !canSpell); // Command exhaustion does not block spellcasting.
	const bool anyCommand = refreshCommandButtons(owner, hero);
	const auto order = callback->battleGetActiveOrder(side);
	if(targetReadback)
	{
		const auto readback = activeOrderReadback(*callback, side, rules, order);
		if(targetReadback->getText() != readback)
			targetReadback->setText(readback);
	}
	const auto reason = commandUnavailableReason(*owner);
	setStateText("Order: " + HeroCommandUI::name(order) + " | "
		+ (!reason.empty() ? reason : anyCommand ? "Order available" : "No Order currently available"));
}
