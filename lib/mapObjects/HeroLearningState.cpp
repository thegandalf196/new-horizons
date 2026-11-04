/*
 * HeroLearningState.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../callback/IGameInfoCallback.h"
#include "../spells/NewHorizonsMagic.h"

bool CGHeroInstance::isValidNewHorizonsLearningMentorState(ObjectInstanceID heroId, int32_t week,
	const LearningMentorRecipients & recipients)
{
	if(week < -1)
		return false;
	bool foundEmpty = false;
	for(const auto recipientId : recipients)
	{
		if(recipientId == ObjectInstanceID::NONE)
		{
			foundEmpty = true;
			continue;
		}
		if(week == -1 || !heroId.hasValue() || !recipientId.hasValue()
			|| recipientId == heroId || foundEmpty)
			return false;
	}
	return recipients[0] == ObjectInstanceID::NONE || recipients[0] != recipients[1];
}

bool CGHeroInstance::isValidNewHorizonsScholarState(ObjectInstanceID hero, int32_t week, const std::vector<ObjectInstanceID> & partners)
{
	if(week < -1 || (week == -1) != partners.empty())
		return false;
	ObjectInstanceID previous = ObjectInstanceID::NONE;
	for(const auto partner : partners)
	{
		if(!hero.hasValue() || !partner.hasValue() || partner == hero || partner <= previous)
			return false;
		previous = partner;
	}
	return true;
}

void CGHeroInstance::markNewHorizonsScholarMeeting(ObjectInstanceID partner, int32_t week)
{
	if(!id.hasValue() || !partner.hasValue() || partner == id || week < 0 || week < newHorizonsScholarWeek
		|| !isValidNewHorizonsScholarState(id, newHorizonsScholarWeek, newHorizonsScholarPartners))
		throw std::runtime_error("Invalid New Horizons Scholar meeting");
	if(week != newHorizonsScholarWeek)
	{
		newHorizonsScholarWeek = week;
		newHorizonsScholarPartners.clear();
	}
	const auto position = std::lower_bound(newHorizonsScholarPartners.begin(), newHorizonsScholarPartners.end(), partner);
	if(position == newHorizonsScholarPartners.end() || *position != partner)
		newHorizonsScholarPartners.insert(position, partner);
}

bool CGHeroInstance::hasNewHorizonsScholarMeeting(ObjectInstanceID partner, int32_t week) const
{
	return week == newHorizonsScholarWeek
		&& std::binary_search(newHorizonsScholarPartners.begin(), newHorizonsScholarPartners.end(), partner);
}

bool CGHeroInstance::canExchangeNewHorizonsScholarWith(const CGHeroInstance & partner, int32_t week) const
{
	if(!cb || !newHorizonsMagic::rulesActive(getMagicRules())
		|| !id.hasValue() || !partner.id.hasValue() || id == partner.id || week < 0
		|| cb->getPlayerRelations(getOwner(), partner.getOwner()) == PlayerRelations::ENEMIES)
		return false;
	if(!hasActivePerk("new-horizons:learning", "new-horizons:learning.scholar")
		&& !partner.hasActivePerk("new-horizons:learning", "new-horizons:learning.scholar"))
		return false;
	return getNewHorizonsScholarSpellFor(partner).has_value() || partner.getNewHorizonsScholarSpellFor(*this).has_value();
}

void CGHeroInstance::setNewHorizonsLearningMentorState(int32_t week, const LearningMentorRecipients & recipients)
{
	if(!isValidNewHorizonsLearningMentorState(id, week, recipients))
		throw std::runtime_error("Invalid New Horizons Learning Mentor state");
	newHorizonsLearningMentorLastWeek = week;
	newHorizonsLearningMentorRecipients = recipients;
}

TExpType CGHeroInstance::getNewHorizonsLearningMentorExperiencePerLevel() const
{
	const std::string learningSkillId = "new-horizons:learning";
	const auto rank = getPerkSkillRank(learningSkillId);
	if(rank >= MasteryLevel::BASIC && hasActivePerk(learningSkillId, "new-horizons:learning.mentor"))
		return 250;
	return 0;
}

bool CGHeroInstance::hasNewHorizonsLearningMentorUseFor(ObjectInstanceID recipientId, int32_t week) const
{
	const auto experiencePerLevel = getNewHorizonsLearningMentorExperiencePerLevel();
	if(week < 0 || !id.hasValue() || !recipientId.hasValue() || recipientId == id
		|| experiencePerLevel == 0
		|| !isValidNewHorizonsLearningMentorState(id, newHorizonsLearningMentorLastWeek,
			newHorizonsLearningMentorRecipients))
		return false;
	if(newHorizonsLearningMentorLastWeek != week)
		return true;
	// A legacy used-week marker has unknown recipients: never refill it on load or resave.
	if(newHorizonsLearningMentorRecipients[0] == ObjectInstanceID::NONE)
		return false;
	if(std::find(newHorizonsLearningMentorRecipients.begin(), newHorizonsLearningMentorRecipients.end(),
		recipientId) != newHorizonsLearningMentorRecipients.end())
		return false;
	return false;
}

bool CGHeroInstance::canGrantNewHorizonsLearningMentorTo(const CGHeroInstance & recipient, int32_t week) const
{
	return this != &recipient && level > recipient.level && hasNewHorizonsLearningMentorUseFor(recipient.id, week);
}
