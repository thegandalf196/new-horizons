/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "CSpellWindow.h"
#include "SpellbookEntryPresentation.h"
#include "CCastleInterface.h"
#include "InfoWindows.h"
#include "../CPlayerInterface.h"
#include "../PlayerLocalState.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../battle/BattleInterface.h"
#include "../battle/BattleActionsController.h"
#include "../battle/DoubleCastCue.h"
#include "../adventureMap/AdventureMapInterface.h"
#include "../gui/WindowHandler.h"
#include "../widgets/CComponent.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "events/InputHandler.h"
#include "render/CAnimation.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/SpellCostBreakdown.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/SpellSchoolHandler.h"
#include "../../lib/spells/adventure/AdventureSpellEffect.h"
#include "../../lib/texts/CGeneralTextHandler.h"

CSpellWindow::SpellArea::SpellArea(Rect pos, CSpellWindow * owner)
{
	this->pos = pos;
	this->owner = owner;
	addUsedEvents(LCLICK | SHOW_POPUP | HOVER);

	schoolLevel = -1;
	schoolLocked = false;
	mySpell = nullptr;

	OBJECT_CONSTRUCTION;

	image = std::make_shared<CAnimImage>(AnimationPath::builtin("Spells"), 0, 0);
	image->visible = false;

	name = std::make_shared<CLabel>(39, 70, FONT_TINY, ETextAlignment::CENTER);
	cost = std::make_shared<CLabel>(39, 82, FONT_TINY, ETextAlignment::CENTER);
	effect = std::make_shared<CLabel>(39, 94, FONT_TINY, ETextAlignment::CENTER);

	for(auto l : {name, cost, effect})
		l->setAutoRedraw(false);
}

CSpellWindow::SpellArea::~SpellArea() = default;

void CSpellWindow::SpellArea::clickPressed(const Point & cursorPosition)
{
	if(mySpell)
	{
		ENGINE->input().hapticFeedback();
		if(schoolLocked)
		{
			GAME->interface()->showInfoDialog(schoolRequirementText);
			return;
		}
		if(const auto refusal = owner->followUpRefusalText(mySpell->id); !refusal.empty())
		{
			GAME->interface()->showInfoDialog(refusal);
			return;
		}

		if(owner->onSpellSelect)
		{
			owner->onSpellSelect(mySpell->id);
			owner->close();
			return;
		}

		const auto battleInterface = owner->myInt->battleInt;
		const auto battleCallback = battleInterface ? battleInterface->getBattle() : nullptr;
		const auto costBreakdown = owner->myInt->cb->getSpellCostBreakdown(mySpell, owner->myHero);
		const int spellCost = costBreakdown.finalCost;
		if(spellCost > owner->myHero->getManaAvailable()) // Insufficient Mana.
		{
			MetaString message = MetaString::createFromTextID("core.genrltxt.206"); // That spell costs %d spell points. Your hero only has %d spell points...
			message.replaceNumber(spellCost);
			message.replaceNumber(owner->myHero->getManaAvailable());
			GAME->interface()->showInfoDialog(message.toString(&GAME->translator()));
			return;
		}

		//anything that is not combat spell is adventure spell
		//this not an error in general to cast even creature ability with hero
		const bool combatSpell = mySpell->isCombat();
		if(combatSpell == mySpell->isAdventure())
		{
			logGlobal->error("Spell have invalid flags");
			return;
		}

		const bool inCombat = battleInterface != nullptr;
		const bool inCastle = owner->myInt->castleInt != nullptr;

		//battle spell on adv map or adventure map spell during combat => display infowindow, not cast
		if((combatSpell != inCombat) || inCastle || (!combatSpell && !GAME->interface()->makingTurn))
		{
			std::vector<std::shared_ptr<CComponent>> hlp(1, std::make_shared<CComponent>(ComponentType::SPELL, mySpell->id));
			GAME->interface()->showInfoDialog(
				newHorizonsMagic::spellSummaryForHero(owner->myHero, mySpell, schoolLevel), hlp, 0,
				newHorizonsMagic::spellDescriptionForHero(owner->myHero, mySpell, schoolLevel));
		}
		else if(combatSpell)
		{
			spells::detail::ProblemImpl problem;
			const bool canCast = mySpell->canBeCast(problem, battleCallback.get(), spells::Mode::HERO,
				owner->myHero);
				if(canCast)
			{
				// Close the spellbook before cast setup: a NO_LOCATION spell may
				// synchronously open a post-selection modal (Selective Dispel), and
				// only the topmost window may be closed.
				const SpellID selectedSpell = mySpell->id;
				owner->closeForSpellSelection();
				battleInterface->castThisSpell(selectedSpell);
				return;
			}
			else
			{
				std::vector<std::string> texts;
				problem.getAll(texts);
				if(!texts.empty())
					GAME->interface()->showInfoDialog(texts.front());
				else
					GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.adventureMap.spellUnknownProblem"));
			}
		}
		else //adventure spell
		{
			const CGHeroInstance * h = owner->myHero;
			ENGINE->windows().popWindows(1);

			auto guard = vstd::makeScopeGuard([this]()
			{
				auto spellBookState = owner->myInt->localState->getSpellbookSettings();
				spellBookState.spellbookLastTabAdvmap = owner->selectedTab;
				spellBookState.spellbookLastPageAdvmap = owner->currentPage;
				owner->myInt->localState->setSpellbookSettings(spellBookState);
			});

			spells::detail::ProblemImpl problem;
			if (mySpell->getAdventureMechanics().canBeCast(problem, GAME->interface()->cb.get(), owner->myHero))
			{
				const auto * spellEffect = mySpell->getAdventureMechanics().getEffectAs<IAdventureSpellEffect>(owner->myHero);

				if(spellEffect && spellEffect->requiresTargetSelection(owner->myHero))
					adventureInt->enterCastingMode(mySpell);
				else
					owner->myInt->cb->castSpell(h, mySpell->id);
			}
			else
			{
				std::vector<std::string> texts;
				problem.getAll(texts);
				if(!texts.empty())
					GAME->interface()->showInfoDialog(texts.front());
				else
					GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.adventureMap.spellUnknownProblem"));
			}
		}
	}
}

