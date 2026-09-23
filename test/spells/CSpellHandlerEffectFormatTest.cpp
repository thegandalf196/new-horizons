/*
 * CSpellHandlerEffectFormatTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/logging/CLogger.h"
#include "../../lib/spells/CSpellHandler.h"

namespace test
{

namespace
{

class MixingWarningLogTarget final : public ILogTarget
{
public:
	explicit MixingWarningLogTarget(std::shared_ptr<std::vector<std::string>> messages)
		: messages(std::move(messages))
	{
	}

	void write(const LogRecord & record) override
	{
		if(record.message.find("Mixing ") != std::string::npos
			&& (record.message.find("offensiveSpecialOnlyProbe") != std::string::npos
				|| record.message.find("mixedFormatProbe") != std::string::npos))
			messages->push_back(record.message);
	}

private:
	std::shared_ptr<std::vector<std::string>> messages;
};

class TestSpellHandler final : public CSpellHandler
{
public:
	using CSpellHandler::loadFromJson;
};

JsonNode spellConfig(const std::string & name, bool includeLegacyEffects)
{
	JsonNode spell;
	spell["name"].String() = name;
	spell["type"].String() = "combat";
	spell["targetType"].String() = "CREATURE";
	spell["flags"]["damage"].Bool() = true;
	spell["flags"]["offensive"].Bool() = true;
	spell["flags"]["negative"].Bool() = true;

	for(const std::string levelName : {"none", "basic", "advanced", "expert"})
	{
		auto & level = spell["levels"][levelName];
		level["range"].String() = "0";
		level["battleEffects"]["directDamage"]["type"].String() = "damage";
	}

	if(includeLegacyEffects)
	{
		auto & legacy = spell["levels"]["none"]["effects"]["legacyDamageReduction"];
		legacy["type"].String() = "GENERAL_DAMAGE_REDUCTION";
		legacy["subtype"].String() = "damageTypeMelee";
		legacy["duration"].String() = "N_TURNS";
	}

	spell.setModScope("vcmi-test");
	return spell;
}

}

TEST(CSpellHandlerEffectFormatTest, OffensiveBattleEffectsAloneDoNotReportMixingButLegacyPayloadStillDoes)
{
	auto messages = std::make_shared<std::vector<std::string>>();
	CLogger::getGlobalLogger()->addTarget(std::make_unique<MixingWarningLogTarget>(messages));
	TestSpellHandler handler;

	auto specialOnly = handler.loadFromJson("vcmi-test", spellConfig("offensiveSpecialOnlyProbe", false), "offensiveSpecialOnlyProbe", 9000);
	ASSERT_TRUE(specialOnly->isOffensive());
	ASSERT_TRUE(specialOnly->isDamage());
	EXPECT_TRUE(messages->empty());

	auto mixed = handler.loadFromJson("vcmi-test", spellConfig("mixedFormatProbe", true), "mixedFormatProbe", 9001);
	ASSERT_TRUE(mixed->isOffensive());
	ASSERT_TRUE(mixed->isDamage());
	ASSERT_EQ(messages->size(), 1u);
	EXPECT_NE(messages->front().find("Mixing "), std::string::npos);
}

}
