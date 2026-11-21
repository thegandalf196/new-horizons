/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CSpellWindow.h"
#include "SpellbookEntryPresentation.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../battle/BattleInterface.h"
#include "../battle/DoubleCastCue.h"
#include "../widgets/TextControls.h"
#include "render/IFont.h"
#include "render/IRenderHandler.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/texts/CGeneralTextHandler.h"

SpellCostBreakdown CSpellWindow::SpellArea::refreshReadout()
{
	const auto access = spellbookPresentation::access(*owner->myHero, *mySpell, GAME->translator());
	schoolLocked = access.schoolLocked;
	schoolRequirementText = access.requirement;
	divineMandateLocked = !owner->canUseSpellForCurrentDivineMandateFollowup(mySpell->getId());
	divineMandateRequirementText = divineMandateLocked
		? LIBRARY->generaltexth->translate("new-horizons.spellbook.lightFollowupOnly") : "";
	doubleCastLocked = owner->repeatsDoubleCastSpell(mySpell->getId());
	const auto breakdown = owner->myInt->cb->getSpellCostBreakdown(mySpell, owner->myHero);
	const auto battle = owner->myInt->battleInt ? owner->myInt->battleInt->getBattle() : nullptr;
	effectPreview = newHorizonsMagic::spellEffectPreview(*owner->myHero, *mySpell, battle.get());
	const int selectionCost = breakdown.finalCost;
	const bool unavailable = divineMandateLocked || doubleCastLocked || owner->isAdventureSpellUsedToday(mySpell->id)
		|| ((selectionCost > owner->myHero->getManaAvailable() || schoolLocked) && !owner->onSpellSelect);
	name->color = unavailable ? ColorRGBA(62, 52, 44) : ColorRGBA(40, 24, 12);
	cost->color = effect->color = unavailable ? ColorRGBA(90, 24, 14) : ColorRGBA(56, 34, 18);
	const auto font = ENGINE->renderHandler().loadFont(FONT_TINY);
	const auto width = [&font](const std::string & text) { return static_cast<int>(font->getStringWidth(text)); };
	name->setText(mySpell->getNameTranslated());
	cost->setText(spellbookPresentation::costLine(breakdown, GAME->translator(), width));
	effect->setText(spellbookPresentation::effectLine(effectPreview, GAME->translator(), width));
	return breakdown;
}

bool CSpellWindow::repeatsDoubleCastSpell(SpellID spell) const
{
	// Choosing a quick-spell slot is configuration, not a cast.
	if(!battleSpellsOnly || onSpellSelect || !myInt->battleInt)
		return false;
	try
	{
		return doubleCastCue::repeatsFirstSpell(myInt->battleInt->getBattle().get(), spell);
	}
	catch(const std::runtime_error &)
	{
		return false; // The battle ended while this view is closing; the authority still decides.
	}
}

std::string CSpellWindow::followUpRefusalText(SpellID spell) const
{
	if(!canUseSpellForCurrentDivineMandateFollowup(spell))
		return currentDivineMandateFollowupText() + "\nThis Spell cannot use that follow-up.";
	return repeatsDoubleCastSpell(spell) ? doubleCastCue::differentSpellHelp(spell) : std::string();
}
