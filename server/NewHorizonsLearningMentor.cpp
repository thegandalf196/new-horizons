/*
 * NewHorizonsLearningMentor.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGameHandler.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/entities/hero/NewHorizonsLearningSpecialties.h"
#include "../lib/entities/creature/NewHorizonsMusterRules.h"
#include "../lib/networkPacks/PacksForClient.h"
std::optional<CGameHandler::LearningMentorAward> CGameHandler::prepareLearningMentorAward(
	const CGHeroInstance * first, const CGHeroInstance * second)
{
	if(!first || !second || first == second || first->id == second->id)
		return std::nullopt;
	if(gameInfo().getPlayerRelations(first->getOwner(), second->getOwner()) == PlayerRelations::ENEMIES)
		return std::nullopt;

	const auto week = newHorizonsMuster::absoluteWeek(gameInfo().getCalendar().getCurrentDay(),
		gameInfo().getCalendar().getDaysInWeek());

	const auto tryMentor = [&](const CGHeroInstance * mentor, const CGHeroInstance * recipient)
		-> std::optional<LearningMentorAward>
	{
		const int32_t mentorLevel = mentor->level;
		const int32_t recipientLevel = recipient->level;
		if(!mentor->canGrantNewHorizonsLearningMentorTo(*recipient, week))
			return std::nullopt;

		const auto baseExperiencePerMentorLevel = mentor->getNewHorizonsLearningMentorExperiencePerLevel();
		const auto baseExperience = newHorizonsHeroes::mentorSpecialtyAward(mentor, baseExperiencePerMentorLevel * mentorLevel);
		return LearningMentorAward{
			mentor->id,
			recipient->id,
			mentorLevel,
			recipientLevel,
			week,
			baseExperiencePerMentorLevel,
			baseExperience,
			recipient->calculateXp(baseExperience)
		};
	};

	if(auto award = tryMentor(first, second))
		return award;
	return tryMentor(second, first);
}

void CGameHandler::grantLearningMentorAward(const std::optional<LearningMentorAward> & award)
{
	if(!award || award->mentorLevelAtMeeting <= award->recipientLevelAtMeeting)
		return;

	const auto * mentor = gameInfo().getHero(award->mentorId);
	const auto * recipient = gameInfo().getHero(award->recipientId);
	const auto currentWeek = newHorizonsMuster::absoluteWeek(gameInfo().getCalendar().getCurrentDay(),
		gameInfo().getCalendar().getDaysInWeek());
	if(!mentor || !recipient || award->week != currentWeek
		|| gameInfo().getPlayerRelations(mentor->getOwner(), recipient->getOwner()) == PlayerRelations::ENEMIES
		|| !mentor->hasNewHorizonsLearningMentorUseFor(recipient->id, award->week)
		|| mentor->getNewHorizonsLearningMentorExperiencePerLevel() != award->experiencePerMentorLevel
		|| newHorizonsHeroes::mentorSpecialtyAward(mentor, award->experiencePerMentorLevel * award->mentorLevelAtMeeting) != award->rawExperience)
		return;

	SetNewHorizonsLearningMentorState state;
	state.heroId = award->mentorId;
	state.lastUseWeek = award->week;
	if(mentor->getNewHorizonsLearningMentorLastWeek() == award->week)
		state.recipientIds = mentor->getNewHorizonsLearningMentorRecipients();
	const auto nextRecipient = std::find(state.recipientIds.begin(), state.recipientIds.end(), ObjectInstanceID::NONE);
	if(nextRecipient == state.recipientIds.end())
		return;
	*nextRecipient = recipient->id;
	// Mark before Experience is applied so nested encounter paths cannot spend
	// the same weekly use again while a level-up query is being created.
	sendAndApply(state);
	giveExperience(recipient, award->experience);
}
