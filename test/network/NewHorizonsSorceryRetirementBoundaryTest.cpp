/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/serializer/GameConnectionProtocol.h"
#include "../../lib/serializer/CLoadFile.h"
#include <boost/filesystem.hpp>
#include <fstream>

TEST(NewHorizonsSorceryRetirementBoundary, RejectsMissingOldAndFutureProtocolBeforePayload)
{
	std::vector<std::byte> bytes;
	EXPECT_THROW(gameConnectionProtocol::validateHeader(bytes, ESerializationVersion::CURRENT), std::runtime_error);
	gameConnectionProtocol::appendHeader(bytes, ESerializationVersion::CURRENT);
	bytes.push_back(std::byte{0xff}); // Intentionally invalid polymorphic payload; header validation never reads it.
	EXPECT_NO_THROW(gameConnectionProtocol::validateHeader(bytes, ESerializationVersion::CURRENT));
	const auto setWireVersion = [&bytes](uint32_t version)
	{
		for(unsigned index = 0; index < sizeof(version); ++index)
			bytes[sizeof(gameConnectionProtocol::MAGIC) + index]
				= static_cast<std::byte>((version >> (index * 8)) & 0xff);
	};
	setWireVersion(static_cast<uint32_t>(ESerializationVersion::NEW_HORIZONS_ESTATES));
	EXPECT_THROW(gameConnectionProtocol::validateHeader(bytes, ESerializationVersion::CURRENT), std::runtime_error);
	setWireVersion(static_cast<uint32_t>(ESerializationVersion::CURRENT) + 1);
	EXPECT_THROW(gameConnectionProtocol::validateHeader(bytes, ESerializationVersion::CURRENT), std::runtime_error);
	bytes[0] = std::byte{0};
	EXPECT_THROW(gameConnectionProtocol::validateHeader(bytes, ESerializationVersion::CURRENT), std::runtime_error);
}

TEST(NewHorizonsSorceryRetirementBoundary, UnsupportedProtocolCannotAppendEvenAPrefix)
{
	std::vector<std::byte> bytes{std::byte{0x5a}};
	const auto original = bytes;
	EXPECT_THROW(gameConnectionProtocol::appendHeader(bytes, ESerializationVersion::NEW_HORIZONS_ESTATES), std::runtime_error);
	EXPECT_EQ(bytes, original);
}

TEST(NewHorizonsSorceryRetirementBoundary, OldSaveRejectsBeforePayloadAndPreservesFile)
{
	const auto path = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("nh-retirement-%%%%-%%%%.sav");
	struct Cleanup
	{
		boost::filesystem::path path;
		~Cleanup() { boost::system::error_code ignored; boost::filesystem::remove(path, ignored); }
	} cleanup{path};
	const auto oldVersion = ESerializationVersion::NEW_HORIZONS_ESTATES;
	std::string bytes("VCMI");
	bytes.append(reinterpret_cast<const char *>(&oldVersion), sizeof(oldVersion));
	bytes.append("invalid retired payload");
	{
		std::ofstream output(path.string(), std::ios::binary);
		output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	}
	try
	{
		CLoadFile rejected(path, nullptr);
		FAIL() << "Retired save was admitted";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_NE(std::string(error.what()).find("Sorcery and Metamagic redesign"), std::string::npos);
	}
	std::ifstream input(path.string(), std::ios::binary);
	const std::string preserved((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	EXPECT_EQ(preserved, bytes);
}
