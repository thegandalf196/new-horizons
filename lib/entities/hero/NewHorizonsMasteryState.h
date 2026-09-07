/*
 * NewHorizonsMasteryState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once
#include "NewHorizonsMasteryRules.h"
#include <vector>

namespace newHorizonsHeroes
{
struct DLL_LINKAGE MasterySelection
{
	SecondarySkill skill;
	MasteryOption option;
	uint32_t level = 0;
	uint64_t sequence = 0;

	template<typename Handler> void serialize(Handler & h)
	{
		h & skill;
		h & option;
		h & level;
		serializeMasterySequence(h, sequence);
	}
};

struct DLL_LINKAGE MasteryChoiceView
{
	MasterySelection selection;
	bool active = false;
};

struct DLL_LINKAGE MasteryView
{
	std::vector<MasteryChoiceView> choices;
	std::optional<MasteryOffer> pending;
	std::vector<SecondarySkill> eligibleNextLevel;
	std::vector<SecondarySkill> awaitingChoice;
};

/// Hero-owned saved progression. Mutators are for authoritative packet application
/// or initialization, never frontend input. Query ownership/topness remains an
/// additional server check before calling accept(). No bonuses are granted here.
class DLL_LINKAGE MasteryState
{
public:
	JsonNode rules;
	uint64_t lastSequence = 0;
	uint32_t eligibilityLevel = 0;
	bool artilleryEligible = false;
	std::optional<MasteryOffer> pending;
	std::vector<MasterySelection> selected;

	bool hasChoice(SecondarySkill skill) const;
	void validate() const;
	JsonNode toJson() const;
	static MasteryState fromJson(const JsonNode & node);
	void captureBeforeLevel(uint32_t nextLevel, int artilleryRank);
	std::optional<MasteryOffer> prepareOffer(ObjectInstanceID hero, PlayerColor player, uint32_t level) const;
	void applyOffer(const MasteryOffer & offer);
	MasteryReplyError accept(ObjectInstanceID hero, PlayerColor player, uint64_t sequence,
		uint32_t level, int skillRank, int choice);

	template<typename Handler> void serialize(Handler & h)
	{
		h & rules;
		serializeMasterySequence(h, lastSequence);
		h & eligibilityLevel;
		h & artilleryEligible;
		h & pending;
		h & selected;
		if(!h.saving)
			validate();
	}
};
}
