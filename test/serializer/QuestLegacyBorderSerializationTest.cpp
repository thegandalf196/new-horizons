/*
 * QuestLegacyBorderSerializationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapObjects/Quest.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/SerializerReflection.h"

#include <array>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
using Version = ESerializationVersion;
constexpr std::array legacyVersions{Version::MINIMAL, Version::CONTROL_LOSS_TRACKING};
constexpr uint32_t trailingMarker = 0x12345678;
static_assert(Version::MINIMAL <= Version::CONTROL_LOSS_TRACKING);
static_assert(Version::CONTROL_LOSS_TRACKING < Version::QUEST_REWORK);

template<typename T>
std::vector<std::byte> currentBytes(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

void setBase(CGObjectInstance & object, MapObjectID type, int colour)
{
	object.ID = type;
	object.subID = MapObjectSubID(colour);
	object.id = ObjectInstanceID(27);
	object.pos = int3(8, 9, 0);
	object.tempOwner = PlayerColor(1);
	object.blockVisit = true;
	object.removable = true;
	object.instanceName = "legacy-border-source";
}

void setDestination(QuestSource & object)
{
	setBase(object, Obj::BORDER_GATE, 6);
	object.instanceName = "preserved-destination";
	object.pos = int3(12, 13, 1);
	auto & quest = object.addQuest();
	quest.lastDay = 45;
	quest.mission.heroLevel = 7;
	quest.mission.requiredKeys = {MapObjectSubID(6)};
	quest.defineQuestName();
}

std::shared_ptr<Quest> legacyQuest()
{
	auto quest = std::make_shared<Quest>();
	quest->lastDay = 19;
	quest->mission.heroLevel = 9;
	quest->activeForPlayers.insert(PlayerColor(0));
	quest->firstVisitText.appendRawString("Legacy quest text");
	quest->defineQuestName();
	return quest;
}

void writeLegacyPayload(CMemorySerializer & memory, Version version,
	const std::shared_ptr<Quest> & quest, const CGObjectInstance & base)
{
	// Build the historical payload directly with the real binary writer; a
	// current QuestGate writer must not be used to manufacture an old record.
	memory.oser.version = version;
	memory.oser & quest;
	memory.oser & base;
	memory.oser & trailingMarker;
	memory.iser.version = version;
}

void expectUnconsumedBase(CMemorySerializer & memory, const CGObjectInstance & expected)
{
	QuestGate decoded(nullptr);
	ASSERT_NO_THROW(memory.iser & static_cast<CGObjectInstance &>(decoded));
	EXPECT_EQ(currentBytes(static_cast<const CGObjectInstance &>(decoded)), currentBytes(expected));
	uint32_t marker = 0;
	ASSERT_NO_THROW(memory.iser & marker);
	EXPECT_EQ(marker, trailingMarker);
}

void expectConverted(const QuestSource & actual, const CGObjectInstance & base, const Quest & quest)
{
	EXPECT_EQ(currentBytes(static_cast<const CGObjectInstance &>(actual)), currentBytes(base));
	ASSERT_EQ(actual.allQuests().size(), 1u);
	auto expected = quest;
	expected.mission.requiredKeys.push_back(base.subID);
	EXPECT_EQ(actual.getQuest().mission.requiredKeys, (std::vector<MapObjectSubID>{base.subID}));
	EXPECT_EQ(currentBytes(actual.getQuest()), currentBytes(expected));
}
}

TEST(QuestLegacyBorderSerializationTest, OldDirectGateWriterRejectsBeforeBytesOrDestinationMutation)
{
	for(const auto version : legacyVersions)
	{
		SCOPED_TRACE(static_cast<int>(version));
		QuestGate destination(nullptr);
		setDestination(destination);
		const auto before = currentBytes(destination);
		const auto * originalQuest = destination.allQuests().front().get();
		CMemorySerializer memory;
		memory.oser.version = version;
		EXPECT_THROW(memory.oser & destination, std::runtime_error);
		EXPECT_TRUE(memory.extractBuffer().empty());
		EXPECT_EQ(currentBytes(destination), before);
		ASSERT_EQ(destination.allQuests().size(), 1u);
		EXPECT_EQ(destination.allQuests().front().get(), originalQuest);
	}
}

TEST(QuestLegacyBorderSerializationTest, DirectHelperWriterIsLoadOnlyEvenWithCurrentVersion)
{
	QuestGuard destination(nullptr);
	setDestination(destination);
	const auto before = currentBytes(destination);
	CMemorySerializer memory;
	EXPECT_THROW(loadLegacyBorderGuard(memory.oser, destination), std::runtime_error);
	EXPECT_TRUE(memory.extractBuffer().empty());
	EXPECT_EQ(currentBytes(destination), before);
}

TEST(QuestLegacyBorderSerializationTest, NullLegacyGateReaderRejectsBeforeBaseOrQuestMutation)
{
	for(const auto version : legacyVersions)
	{
		SCOPED_TRACE(static_cast<int>(version));
		QuestGate base(nullptr);
		setBase(base, Obj::BORDER_GATE, 3);
		QuestGate destination(nullptr);
		setDestination(destination);
		const auto before = currentBytes(destination);
		const auto * originalQuest = destination.allQuests().front().get();
		CMemorySerializer memory;
		writeLegacyPayload(memory, version, {}, base);
		EXPECT_THROW(memory.iser & destination, std::runtime_error);
		EXPECT_EQ(currentBytes(destination), before);
		ASSERT_EQ(destination.allQuests().size(), 1u);
		EXPECT_EQ(destination.allQuests().front().get(), originalQuest);
		ASSERT_NO_FATAL_FAILURE(expectUnconsumedBase(memory, base));
	}
}

TEST(QuestLegacyBorderSerializationTest, NullLegacyGuardTypeTenRejectsBeforeBaseOrQuestMutation)
{
	for(const auto version : legacyVersions)
	{
		SCOPED_TRACE(static_cast<int>(version));
		QuestGuard base(nullptr);
		setBase(base, Obj::BORDERGUARD, 4);
		QuestGuard destination(nullptr);
		setDestination(destination);
		const auto before = currentBytes(destination);
		const auto * originalQuest = destination.allQuests().front().get();
		CMemorySerializer memory;
		writeLegacyPayload(memory, version, {}, base);
		auto * loader = CSerializationApplier::getInstance().getApplier(10);
		EXPECT_THROW(loader->loadPtr(memory.iser, nullptr, &destination), std::runtime_error);
		EXPECT_EQ(currentBytes(destination), before);
		ASSERT_EQ(destination.allQuests().size(), 1u);
		EXPECT_EQ(destination.allQuests().front().get(), originalQuest);
		ASSERT_NO_FATAL_FAILURE(expectUnconsumedBase(memory, base));
	}
}

TEST(QuestLegacyBorderSerializationTest, ValidLegacyGateRetainsQuestAndSynthesizesEachRequiredKey)
{
	for(const auto version : legacyVersions)
		for(int colour = 0; colour < 8; ++colour)
		{
			SCOPED_TRACE(static_cast<int>(version));
			SCOPED_TRACE(colour);
			QuestGate base(nullptr);
			setBase(base, Obj::BORDER_GATE, colour);
			const auto quest = legacyQuest();
			CMemorySerializer memory;
			writeLegacyPayload(memory, version, quest, base);
			QuestGate destination(nullptr);
			ASSERT_NO_THROW(memory.iser & destination);
			ASSERT_NO_FATAL_FAILURE(expectConverted(destination, base, *quest));
			uint32_t marker = 0;
			ASSERT_NO_THROW(memory.iser & marker);
			EXPECT_EQ(marker, trailingMarker);
		}
}

TEST(QuestLegacyBorderSerializationTest, ValidLegacyGuardTypeTenRetainsQuestAndSynthesizesEachRequiredKey)
{
	for(const auto version : legacyVersions)
		for(int colour = 0; colour < 8; ++colour)
		{
			SCOPED_TRACE(static_cast<int>(version));
			SCOPED_TRACE(colour);
			QuestGuard base(nullptr);
			setBase(base, Obj::BORDERGUARD, colour);
			const auto quest = legacyQuest();
			CMemorySerializer memory;
			writeLegacyPayload(memory, version, quest, base);
			QuestGuard destination(nullptr);
			auto * loader = CSerializationApplier::getInstance().getApplier(10);
			ASSERT_NO_THROW(loader->loadPtr(memory.iser, nullptr, &destination));
			ASSERT_NO_FATAL_FAILURE(expectConverted(destination, base, *quest));
			uint32_t marker = 0;
			ASSERT_NO_THROW(memory.iser & marker);
			EXPECT_EQ(marker, trailingMarker);
		}
}

TEST(QuestLegacyBorderSerializationTest, CurrentGateRoundtripRetainsExistingRequiredKeysAndLayout)
{
	QuestGate source(nullptr);
	setDestination(source);
	source.getQuest().mission.requiredKeys.push_back(MapObjectSubID(2));
	CMemorySerializer memory;
	memory.oser & source;
	memory.oser & trailingMarker;
	QuestGate destination(nullptr);
	ASSERT_NO_THROW(memory.iser & destination);
	ASSERT_EQ(destination.allQuests().size(), 1u);
	EXPECT_EQ(destination.getQuest().mission.requiredKeys, source.getQuest().mission.requiredKeys);
	EXPECT_EQ(currentBytes(destination), currentBytes(source));
	uint32_t marker = 0;
	ASSERT_NO_THROW(memory.iser & marker);
	EXPECT_EQ(marker, trailingMarker);
}
