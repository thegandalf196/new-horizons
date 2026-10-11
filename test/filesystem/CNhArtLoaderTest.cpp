/*
 * CNhArtLoaderTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../../lib/filesystem/CNhArtLoader.h"
#include "../../lib/filesystem/NhArtSha256.h"
#include "../../lib/filesystem/NhArtCache.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/filesystem/CInputStream.h"
#include "../../lib/texts/TextOperations.h"

#include <array>
#include <fstream>
#include <future>
#include <limits>

namespace test
{
namespace
{
using Bytes = std::vector<ui8>;

struct Payload
{
	std::string name;
	Bytes data;
};

struct Archive
{
	Bytes data;
	size_t indexOffset;
	std::vector<size_t> records;
};

void putLE(Bytes & bytes, size_t at, ui64 value, size_t width)
{
	for(size_t i = 0; i < width; ++i)
		bytes.at(at + i) = static_cast<ui8>(value >> (8 * i));
}

void appendLE(Bytes & bytes, ui64 value, size_t width)
{
	const auto at = bytes.size();
	bytes.resize(at + width);
	putLE(bytes, at, value, width);
}

Archive makeArchive(std::vector<Payload> payloads)
{
	// Genuine synthetic self-contained manifest and record hashes. Original
	// requested rows retain their record indices; metadata is appended last.
	std::erase_if(payloads,[](const auto & row){return row.name==".nhart/manifest.json";});
	std::string metadata="{\"format\":1,\"entries\":[";
	for(size_t i=0;i<payloads.size();++i)
	{
		if(i)metadata+=",";
		// Malformed-name fixtures may contain arbitrary bytes; they are rejected
		// by index admission before their intentionally irrelevant JSON is read.
		metadata+="{\"resource\":\""+payloads[i].name+"\",\"size\":"+std::to_string(payloads[i].data.size())
			+",\"sha256\":\""+nhart::hex(nhart::sha256(payloads[i].data))+"\"}";
	}
	metadata+="]}";
	payloads.push_back({".nhart/manifest.json",Bytes(metadata.begin(),metadata.end())});
	Archive archive;
	archive.data.resize(32);
	const std::array<ui8, 8> magic{'N', 'H', 'A', 'R', 'T', '\r', '\n', 0x1a};
	std::copy(magic.begin(), magic.end(), archive.data.begin());
	putLE(archive.data, 8, 1, 4);
	std::vector<size_t> offsets;
	for(const auto & payload : payloads)
	{
		offsets.push_back(archive.data.size());
		archive.data.insert(archive.data.end(), payload.data.begin(), payload.data.end());
	}
	archive.indexOffset = archive.data.size();
	putLE(archive.data, 16, archive.indexOffset, 8);
	putLE(archive.data, 24, payloads.size(), 8);
	for(size_t i = 0; i < payloads.size(); ++i)
	{
		archive.records.push_back(archive.data.size());
		appendLE(archive.data, payloads[i].name.size(), 4);
		appendLE(archive.data, 0, 4);
		appendLE(archive.data, offsets[i], 8);
		appendLE(archive.data, payloads[i].data.size(), 8);
		const auto digest=nhart::sha256(payloads[i].data);
		archive.data.insert(archive.data.end(),digest.begin(),digest.end());
		archive.data.insert(archive.data.end(), payloads[i].name.begin(), payloads[i].name.end());
	}
	return archive;
}

Bytes syntheticPng()
{
	// Original synthetic one-pixel RGBA PNG, no external game artwork.
	return {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
		0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
		0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
		0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xd0, 0x0a, 0x6c, 0xf9,
		0x0f, 0x00, 0x03, 0xa7, 0x01, 0xff, 0xb5, 0xb3, 0x99, 0x8c, 0x00, 0x00,
		0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
}

Bytes syntheticDef()
{
	// One original, uncompressed one-pixel DEF frame: header, palette,
	// group0, one13-byte frame name, offset and32-byte sprite header.
	Bytes bytes;
	for(const auto value : {0x47, 1, 1, 1})
		appendLE(bytes, value, 4);
	bytes.insert(bytes.end(), 256 * 3, 42);
	for(const auto value : {0, 1, 0, 0})
		appendLE(bytes, value, 4);
	bytes.insert(bytes.end(), 13, 0);
	appendLE(bytes, bytes.size() + 4, 4);
	for(const auto value : {1, 0, 1, 1, 1, 1, 0, 0})
		appendLE(bytes, value, 4);
	bytes.push_back(10);
	return bytes;
}

Bytes readAll(CInputStream & stream)
{
	auto [buffer, size] = stream.readAll();
	return Bytes(buffer.get(), buffer.get() + size);
}
}

class CNhArtLoaderTest : public ::testing::Test
{
	boost::filesystem::path directory;
	unsigned int nextFile = 0;
protected:
	boost::filesystem::path cacheRoot() const { return directory/"cache"; }
	void SetUp() override
	{
		directory = boost::filesystem::temp_directory_path()
			/ boost::filesystem::unique_path("vcmi-nhart-test-%%%%-%%%%-%%%%");
		ASSERT_TRUE(boost::filesystem::create_directory(directory));
	}
	void TearDown() override
	{
		boost::filesystem::remove_all(directory);
	}
	boost::filesystem::path write(const Bytes & data)
	{
		const auto path = directory / (std::to_string(nextFile++) + ".nhart");
		std::ofstream file(path.string(), std::ios::binary);
		file.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
		file.close();
		if(!file)
			throw std::runtime_error("Failed to write synthetic NHART fixture");
		return path;
	}
	void reject(const Bytes & bytes)
	{
		const auto path = write(bytes);
		EXPECT_THROW(CNhArtLoader("", path,cacheRoot()), std::runtime_error);
	}
};

TEST_F(CNhArtLoaderTest, PngAndDefRemainExactBytesWithCaseAndTypeNamespaces)
{
	const auto png = syntheticPng();
	const auto def = syntheticDef();
	const auto path = write(makeArchive({{"Art/Same.png", png}, {"Art/Same.def", def}}).data);
	CNhArtLoader loader("Mount/", path,cacheRoot());
	EXPECT_EQ(loader.getMountPoint(), "Mount/");
	const ResourcePath image("mount/art/same", EResType::IMAGE);
	const ResourcePath animation("MOUNT/ART/SAME", EResType::ANIMATION);
	ASSERT_TRUE(loader.existsResource(image));
	ASSERT_TRUE(loader.existsResource(animation));
	EXPECT_FALSE(loader.existsResource(ResourcePath("Art/Same.png")));
	EXPECT_EQ(readAll(*loader.load(image)), png);
	EXPECT_EQ(readAll(*loader.load(animation)), def);
	EXPECT_THROW(loader.load(ResourcePath("Mount/Art/Absent.png")), std::out_of_range);
	EXPECT_EQ(loader.getFilteredFiles([](const auto & r) { return r.getType() == EResType::IMAGE; }),
		(std::unordered_set<ResourcePath>{image}));
	EXPECT_EQ(loader.getFullFileURI(image), TextOperations::filesystemPathToUtf8(path) + "/Art/Same.png");
	EXPECT_EQ(loader.getLastWriteTime(image), boost::filesystem::last_write_time(path));
}

TEST_F(CNhArtLoaderTest, ReadsSeeksAndSkipsStayWithinPayload)
{
	CNhArtLoader loader("", write(makeArchive({{"first.png", {1, 2, 3}}, {"second.png", {9, 8}}}).data),cacheRoot());
	auto stream = loader.load(ResourcePath("first.png"));
	std::array<ui8, 8> buffer{};
	EXPECT_EQ(stream->getSize(), 3);
	EXPECT_EQ(stream->read(buffer.data(), 8), 3);
	EXPECT_EQ((Bytes(buffer.begin(), buffer.begin() + 3)), (Bytes{1, 2, 3}));
	EXPECT_EQ(buffer[3], 0);
	EXPECT_EQ(stream->tell(), 3);
	EXPECT_EQ(stream->read(buffer.data(), 1), 0);
	EXPECT_EQ(stream->seek(1), 1);
	EXPECT_EQ(stream->read(buffer.data(), 1), 1);
	EXPECT_EQ(buffer[0], 2);
	EXPECT_EQ(stream->skip(-1), -1); // Valid backwards displacement, now at1.
	EXPECT_EQ(stream->tell(), 1);
	EXPECT_EQ(stream->skip(std::numeric_limits<si64>::max()), 2);
	EXPECT_EQ(stream->seek(std::numeric_limits<si64>::max()), 3);
	EXPECT_EQ(stream->seek(-1), -1);
	EXPECT_EQ(stream->skip(std::numeric_limits<si64>::min()), -1);
	EXPECT_EQ(stream->read(buffer.data(), -1), -1);
	EXPECT_EQ(stream->tell(), 3);
	EXPECT_EQ(stream->seek(0), 0);
	EXPECT_EQ(readAll(*stream), (Bytes{1, 2, 3}));
}

TEST_F(CNhArtLoaderTest, EmptyPayloadDoesNotExposeHeaderOrNeighbour)
{
	CNhArtLoader loader("", write(makeArchive({{"empty.png", {}}, {"next.png", {7}}}).data),cacheRoot());
	auto stream = loader.load(ResourcePath("empty.png"));
	ui8 sentinel = 99;
	EXPECT_EQ(stream->getSize(), 0);
	EXPECT_EQ(stream->read(&sentinel, 1), 0);
	EXPECT_EQ(sentinel, 99);
	EXPECT_EQ(stream->seek(8), 0);
	EXPECT_EQ(stream->skip(8), 0);
	EXPECT_EQ(stream->tell(), 0);
	CNhArtLoader empty("", write(makeArchive({}).data),cacheRoot());
	EXPECT_TRUE(empty.getFilteredFiles([](const auto &) { return true; }).empty());
}

TEST_F(CNhArtLoaderTest, ConcurrentStreamsHaveIndependentPositions)
{
	const auto png = syntheticPng();
	CNhArtLoader loader("", write(makeArchive({{"art.png", png}}).data),cacheRoot());
	auto parent = loader.load(ResourcePath("art.png"));
	ASSERT_EQ(parent->seek(5), 5);
	std::vector<std::future<Bytes>> reads;
	for(int i = 0; i < 8; ++i)
		reads.push_back(std::async(std::launch::async, [&loader] {
			auto stream = loader.load(ResourcePath("ART.PNG"));
			Bytes bytes(static_cast<size_t>(stream->getSize()));
			for(auto & byte : bytes)
				if(stream->read(&byte, 1) != 1)
					throw std::runtime_error("Independent stream truncated");
			return bytes;
		}));
	for(auto & read : reads)
		EXPECT_EQ(read.get(), png);
	EXPECT_EQ(parent->tell(), 5);
	EXPECT_EQ(readAll(*parent), png);
}

TEST_F(CNhArtLoaderTest, ManifestIsHiddenAndOrdinaryDotNamesAreResources)
{
	CNhArtLoader loader("", write(makeArchive({{".nhart/manifest.json", {'{', '}'}},
		{"art/.selected.png", {1}}, {"art/caf\xc3\xa9.png", {2}}}).data),cacheRoot());
	EXPECT_FALSE(loader.existsResource(ResourcePath(".nhart/manifest.json")));
	EXPECT_EQ(loader.getFilteredFiles([](const auto &) { return true; }).size(), 2u);
	EXPECT_EQ(readAll(*loader.load(ResourcePath("art/.selected.png"))), (Bytes{1}));
	EXPECT_TRUE(loader.existsResource(ResourcePath("art/caf\xc3\xa9.png")));
}

TEST_F(CNhArtLoaderTest, RejectsNoncanonicalReservedManifestAliases)
{
	for(const auto & name : {".NHART/MANIFEST.JSON", ".nhart/manifest.JSON"})
	{
		SCOPED_TRACE(name);
		reject(makeArchive({{name, {'{', '}'}}}).data);
	}
}

TEST_F(CNhArtLoaderTest, RejectsTruncatedHeaderBadMagicVersionAndFlags)
{
	const auto valid = makeArchive({{"image.png", {1}}}).data;
	for(const auto size : {0u, 7u, 31u})
		reject(Bytes(valid.begin(), valid.begin() + size));
	for(const auto field : {0u, 8u, 12u})
	{
		auto malformed = valid;
		malformed[field] ^= 0x40;
		reject(malformed);
	}
}

TEST_F(CNhArtLoaderTest, RejectsHeaderIndexBoundsAndImpossibleCounts)
{
	const auto valid = makeArchive({{"image.png", {1}}}).data;
	for(const auto offset : {ui64{0}, ui64{31}, ui64{valid.size() + 1}, std::numeric_limits<ui64>::max()})
	{
		auto malformed = valid;
		putLE(malformed, 16, offset, 8);
		reject(malformed);
	}
	for(const auto count : {ui64{3}, ui64{1000001}, std::numeric_limits<ui64>::max()})
	{
		auto malformed = valid;
		putLE(malformed, 24, count, 8);
		reject(malformed);
	}
}

TEST_F(CNhArtLoaderTest, RejectsTruncatedIndexNamesReservedBitsAndTrailingBytes)
{
	const auto valid = makeArchive({{"image.png", {1}}});
	for(const auto size : {valid.indexOffset + 55, valid.data.size() - 1})
		reject(Bytes(valid.data.begin(), valid.data.begin() + size));
	for(const auto length : {ui64{0}, ui64{4097}, std::numeric_limits<ui64>::max()})
	{
		auto malformed = valid.data;
		putLE(malformed, valid.records[0], length, 4);
		reject(malformed);
	}
	auto malformed = valid.data;
	putLE(malformed, valid.records[0] + 4, 1, 4);
	reject(malformed);
	malformed = valid.data;
	malformed.push_back(0);
	reject(malformed);
}

TEST_F(CNhArtLoaderTest, RejectsPayloadHeaderIndexCrossingAndUnsignedOverflow)
{
	const auto valid = makeArchive({{"image.png", {1, 2}}});
	for(const auto offset : {ui64{31}, ui64{valid.indexOffset + 1}, std::numeric_limits<ui64>::max()})
	{
		auto malformed = valid.data;
		putLE(malformed, valid.records[0] + 8, offset, 8);
		reject(malformed);
	}
	for(const auto length : {ui64{3}, std::numeric_limits<ui64>::max()})
	{
		auto malformed = valid.data;
		putLE(malformed, valid.records[0] + 16, length, 8);
		reject(malformed);
	}
}

TEST_F(CNhArtLoaderTest, RejectsOverlappingNonemptyPayloadsButAllowsAdjacency)
{
	const auto valid = makeArchive({{"first.png", {1, 2}}, {"second.png", {3, 4}}});
	EXPECT_NO_THROW(CNhArtLoader("", write(valid.data),cacheRoot()));
	auto malformed = valid.data;
	putLE(malformed, valid.records[1] + 8, 33, 8);
	reject(malformed);
}

TEST_F(CNhArtLoaderTest, RejectsNormalizedCaseAndImageExtensionCollisions)
{
	for(const auto & other : {"art/IMAGE.PNG", "Art/Image.pcx", "Art/Image.jpg"})
	{
		SCOPED_TRACE(other);
		reject(makeArchive({{"Art/Image.png", {1}}, {other, {2}}}).data);
	}
}

TEST_F(CNhArtLoaderTest, RejectsTraversalAbsoluteAndMalformedUtf8Names)
{
	const std::vector<std::string> names{"", "/image.png", "../image.png", "art/../image.png",
		"art/./image.png", "art//image.png", "art/image.png/", "C:/image.png", "art\\image.png",
		std::string("art/\0image.png", 14), "art/\xc0\xaf.png", "art/\xed\xa0\x80.png", "art/\xf4\x90\x80\x80.png", "art/\xc3.png"};
	for(const auto & name : names)
	{
		SCOPED_TRACE(name);
		reject(makeArchive({{name, {1}}}).data);
	}
}

TEST_F(CNhArtLoaderTest, Sha256PublishedKnownVectorsAndChunkBoundary)
{
	const auto digest=[](const std::string & value){return nhart::hex(nhart::sha256(std::span<const ui8>(reinterpret_cast<const ui8 *>(value.data()),value.size())));};
	EXPECT_EQ(digest(""),"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	EXPECT_EQ(digest("abc"),"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	EXPECT_EQ(digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	const std::string million(1000000,'a');
	EXPECT_EQ(digest(million),"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
	std::istringstream stream(million);
	EXPECT_EQ(nhart::hex(nhart::sha256(stream,million.size())),digest(million));
}

TEST_F(CNhArtLoaderTest, PreparedBackingAndExplicitDirectBenchmarkHaveIdenticalBytes)
{
	const auto payloads=makeArchive({{"art.png",syntheticPng()},{"art.def",syntheticDef()},{"empty.png",{}}});
	const auto path=write(payloads.data);
	CNhArtLoader cached("",path,cacheRoot());
	CNhArtLoader direct("",path,cacheRoot(),CNhArtLoader::Backing::DirectArchiveForTests);
	for(const auto & resource:cached.getFilteredFiles([](const auto &){return true;}))
		EXPECT_EQ(readAll(*cached.load(resource)),readAll(*direct.load(resource)));
	boost::filesystem::remove(path);
	EXPECT_EQ(readAll(*cached.load(ResourcePath("art.png"))),syntheticPng());
}

TEST_F(CNhArtLoaderTest, BothMountScopesSharePreparedBackingWithoutCollapsingIdentities)
{
	const auto path=write(makeArchive({{"DATA/value.json",{1,2}}}).data);
	CNhArtLoader builtin("",path,cacheRoot());
	CNhArtLoader mod("MODS/NEW-HORIZONS/",path,cacheRoot());
	EXPECT_TRUE(builtin.existsResource(ResourcePath("DATA/value.json")));
	EXPECT_TRUE(mod.existsResource(ResourcePath("MODS/NEW-HORIZONS/DATA/value.json")));
	EXPECT_FALSE(mod.existsResource(ResourcePath("DATA/value.json")));
	EXPECT_EQ(readAll(*builtin.load(ResourcePath("DATA/value.json"))),readAll(*mod.load(ResourcePath("MODS/NEW-HORIZONS/DATA/value.json"))));
}

TEST_F(CNhArtLoaderTest, CacheReuseRevalidatesCorruptedPayloadBeforeUse)
{
	const auto archive=makeArchive({{"image.png",{1,2,3}}});const auto path=write(archive.data);
	{CNhArtLoader first("",path,cacheRoot());}
	const auto payload=cacheRoot()/"new-horizons-art"/"v1"/nhart::hex(nhart::sha256(archive.data))/nhart::cacheFileName(0);
	std::ofstream corrupt(payload.string(),std::ios::binary|std::ios::trunc);corrupt.put(9);corrupt.close();
	EXPECT_THROW(CNhArtLoader("",path,cacheRoot()),std::runtime_error);
	EXPECT_TRUE(boost::filesystem::exists(payload)); // Do not delete/repair arbitrary existing trees.
}

TEST_F(CNhArtLoaderTest, CacheReuseRejectsExtraFiles)
{
	const auto archive=makeArchive({{"image.png",{1}}});const auto path=write(archive.data);
	{CNhArtLoader first("",path,cacheRoot());}
	const auto directory=cacheRoot()/"new-horizons-art"/"v1"/nhart::hex(nhart::sha256(archive.data));
	std::ofstream extra((directory/"unrelated.png").string());extra.put(1);extra.close();
	EXPECT_THROW(CNhArtLoader("",path,cacheRoot()),std::runtime_error);
	EXPECT_TRUE(boost::filesystem::exists(directory/"unrelated.png"));
}

TEST_F(CNhArtLoaderTest, RejectsPayloadAndEmbeddedManifestDigestCorruption)
{
	auto archive=makeArchive({{"image.png",{1,2,3}}});
	auto corrupt=archive.data;corrupt[32]^=1;reject(corrupt);
	corrupt=archive.data;corrupt[archive.records.back()+24]^=1;reject(corrupt);
}

TEST_F(CNhArtLoaderTest, RejectsWindowsDeviceTrailingDotSpaceAndControlNames)
{
	for(const auto & name:{"CON.png","art/NUL.json","LPT9.png","COM1.def","bad./art.png","bad /art.png","bad\x01.png"})
		reject(makeArchive({{name,{1}}}).data);
}

#ifndef _WIN32
TEST_F(CNhArtLoaderTest, DeveloperArchiveSymlinkResolvesSourceButNotCacheTargets)
{
	const auto source=write(makeArchive({{"image.png",{1,2}}}).data);
	const auto alias=source.parent_path()/"linked.nhart";
	boost::filesystem::create_symlink(source,alias);
	CNhArtLoader loader("",alias,cacheRoot());
	EXPECT_EQ(readAll(*loader.load(ResourcePath("image.png"))),(Bytes{1,2}));
	EXPECT_EQ(loader.getFullFileURI(ResourcePath("image.png")),TextOperations::filesystemPathToUtf8(alias)+"/image.png");
}

TEST_F(CNhArtLoaderTest, RejectsLinkedCacheRootWithoutWritingOutside)
{
	const auto outside=cacheRoot().parent_path()/"outside";
	ASSERT_TRUE(boost::filesystem::create_directory(outside));
	boost::filesystem::create_directory_symlink(outside,cacheRoot());
	EXPECT_THROW(CNhArtLoader("",write(makeArchive({{"image.png",{1}}}).data),cacheRoot()),std::runtime_error);
	EXPECT_TRUE(boost::filesystem::is_empty(outside));
}
#endif

TEST_F(CNhArtLoaderTest, CommittedPackSelfContainedManifestAndAllPayloadsMatchDirectBacking)
{
	const auto archive=CResourceHandler::get("initial")->getResourceName(ResourcePath("Mods/new-horizons/NewHorizons.nhart",EResType::ARCHIVE_NHART));
	ASSERT_TRUE(archive.has_value());
	CNhArtLoader prepared("",*archive,cacheRoot());
	CNhArtLoader direct("",*archive,cacheRoot(),CNhArtLoader::Backing::DirectArchiveForTests);
	const auto resources=prepared.getFilteredFiles([](const auto &){return true;});
	ASSERT_FALSE(resources.empty());
	EXPECT_EQ(resources,direct.getFilteredFiles([](const auto &){return true;}));
	EXPECT_FALSE(prepared.existsResource(ResourcePath(".nhart/manifest.json")));
	for(const auto & resource:resources)EXPECT_EQ(readAll(*prepared.load(resource)),readAll(*direct.load(resource)));
}
}
