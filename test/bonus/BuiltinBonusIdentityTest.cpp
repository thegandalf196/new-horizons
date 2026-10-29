/*
 * BuiltinBonusIdentityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CBonusTypeHandler.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/json/JsonBonus.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include <array>

namespace
{
Bonus input(BonusType type)
{
	return Bonus(BonusDuration::PERMANENT, type, BonusSource::OTHER, 1, BonusSourceID());
}
}

TEST(BuiltinBonusIdentityTest, AllBuiltinTokensKeepExactCanonicalIdentityWithOrWithoutDisplayRows)
{
#define BONUS_NAME(name) std::pair{BonusType::name, #name},
	const auto names = std::array{BONUS_LIST};
#undef BONUS_NAME
	for(const auto & [type, name] : names)
	{
		SCOPED_TRACE(name);
		EXPECT_EQ(LIBRARY->bth->bonusToString(type), name);
		if(type != BonusType::NONE)
		{
			EXPECT_EQ(BonusTypeID::encode(static_cast<int32_t>(type)), name);
			EXPECT_EQ(BonusTypeID::decode(name), static_cast<int32_t>(type));
		}
	}
}

TEST(BuiltinBonusIdentityTest, MissingDisplayMetadataCannotAliasJsonTypesAndBothParseBack)
{
	// These builtin types have no optional rows in the current bonuses.json.
	const auto first = input(BonusType::METAMAGIC_USES_PER_COMBAT);
	const auto second = input(BonusType::TIME_STOP);
	const auto firstJson = first.toJsonNode();
	const auto secondJson = second.toJsonNode();
	EXPECT_EQ(firstJson["type"].String(), "METAMAGIC_USES_PER_COMBAT");
	EXPECT_EQ(secondJson["type"].String(), "TIME_STOP");
	EXPECT_NE(firstJson, secondJson);
	// Apart from actual type, these are exactly the same semantic bonus shape.
	auto sameShape = secondJson;
	sameShape["type"] = firstJson["type"];
	EXPECT_EQ(firstJson, sameShape);
	const auto firstRestored = JsonUtils::parseBonus(firstJson);
	const auto secondRestored = JsonUtils::parseBonus(secondJson);
	ASSERT_NE(firstRestored, nullptr);
	ASSERT_NE(secondRestored, nullptr);
	EXPECT_EQ(firstRestored->type, first.type);
	EXPECT_EQ(secondRestored->type, second.type);
	EXPECT_EQ(firstRestored->toJsonNode(), firstJson);
	EXPECT_EQ(secondRestored->toJsonNode(), secondJson);
}

TEST(BuiltinBonusIdentityTest, LimiterJsonKeepsDistinctCanonicalDependencyAndRoundTrips)
{
	auto first = input(BonusType::STACKS_SPEED);
	auto second = first;
	first.limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::METAMAGIC_USES_PER_COMBAT);
	second.limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::TIME_STOP);
	EXPECT_NE(first.toJsonNode(), second.toJsonNode());
	for(const auto * original : {&first, &second})
	{
		const auto restored = JsonUtils::parseBonus(original->toJsonNode());
		ASSERT_NE(restored, nullptr);
		const auto * limiter = dynamic_cast<const HasAnotherBonusLimiter *>(restored->limiter.get());
		ASSERT_NE(limiter, nullptr);
		EXPECT_EQ(limiter->type, static_cast<const HasAnotherBonusLimiter *>(original->limiter.get())->type);
		EXPECT_EQ(restored->toJsonNode(), original->toJsonNode());
	}
}

TEST(BuiltinBonusIdentityTest, BinaryEnumStateRetainsEachIdentityWithoutJsonConversion)
{
	for(const auto type : {BonusType::METAMAGIC_USES_PER_COMBAT, BonusType::TIME_STOP, BonusType::MORALE})
	{
		auto original = input(type);
		CMemorySerializer writer;
		writer.oser.version = ESerializationVersion::CURRENT;
		writer.oser & original;
		CMemorySerializer reader(writer.extractBuffer());
		reader.iser.version = ESerializationVersion::CURRENT;
		Bonus restored;
		reader.iser & restored;
		EXPECT_EQ(restored.type, type);
		EXPECT_EQ(restored.val, original.val);
		EXPECT_EQ(restored.toJsonNode(), original.toJsonNode());
	}
}

TEST(BuiltinBonusIdentityTest, NoneEncodingAndOutOfRangeRefusalRemainUnchanged)
{
	EXPECT_TRUE(BonusTypeID::encode(static_cast<int32_t>(BonusType::NONE)).empty());
	EXPECT_EQ(BonusTypeID::decode(""), static_cast<int32_t>(BonusType::NONE));
	EXPECT_THROW(BonusTypeID::encode(std::numeric_limits<int32_t>::max()), std::out_of_range);
}

TEST(BuiltinBonusIdentityTest, PhysicalResistanceDisplayUsesActualBasisPointValue)
{
	auto bonus = std::make_shared<Bonus>(input(BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS));
	bonus->val = 7500;
	EXPECT_EQ(LIBRARY->bth->bonusToString(bonus), "Physical resistance: 75% less physical damage.");
	bonus->val = 1250;
	EXPECT_EQ(LIBRARY->bth->bonusToString(bonus), "Physical resistance: 12.5% less physical damage.");
}
