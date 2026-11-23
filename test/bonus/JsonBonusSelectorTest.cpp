/*
 * JsonBonusSelectorTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/json/JsonBonus.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"
#include "../../lib/spells/CSpellHandler.h"

namespace
{
JsonNode selectorJson(const std::string & type, const std::string & subtype)
{
	JsonNode json;
	if(!type.empty())
		json["type"].String() = type;
	if(!subtype.empty())
		json["subtype"].String() = subtype;
	json.setModScope(ModScope::scopeBuiltin());
	return json;
}

bool matches(const CSelector & selector, BonusType type, BonusSubtypeID subtype,
	BonusSource source = BonusSource::OTHER, BonusSourceID sourceID = BonusSourceID())
{
	const Bonus bonus(BonusDuration::PERMANENT, type, source, 1, sourceID, subtype);
	return selector(&bonus);
}

/// Swaps in an empty identifier storage that is still loading, so that requests made by the parser stay
/// pending until the test finalizes it - the way content parsed before every mod is read resolves.
class LoadingIdentifiers
{
	std::unique_ptr<CIdentifierStorage> loaded;
public:
	LoadingIdentifiers()
		: loaded(std::move(LIBRARY->identifiersHandler))
	{
		LIBRARY->identifiersHandler = std::make_unique<CIdentifierStorage>();
	}
	~LoadingIdentifiers()
	{
		LIBRARY->identifiersHandler = std::move(loaded);
	}
	LoadingIdentifiers(const LoadingIdentifiers &) = delete;
	LoadingIdentifiers & operator=(const LoadingIdentifiers &) = delete;

	CIdentifierStorage & storage()
	{
		return *LIBRARY->identifiersHandler;
	}
};

constexpr int32_t lateCreature = 7;
constexpr int32_t otherLateCreature = 8;
}

TEST(JsonBonusSelectorTest, SubtypeMatchesOnlyThatSubtypeOfThatType)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto selector = JsonUtils::parseSelector(selectorJson("PRIMARY_SKILL", "attack"));

	EXPECT_TRUE(matches(selector, BonusType::PRIMARY_SKILL, PrimarySkill::ATTACK));
	EXPECT_FALSE(matches(selector, BonusType::PRIMARY_SKILL, PrimarySkill::DEFENSE));
	EXPECT_FALSE(matches(selector, BonusType::PRIMARY_SKILL, BonusSubtypeID()));
	EXPECT_FALSE(matches(selector, BonusType::MORALE, PrimarySkill::ATTACK));
}

TEST(JsonBonusSelectorTest, TypeWithoutSubtypeMatchesEverySubtypeOfThatTypeOnly)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto selector = JsonUtils::parseSelector(selectorJson("PRIMARY_SKILL", ""));

	EXPECT_TRUE(matches(selector, BonusType::PRIMARY_SKILL, PrimarySkill::ATTACK));
	EXPECT_TRUE(matches(selector, BonusType::PRIMARY_SKILL, PrimarySkill::DEFENSE));
	EXPECT_FALSE(matches(selector, BonusType::MORALE, BonusSubtypeID()));
}

TEST(JsonBonusSelectorTest, UnknownTypeOrSubtypeMatchesNothing)
{
	const auto unknownType = JsonUtils::parseSelector(selectorJson("NO_SUCH_BONUS_TYPE", ""));
	const auto unknownSubtype = JsonUtils::parseSelector(selectorJson("PRIMARY_SKILL", "noSuchSkill"));

	EXPECT_FALSE(matches(unknownType, BonusType::NONE, BonusSubtypeID()));
	EXPECT_FALSE(matches(unknownSubtype, BonusType::PRIMARY_SKILL, BonusSubtypeID()));
	EXPECT_FALSE(matches(unknownSubtype, BonusType::PRIMARY_SKILL, PrimarySkill::ATTACK));
}

TEST(JsonBonusSelectorTest, SourceIdMatchesOnlyThatSourceInstance)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	JsonNode json;
	json["sourceType"].String() = "SPELL_EFFECT";
	json["sourceID"].String() = "bless";
	json.setModScope(ModScope::scopeBuiltin());
	const auto selector = JsonUtils::parseSelector(json);

	EXPECT_TRUE(matches(selector, BonusType::MORALE, BonusSubtypeID(), BonusSource::SPELL_EFFECT, SpellID(SpellID::BLESS)));
	EXPECT_FALSE(matches(selector, BonusType::MORALE, BonusSubtypeID(), BonusSource::SPELL_EFFECT, SpellID(SpellID::CURSE)));
	EXPECT_FALSE(matches(selector, BonusType::MORALE, BonusSubtypeID(), BonusSource::ARTIFACT, SpellID(SpellID::BLESS)));
}

TEST(JsonBonusSelectorTest, DeferredSubtypeResolvesIntoTheSelectorAfterTheParsedJsonIsGone)
{
	LoadingIdentifiers identifiers;
	identifiers.storage().registerObject(ModScope::scopeBuiltin(), "bonus", "HATE", static_cast<int32_t>(BonusType::HATE));

	CSelector selector;
	{
		// the json and the parsing call both end before the identifiers resolve
		selector = JsonUtils::parseSelector(selectorJson("HATE", "lateCreature"));
	}
	EXPECT_FALSE(matches(selector, BonusType::HATE, CreatureID(lateCreature))) << "unresolved selector must match nothing";

	// named by a mod that is read only after the selector was parsed
	identifiers.storage().registerObject(ModScope::scopeBuiltin(), "creature", "lateCreature", lateCreature);
	identifiers.storage().registerObject(ModScope::scopeBuiltin(), "creature", "otherLateCreature", otherLateCreature);
	identifiers.storage().finalize();

	EXPECT_TRUE(matches(selector, BonusType::HATE, CreatureID(lateCreature)));
	EXPECT_FALSE(matches(selector, BonusType::HATE, CreatureID(otherLateCreature)));
	EXPECT_FALSE(matches(selector, BonusType::HATE, BonusSubtypeID()));
	EXPECT_FALSE(matches(selector, BonusType::MORALE, CreatureID(lateCreature)));
}

TEST(JsonBonusSelectorTest, DeferredRequestsOutliveADiscardedSelector)
{
	LoadingIdentifiers identifiers;
	identifiers.storage().registerObject(ModScope::scopeBuiltin(), "bonus", "HATE", static_cast<int32_t>(BonusType::HATE));

	JsonNode json = selectorJson("HATE", "lateCreature");
	json["sourceType"].String() = "CREATURE_ABILITY";
	json["sourceID"].String() = "lateCreature";
	json.setModScope(ModScope::scopeBuiltin());
	JsonUtils::parseSelector(json);

	// resolving writes only into state the pending requests own themselves
	identifiers.storage().registerObject(ModScope::scopeBuiltin(), "creature", "lateCreature", lateCreature);
	EXPECT_NO_THROW(identifiers.storage().finalize());
}

TEST(JsonBonusSelectorTest, NoLoadedSpellTargetConditionUsesTheStructuredSelectorForm)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	// Audit pin: spell target conditions are the only content that reaches parseSelector, and only
	// through a structured entry. None is shipped - the subtype fix changes no current gameplay - and
	// any added later needs its intended subtype behaviour reviewed.
	for(const auto & spell : LIBRARY->spellh->objects)
	{
		if(!spell)
			continue;
		const JsonNode & conditions = spell->targetCondition;
		for(const std::string mode : {"anyOf", "allOf", "noneOf"})
		{
			if(!conditions[mode].isStruct())
				continue;
			for(const auto & entry : conditions[mode].Struct())
				EXPECT_TRUE(entry.second.isString()) << spell->getJsonKey() << " " << mode << " " << entry.first;
		}
	}
}
