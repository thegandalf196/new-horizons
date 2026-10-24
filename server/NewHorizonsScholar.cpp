/*
 * NewHorizonsScholar.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsScholar.h"
#include "CGameHandler.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/entities/hero/NewHorizonsLearningExchangeSpecialties.h"
#include "../lib/entities/creature/NewHorizonsMusterRules.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/spells/NewHorizonsMagic.h"
#include <algorithm>
#include <set>
namespace newHorizonsScholar
{
namespace
{
void notify(CGameHandler & handler, const CGHeroInstance & learner, const CGHeroInstance & teacher,
	const std::vector<SpellID> & spells)
{
	if(spells.empty())
		return;
	InfoWindow notice;
	notice.player = learner.getOwner();
	notice.text.appendTextID(learner.getNameTextID());
	notice.text.appendRawString(" learned through Scholar from ");
	notice.text.appendTextID(teacher.getNameTextID());
	notice.text.appendRawString(": ");
	for(size_t i = 0; i < spells.size(); ++i)
	{
		if(i != 0)
			notice.text.appendRawString(", ");
		notice.text.appendName(spells[i]);
		notice.components.emplace_back(ComponentType::SPELL, spells[i]);
	}
	notice.text.appendRawString(".");
	handler.sendAndApply(notice);
}
}
void execute(CGameHandler & handler, const CGHeroInstance & first, const CGHeroInstance & second)
{
	if(!newHorizonsMagic::rulesActive(handler.gameInfo().getMagicRules()))
		return;
	const auto & calendar = handler.gameInfo().getCalendar();
	const auto week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	const auto plan = newHorizonsHeroes::prepareScholarExchange(first, second, week);
	if(!plan)
		return;
	SetNewHorizonsScholarMeeting receipt;
	receipt.first = std::min(first.id, second.id);
	receipt.second = std::max(first.id, second.id);
	receipt.week = week;
	handler.sendAndApply(receipt);
	if(!plan->firstTeaches.empty())
		handler.changeSpells(&second, true, std::set<SpellID>(plan->firstTeaches.begin(), plan->firstTeaches.end()));
	if(!plan->secondTeaches.empty())
		handler.changeSpells(&first, true, std::set<SpellID>(plan->secondTeaches.begin(), plan->secondTeaches.end()));
	if(plan->directedBonus)
	{
		notify(handler, second, first, plan->firstTeaches);
		notify(handler, first, second, plan->secondTeaches);
	}
}
}
