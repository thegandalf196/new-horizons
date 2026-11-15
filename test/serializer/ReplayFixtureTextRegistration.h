/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace replayFixture
{
template<typename T> std::vector<std::byte> textRegistrationBytes(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

// TinyH3M bypasses the ordinary map-header/save string registration boundary.
// Match that boundary before an initial replay anchor, as in SaveParityTest.
inline void registerMapStrings(CMap & map)
{
	const auto owner = map.texts;
	ASSERT_NE(owner, nullptr);
	ASSERT_NE(owner.get(), static_cast<TextLocalizationContainer *>(LIBRARY->generaltexth.get()));
	std::map<std::string, ExportedStrings> original;
	owner->exportAllTexts(original, false);
	ASSERT_EQ(original.size(), 1u);
	ASSERT_TRUE(original.contains("core"));
	ASSERT_FALSE(original.at("core").strings.empty());
	EXPECT_TRUE(original.at("core").overridenMods.empty());
	const auto translations = textRegistrationBytes(map.translations);
	map.registerMapStrings();
	EXPECT_EQ(map.texts, owner);
	std::map<std::string, ExportedStrings> registered;
	owner->exportAllTexts(registered, false);
	ASSERT_EQ(registered.size(), 1u);
	ASSERT_TRUE(registered.contains("map"));
	EXPECT_EQ(registered.at("map").strings, original.at("core").strings);
	EXPECT_TRUE(registered.at("map").overridenMods.empty());
	EXPECT_EQ(textRegistrationBytes(map.translations), translations);
	const auto once = textRegistrationBytes(*owner);
	map.registerMapStrings();
	EXPECT_EQ(map.texts, owner);
	EXPECT_EQ(textRegistrationBytes(*owner), once);
	EXPECT_EQ(textRegistrationBytes(map.translations), translations);
}
}
