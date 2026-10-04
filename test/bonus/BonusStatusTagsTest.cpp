/*
 * BonusStatusTagsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/json/JsonBonus.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"

namespace
{
Bonus makeStatusBonus(const std::string & identity, const si32 value, const PlayerColor caster)
{
	Bonus result(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, value, BonusSourceID(SpellID(SpellID::SLOW)));
	result.turnsRemain = 2;
	result.spellCasterOwner = caster;
	result.appliedByEnemy = true;
	if(!identity.empty())
	{
		result.statusTags.push_back(BonusStatusTag::DEBUFF);
		result.statusIdentity = identity;
	}
	return result;
}

void expectStatusMetadata(const Bonus & actual, const Bonus & expected)
{
	EXPECT_EQ(actual.statusTags, expected.statusTags);
	EXPECT_EQ(actual.statusIdentity, expected.statusIdentity);
}
}

TEST(BonusStatusTagsTest, JsonRoundTripRejectsUnknownDuplicateAndMalformedMetadata)
{
	Bonus tagged = makeStatusBonus("test.slow.primary", -4, PlayerColor(0));
	const JsonNode json = tagged.toJsonNode();
	ASSERT_EQ(json["statusTags"].getType(), JsonNode::JsonType::DATA_VECTOR);
	ASSERT_EQ(json["statusTags"].Vector().size(), 1u);
	EXPECT_EQ(json["statusTags"][0].String(), "DEBUFF");
	EXPECT_EQ(json["statusIdentity"].String(), "test.slow.primary");

	const auto parsed = JsonUtils::parseBonus(json);
	ASSERT_NE(parsed, nullptr);
	EXPECT_EQ(parsed->statusTags, tagged.statusTags);
	EXPECT_EQ(parsed->statusIdentity, tagged.statusIdentity);
	EXPECT_EQ(parsed->val, tagged.val);
	EXPECT_EQ(parsed->spellCasterOwner, tagged.spellCasterOwner);

	Bonus ordinary(BonusDuration::PERMANENT, BonusType::MORALE, BonusSource::OTHER, 1, BonusSourceID());
	const auto ordinaryParsed = JsonUtils::parseBonus(ordinary.toJsonNode());
	ASSERT_NE(ordinaryParsed, nullptr);
	EXPECT_TRUE(ordinaryParsed->statusTags.empty());
	EXPECT_TRUE(ordinaryParsed->statusIdentity.empty());

	auto unknownTag = json;
	unknownTag["statusTags"][0].String() = "UNRECOGNIZED";
	EXPECT_THROW(JsonUtils::parseBonus(unknownTag), std::runtime_error);

	auto nonStringTag = json;
	nonStringTag["statusTags"][0] = JsonNode(1);
	EXPECT_THROW(JsonUtils::parseBonus(nonStringTag), std::runtime_error);

	auto nullTags = json;
	nullTags["statusTags"] = JsonNode();
	EXPECT_THROW(JsonUtils::parseBonus(nullTags), std::runtime_error);

	auto duplicateTag = json;
	duplicateTag["statusTags"].Vector().emplace_back("DEBUFF");
	EXPECT_THROW(JsonUtils::parseBonus(duplicateTag), std::runtime_error);

	auto malformedIdentity = json;
	malformedIdentity["statusIdentity"].String() = "test\nidentity";
	EXPECT_THROW(JsonUtils::parseBonus(malformedIdentity), std::runtime_error);

	auto nonStringIdentity = json;
	nonStringIdentity["statusIdentity"] = JsonNode(4);
	EXPECT_THROW(JsonUtils::parseBonus(nonStringIdentity), std::runtime_error);

	auto untaggedIdentity = json;
	untaggedIdentity["statusTags"].Vector().clear();
	EXPECT_THROW(JsonUtils::parseBonus(untaggedIdentity), std::runtime_error);

	auto overlongIdentity = json;
	overlongIdentity["statusIdentity"].String() = std::string(Bonus::MAX_STATUS_IDENTITY_LENGTH + 1, 'x');
	EXPECT_THROW(JsonUtils::parseBonus(overlongIdentity), std::runtime_error);

	Bonus invalidTag = tagged;
	invalidTag.statusTags = {static_cast<BonusStatusTag>(255)};
	EXPECT_THROW(invalidTag.toJsonNode(), std::runtime_error);
	Bonus invalidIdentity = tagged;
	invalidIdentity.statusTags.clear();
	EXPECT_THROW(invalidIdentity.toJsonNode(), std::runtime_error);
}

TEST(BonusStatusTagsTest, BonusAndStackEffectPacketRoundTripAndRejectLossyDownsave)
{
	const Bonus tagged = makeStatusBonus("test.slow.primary", -4, PlayerColor(0));
	Bonus invalidTag = tagged;
	invalidTag.statusTags = {static_cast<BonusStatusTag>(255)};
	CMemorySerializer invalidWriter;
	invalidWriter.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(invalidWriter.oser & invalidTag, std::runtime_error);
	EXPECT_TRUE(invalidWriter.extractBuffer().empty());
	CMemorySerializer currentBonus;
	currentBonus.oser.version = ESerializationVersion::CURRENT;
	currentBonus.oser & tagged;
	const auto savedBonusBytes = currentBonus.extractBuffer();
	CMemorySerializer currentBonusReader(savedBonusBytes);
	currentBonusReader.iser.version = ESerializationVersion::CURRENT;
	Bonus restoredBonus;
	currentBonusReader.iser & restoredBonus;
	expectStatusMetadata(restoredBonus, tagged);
	EXPECT_EQ(restoredBonus.spellCasterOwner, PlayerColor(0));
	EXPECT_TRUE(restoredBonus.appliedByEnemy);

	const auto previousVersion = ESerializationVersion::NEW_HORIZONS_PUPPET_MASTER_CONTROL;
	const Bonus untagged = makeStatusBonus("", -3, PlayerColor(0));
	CMemorySerializer previousBonusWriter;
	previousBonusWriter.oser.version = previousVersion;
	previousBonusWriter.oser & untagged;
	const auto previousBonusBytes = previousBonusWriter.extractBuffer();
	CMemorySerializer previousBonusReader(previousBonusBytes);
	previousBonusReader.iser.version = previousVersion;
	Bonus legacyBonus;
	legacyBonus.statusTags.push_back(BonusStatusTag::DEBUFF);
	legacyBonus.statusIdentity = "stale.status";
	previousBonusReader.iser & legacyBonus;
	EXPECT_TRUE(legacyBonus.statusTags.empty());
	EXPECT_TRUE(legacyBonus.statusIdentity.empty());
	EXPECT_EQ(legacyBonus.spellCasterOwner, PlayerColor(0));

	CMemorySerializer oldBonusWriter;
	oldBonusWriter.oser.version = previousVersion;
	EXPECT_THROW(oldBonusWriter.oser & tagged, std::runtime_error);
	EXPECT_TRUE(oldBonusWriter.extractBuffer().empty())
		<< "A Bonus carrying status metadata must be rejected before any older-version bytes are emitted";

	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(17, std::vector<Bonus>{tagged});

	CMemorySerializer currentPacket;
	currentPacket.oser.version = ESerializationVersion::CURRENT;
	currentPacket.oser & packet;
	const auto savedPacketBytes = currentPacket.extractBuffer();
	CMemorySerializer currentPacketReader(savedPacketBytes);
	currentPacketReader.iser.version = ESerializationVersion::CURRENT;
	SetStackEffect restoredPacket;
	currentPacketReader.iser & restoredPacket;
	ASSERT_EQ(restoredPacket.toAdd.size(), 1u);
	ASSERT_EQ(restoredPacket.toAdd.front().second.size(), 1u);
	expectStatusMetadata(restoredPacket.toAdd.front().second.front(), tagged);

	SetStackEffect untaggedPacket;
	untaggedPacket.battleID = BattleID(0);
	untaggedPacket.toAdd.emplace_back(17, std::vector<Bonus>{untagged});
	CMemorySerializer previousPacketWriter;
	previousPacketWriter.oser.version = previousVersion;
	previousPacketWriter.oser & untaggedPacket;
	const auto previousPacketBytes = previousPacketWriter.extractBuffer();
	CMemorySerializer previousPacketReader(previousPacketBytes);
	previousPacketReader.iser.version = previousVersion;
	SetStackEffect legacyPacket;
	previousPacketReader.iser & legacyPacket;
	ASSERT_EQ(legacyPacket.toAdd.size(), 1u);
	ASSERT_EQ(legacyPacket.toAdd.front().second.size(), 1u);
	EXPECT_TRUE(legacyPacket.toAdd.front().second.front().statusTags.empty());
	EXPECT_TRUE(legacyPacket.toAdd.front().second.front().statusIdentity.empty());

	CMemorySerializer oldPacketWriter;
	oldPacketWriter.oser.version = previousVersion;
	EXPECT_THROW(oldPacketWriter.oser & packet, std::runtime_error);
	EXPECT_TRUE(oldPacketWriter.extractBuffer().empty())
		<< "A stack-effect packet carrying status metadata must be rejected before its BattleID is written";
}

class BonusStatusTagsRefreshTest : public BattleTestFixture
{
};

TEST_F(BonusStatusTagsRefreshTest, RefreshMergesTagsWithoutReplacingStrengthOrProvenanceAndSeparatesIdentities)
{
	startGame();
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 10);
	ASSERT_NE(stack, nullptr);

	const Bonus original = makeStatusBonus("", -3, PlayerColor(0));
	battle()->addOrUpdateUnitBonus(stack, original, true);

	Bonus refresh = makeStatusBonus("", -18, PlayerColor(1));
	refresh.statusTags.push_back(BonusStatusTag::DEBUFF);
	refresh.turnsRemain = 5;
	battle()->addOrUpdateUnitBonus(stack, refresh, false);

	std::vector<std::shared_ptr<Bonus>> matching;
	for(const auto & bonus : stack->getExportedBonusList())
		if(bonus->source == original.source && bonus->sid == original.sid
			&& bonus->type == original.type && bonus->statusIdentity.empty())
			matching.push_back(bonus);

	ASSERT_EQ(matching.size(), 1u);
	EXPECT_EQ(matching.front()->val, original.val);
	EXPECT_EQ(matching.front()->turnsRemain, 5);
	EXPECT_EQ(matching.front()->spellCasterOwner, original.spellCasterOwner);
	EXPECT_EQ(matching.front()->appliedByEnemy, original.appliedByEnemy);
	ASSERT_EQ(matching.front()->statusTags.size(), 1u);
	EXPECT_EQ(matching.front()->statusTags.front(), BonusStatusTag::DEBUFF);

	Bonus separateIdentity = makeStatusBonus("test.slow.secondary", -6, PlayerColor(1));
	separateIdentity.appliedByEnemy = false;
	battle()->addOrUpdateUnitBonus(stack, separateIdentity, false);

	std::vector<std::shared_ptr<Bonus>> sameSourceAndType;
	for(const auto & bonus : stack->getExportedBonusList())
		if(bonus->source == original.source && bonus->sid == original.sid
			&& bonus->type == original.type)
			sameSourceAndType.push_back(bonus);
	ASSERT_EQ(sameSourceAndType.size(), 2u)
		<< "Different explicit status identities from the same source/SID must not refresh into one component";
	const auto secondary = std::find_if(sameSourceAndType.begin(), sameSourceAndType.end(), [](const auto & bonus)
	{
		return bonus->statusIdentity == "test.slow.secondary";
	});
	ASSERT_NE(secondary, sameSourceAndType.end());
	EXPECT_EQ((*secondary)->val, separateIdentity.val);
	EXPECT_EQ((*secondary)->spellCasterOwner, separateIdentity.spellCasterOwner);
}

TEST_F(BonusStatusTagsRefreshTest, UntaggedIdentityDoesNotCollapseOrClearAnExplicitTaggedStatus)
{
	startGame();
	startBattle();
	auto * stack = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 10);
	ASSERT_NE(stack, nullptr);

	const Bonus tagged = makeStatusBonus("test.slow.primary", -3, PlayerColor(0));
	battle()->addOrUpdateUnitBonus(stack, tagged, true);

	const Bonus untagged = makeStatusBonus("", -18, PlayerColor(1));
	battle()->addOrUpdateUnitBonus(stack, untagged, false);

	std::vector<std::shared_ptr<Bonus>> matching;
	for(const auto & bonus : stack->getExportedBonusList())
		if(bonus->source == tagged.source && bonus->sid == tagged.sid && bonus->type == tagged.type)
			matching.push_back(bonus);
	ASSERT_EQ(matching.size(), 2u);

	const auto taggedEntry = std::find_if(matching.begin(), matching.end(), [](const auto & bonus)
	{
		return bonus->statusIdentity == "test.slow.primary";
	});
	const auto untaggedEntry = std::find_if(matching.begin(), matching.end(), [](const auto & bonus)
	{
		return bonus->statusIdentity.empty();
	});
	ASSERT_NE(taggedEntry, matching.end());
	ASSERT_NE(untaggedEntry, matching.end());
	EXPECT_EQ((*taggedEntry)->val, tagged.val);
	EXPECT_EQ((*taggedEntry)->spellCasterOwner, tagged.spellCasterOwner);
	EXPECT_EQ((*taggedEntry)->statusTags, tagged.statusTags);
	EXPECT_EQ((*untaggedEntry)->val, untagged.val);
	EXPECT_TRUE((*untaggedEntry)->statusTags.empty());
}
