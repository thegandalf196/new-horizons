/*
 * NewHorizonsHeroRulesFixture.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/CHeroClassHandler.h"
#include "../../lib/GameLibrary.h"

inline JsonNode testHeroRules()
{
	JsonNode rules;
	rules["schemaVersion"].Integer() = 1;
	rules["rulesetVersion"].Integer() = 1;
	rules["powerDivisor"].Integer() = 10;
	rules["maxPrimary"].Integer() = 1000000;
	JsonNode profile;
	for(int value : {15, 20, 5, 10})
		profile["starting"].Vector().push_back(JsonNode(value));
	for(int value : {4, 4, 1, 1})
		profile["growth"].Vector().push_back(JsonNode(value));
	for(const auto & heroClass : LIBRARY->heroclassesh->objects)
		rules["classProfiles"][heroClass->getJsonKey()] = profile;
	for(const auto & [skill, primary] : {std::pair<SecondarySkill, PrimarySkill>{SecondarySkill::OFFENCE, PrimarySkill::ATTACK},
		std::pair<SecondarySkill, PrimarySkill>{SecondarySkill::ARMORER, PrimarySkill::DEFENSE}})
	{
		JsonNode extra;
		extra["skill"].String() = SecondarySkill::encode(skill.getNum());
		extra["primary"].Integer() = primary.getNum();
		for(int value : {0, 100, 100, 100})
			extra["chances"].Vector().push_back(JsonNode(value));
		rules["extraGrowth"].Vector().push_back(extra);
	}
	return rules;
}