void CSpellWindow::SpellArea::hover(bool on)
{
	if(mySpell)
	{
		if(on)
		{
			MetaString message = MetaString::createFromRawString("%s (%s)");
			message.replaceTextID(mySpell->getNameTextID());
			const int spellLevel = owner->spellLevels.at(mySpell->getId());
			if(spellLevel > 0)
				message.replaceTextID("core.genrltxt", 171 + spellLevel);
			else
				message.replaceTextID("vcmi.spellBook.zero_level.hint");
			auto statusText = message.toString(&GAME->translator());
			// The open offer's grant would pay for the repeat, so do not advertise it.
			const auto followup = doubleCastLocked ? doubleCastCue::differentSpellHint()
				: owner->spellActionOpportunityText(mySpell->id);
			if(!followup.empty())
				statusText += " — " + followup;
			owner->statusBar->write(statusText);
		}
		else
			owner->statusBar->clear();
	}
}

void CSpellWindow::SpellArea::setSpell(const CSpell * spell)
{
	schoolBorder.reset();
	schoolLocked = false;
	divineMandateLocked = false;
	doubleCastLocked = false;
	schoolRequirementText.clear();
	divineMandateRequirementText.clear();
	image->visible = false;
	// setSpell reuses each slot; restore the legacy top-left frame origin before
	// selecting a new frame so a preceding centered PNG cannot shift a DEF icon.
	image->moveTo(pos.topLeft());
	name->setText("");
	effect->setText("");
	cost->setText("");
	mySpell = spell;
	if(mySpell)
	{

		SpellSchool whichSchool;
		schoolLevel = owner->myHero->getSpellSchoolLevel(mySpell, &whichSchool);


		image->setFrame(mySpell->id.getNum());
		image->visible = true;

		{
			OBJECT_CONSTRUCTION;

			schoolBorder.reset();
			if (owner->selectedTab == SpellSchool::ANY)
			{
				if (whichSchool.hasValue())
					schoolBorder = std::make_shared<CAnimImage>(LIBRARY->spellSchoolHandler->getById(whichSchool)->getSpellBordersPath(), schoolLevel);
			}
			else
				schoolBorder = std::make_shared<CAnimImage>(LIBRARY->spellSchoolHandler->getById(owner->selectedTab)->getSpellBordersPath(), schoolLevel);
		}

		// New spell icons are registered as independent PNG frames in SPELLS.
		// Keep legacy DEF frames at their original offsets, but center dynamic
		// frames in the school-border canvas (or the original icon canvas when no
		// border is available) using the dimensions actually loaded at runtime.
		if(!mySpell->getIconBook().empty())
		{
			constexpr int originalIconCanvasWidth = 78;
			constexpr int originalIconCanvasHeight = 65;
			Rect iconCanvas(pos.x, pos.y, originalIconCanvasWidth, originalIconCanvasHeight);
			if(schoolBorder && schoolBorder->pos.w > 0 && schoolBorder->pos.h > 0)
				iconCanvas = schoolBorder->pos;

			image->moveTo(Point(
				iconCanvas.x + (iconCanvas.w - image->pos.w) / 2,
				iconCanvas.y + (iconCanvas.h - image->pos.h) / 2));
		}

		refreshReadout();
	}
}
