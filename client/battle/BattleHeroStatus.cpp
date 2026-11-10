/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleWindow.h"
#include "BattleInterface.h"
#include "HeroInfoWindow.h"
#include "PassiveFactionSkillPresentation.h"
#include "../windows/InfoWindows.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

void BattleWindow::refreshHeroBattleStatus(BattleSide side)
{
	const auto battleCallback = owner.getBattle();
	if(!battleCallback)
		return;
	const auto * battle = battleCallback->getBattle();
	if(!battle)
		return;

	std::vector<CombatStatusEntry> entries;
	const bool counterspellArmed = battleCallback->battleWasCounterspellArmed(side);
	if(counterspellArmed)
	{
		entries.push_back({{}, "Ward", "ARMED",
			CInfoWindow::genText("Counterspell Ward", "The Counterspell ward is armed for this side.")});
	}

	const auto * visibleHero = battleCallback->battleGetFightingHero(side);

	if(const auto * hero = visibleHero)
	{
		if(const auto attunement = passiveFactionSkillPresentation::elementalAttunement(*hero, *battleCallback, side))
			entries.push_back({{}, "Attunement", attunement->value,
				CInfoWindow::genText("Elemental Attunement", attunement->details)});
		for(const auto & skill : LIBRARY->skillh->objects)
		{
			if(!skill || skill->getCombatStatusProvider() == CSkill::CombatStatusProvider::NONE)
				continue;
			const int skillRank = hero->getPerkSkillRank(skill->getJsonKey());
			if(skillRank <= 0)
				continue;

			std::string value;
			std::string providerDetails;
			bool replaceProviderDescription = false;
			switch(skill->getCombatStatusProvider())
			{
				case CSkill::CombatStatusProvider::METAMAGIC_USES:
				{
					const auto readout = passiveFactionSkillPresentation::metamagic(*hero, *battleCallback, side);
					if(!readout)
						continue;
					value = readout->value;
					providerDetails = readout->details;
					replaceProviderDescription = readout->replacesProviderDescription;
					break;
				}
				case CSkill::CombatStatusProvider::BLOODRAGE_DAMAGE:
				{
					const int cap = battle->getBloodrageCapPercent(side);
					if(cap <= 0)
						continue;
					const int current = std::clamp(battle->getBloodrageDamagePercent(side), 0, cap);
					// Keep current/cap legible in the compact 42 px value column.
					// The localized tooltip carries the fully expanded explanation.
					value = "+" + std::to_string(current) + "/" + std::to_string(cap) + "%";
					break;
				}
				case CSkill::CombatStatusProvider::DIVINE_MANDATE_USES:
				{
					const auto readout = passiveFactionSkillPresentation::divineMandate(*hero, *battleCallback, side);
					if(!readout)
						continue;
					value = readout->value;
					providerDetails = readout->details;
					replaceProviderDescription = readout->replacesProviderDescription;
					break;
				}
				case CSkill::CombatStatusProvider::NONE:
					continue;
			}
			const auto description = replaceProviderDescription ? std::string{}
				: skill->getCombatStatusDescriptionTranslated();
			const auto details = providerDetails.empty()
				? (description.empty() ? value : description + "\n\n" + value)
				: (description.empty() ? providerDetails : description + "\n\n" + providerDetails);
			entries.push_back({skill->at(std::clamp(skillRank, 1, 3)).iconSmall,
				skill->getNameTranslated(), value, CInfoWindow::genText(skill->getNameTranslated(), details)});
		}
	}

	const bool showNormalHeroAction = battleCallback->battleUsesHeroCommands()
		&& battle->getSideHero(side) != nullptr;
	const auto actionCounts = showNormalHeroAction
		? battleCallback->battleHeroActionAllowanceCounts(side)
		: HeroActionAllowanceState::Counts{};
	const bool normalHeroActionAvailable = showNormalHeroAction && actionCounts.heroActions > 0;

	const auto panel = side == BattleSide::ATTACKER ? attackerHeroWindow : defenderHeroWindow;
	if(panel)
		panel->setBattleStatus(entries, normalHeroActionAvailable, showNormalHeroAction);

	const auto statusArea = side == BattleSide::ATTACKER ? attackerHeroStatus : defenderHeroStatus;
	if(statusArea)
		statusArea->setStatus(entries, normalHeroActionAvailable, showNormalHeroAction);
}

int BattleWindow::heroBattleStatusHeight(BattleSide side) const
{
	if(settings["battle"]["stickyHeroInfoWindows"].Bool())
	{
		const auto panel = side == BattleSide::ATTACKER ? attackerHeroWindow : defenderHeroWindow;
		return panel ? panel->battleStatusHeight() : 0;
	}

	const auto statusArea = side == BattleSide::ATTACKER ? attackerHeroStatus : defenderHeroStatus;
	return statusArea ? statusArea->statusHeight() : 0;
}
