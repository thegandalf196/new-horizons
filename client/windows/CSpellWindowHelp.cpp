/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CSpellWindow.h"
#include "SpellbookEntryPresentation.h"
#include "InfoWindows.h"
#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../battle/BattleInterface.h"
#include "../battle/DoubleCastCue.h"
#include "../widgets/CComponent.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/SpellCostBreakdown.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
const char * spellCostStageTextKey(SpellCostStage::Kind kind)
{
	switch(kind)
	{
		case SpellCostStage::Kind::LISTED_MULTIPLIER: return "new-horizons.spellCost.modifier.listedMultiplier";
		case SpellCostStage::Kind::CHOSEN_SPECIALTY: return "new-horizons.spellCost.modifier.chosenSpecialty";
		case SpellCostStage::Kind::WISDOM: return "new-horizons.spellCost.modifier.wisdom";
		case SpellCostStage::Kind::ADVENTURE_ARTIFACT: return "new-horizons.spellCost.modifier.adventureArtifact";
		case SpellCostStage::Kind::KNIGHTLY_SEQUENCE: return "new-horizons.spellCost.modifier.knightlySequence";
		case SpellCostStage::Kind::SPELL_FAMILIARITY: return "new-horizons.spellCost.modifier.spellFamiliarity";
		case SpellCostStage::Kind::ALLIED_ARMY: return "new-horizons.spellCost.modifier.alliedArmy";
		case SpellCostStage::Kind::ENEMY_ARMY: return "new-horizons.spellCost.modifier.enemyArmy";
		case SpellCostStage::Kind::MINIMUM_COST: return "new-horizons.spellCost.modifier.minimumCost";
		case SpellCostStage::Kind::METAMAGIC_ARCANE_ECONOMY: return "new-horizons.spellCost.modifier.arcaneEconomy";
		case SpellCostStage::Kind::METAMAGIC_SPELLSTORM: return "new-horizons.spellCost.modifier.spellstorm";
	}
	return "new-horizons.spellCost.current";
}

bool hasSpellCostBreakdown(const SpellCostBreakdown & breakdown)
{
	return breakdown.listedCost != breakdown.finalCost || !breakdown.stages.empty();
}

std::string spellCostBreakdownText(const SpellCostBreakdown & breakdown)
{
	if(!hasSpellCostBreakdown(breakdown))
		return {};

	MetaString listed = MetaString::createFromTextID("new-horizons.spellCost.listed");
	listed.replaceNumber(breakdown.listedCost);
	MetaString current = MetaString::createFromTextID("new-horizons.spellCost.current");
	current.replaceNumber(breakdown.finalCost);

	std::string result = listed.toString(&GAME->translator()) + "\n" + current.toString(&GAME->translator());
	for(const auto & stage : breakdown.stages)
	{
		MetaString stageText = MetaString::createFromTextID("new-horizons.spellCost.stage");
		stageText.replaceRawString(LIBRARY->generaltexth->translate(spellCostStageTextKey(stage.kind)));
		stageText.replaceNumber(stage.before);
		stageText.replaceNumber(stage.after);
		result += "\n" + stageText.toString(&GAME->translator());
	}
	return result;
}
}

void CSpellWindow::SpellArea::showPopupWindow(const Point & cursorPosition)
{
	if(mySpell)
	{
		const auto breakdown = refreshReadout();
		const auto heading = spellbookPresentation::levelAndSchools(owner->spellLevels.at(mySpell->getId()),
			schoolLevel, owner->spellSchools.at(mySpell->getId()), GAME->translator());
		const auto battle = owner->myInt->battleInt ? owner->myInt->battleInt->getBattle() : nullptr;
		const auto army = spellbookPresentation::heavenlyGaleArmyPreview(*owner->myHero, *mySpell, battle.get());
		const auto currentEffect = army ? spellbookPresentation::armyEffectDetail(*army, GAME->translator())
			: spellbookPresentation::effectDetail(effectPreview, GAME->translator());
		const auto currentCost = army ? "Mana: " + std::to_string(army->manaCost) : spellbookPresentation::costLine(breakdown, GAME->translator(),
				[](const std::string &) { return 0; });

		std::string requirements;
		if(schoolLocked)
			requirements += "\n\n" + schoolRequirementText;
		if(divineMandateLocked)
			requirements += "\n\n" + divineMandateRequirementText;
		if(doubleCastLocked)
			requirements += "\n\n" + doubleCastCue::differentSpellHelp(mySpell->id);
		const auto followup = doubleCastLocked ? std::string() : owner->spellActionOpportunityText(mySpell->id);
		const auto followupInfo = followup.empty() ? std::string() : "\n\n" + followup;
		std::string costInfo;
		if(!army && newHorizonsMagic::rulesActive(owner->myHero->getMagicRules()))
		{
			costInfo = spellCostBreakdownText(breakdown);
			if(!costInfo.empty())
				costInfo = "\n\n" + costInfo;

			if(newHorizonsMagic::magicArrowOverchargeEnabled(owner->myHero->getMagicRules(), mySpell->id))
				costInfo += (costInfo.empty() ? "\n\n" : "\n")
					+ LIBRARY->generaltexth->translate("new-horizons.spellCost.overcharge");
		}
		const auto fullDescription = army
			? "Protect the friendly army from physical ranged projectiles, including siege shots. "
				"Melee attacks, spells, magical beams and explosions are unaffected."
			: newHorizonsMagic::spellDescriptionForHero(owner->myHero, mySpell, schoolLevel);
		const auto summary = army ? std::string() : newHorizonsMagic::spellSummaryForHero(owner->myHero, mySpell, schoolLevel);
		const auto description = summary.empty() || summary == fullDescription
			? fullDescription : summary + "\n\n" + fullDescription;
		CRClickPopup::createAndPush(heading + "\n\n" + description
			+ "\n\n" + currentCost + "\n" + currentEffect
			+ costInfo + requirements + followupInfo,
			std::make_shared<CComponent>(ComponentType::SPELL, mySpell->id));
	}
}
