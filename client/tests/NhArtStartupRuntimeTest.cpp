/*
 * CPU-only NHART startup/cache runtime probe, part of New Horizons.
 * GPL-2.0-or-later; see license.txt. Never initializes SDL or GameLibrary.
 * Uses only a fresh owned child of an explicit existing disposable cache root.
 * Caller paths and diagnostic cache are never automatically deleted.
 */
#include "../../lib/StdInc.h"
#include "../../lib/filesystem/CNhArtLoader.h"
#include "../../lib/filesystem/CInputStream.h"
#include "../../lib/filesystem/NhArtSha256.h"

#include <fstream>
#include <chrono>
#include <iostream>
#include <sstream>

namespace
{
using Bytes = std::vector<ui8>;
constexpr size_t EXPECTED_RESOURCES = 3137;
constexpr const char * EXPECTED_PACK_SHA256 = "1a91c08b1efa146af5d8f7df0d8a99a5739c94e280fedb13e8fd4d11ae555fc8";

void require(bool valid, const std::string & message)
{
	if(!valid)
		throw std::runtime_error(message);
}

std::string digest(const std::string & text)
{
	return nhart::hex(nhart::sha256(std::span<const ui8>(reinterpret_cast<const ui8 *>(text.data()), text.size())));
}

void checkKnownVectors()
{
	require(digest("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Empty SHA256 vector");
	require(digest("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "ABC SHA256 vector");
	require(digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
		== "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "Multi-block SHA256 vector");
	const std::string million(1000000, 'a');
	require(digest(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "Million-a SHA256 vector");
	std::istringstream stream(million);
	require(nhart::hex(nhart::sha256(stream, million.size())) == digest(million), "Stream SHA256 vector");
}

Bytes read(CNhArtLoader & loader, const ResourcePath & resource)
{
	auto stream = loader.load(resource);
	auto [bytes, size] = stream->readAll();
	return Bytes(bytes.get(), bytes.get() + size);
}

void requirePlainRoot(const boost::filesystem::path & root)
{
	boost::filesystem::path current = root.root_path();
	for(const auto & component : root.relative_path())
	{
		current /= component;
		const auto status = boost::filesystem::symlink_status(current);
		require(boost::filesystem::is_directory(status) && !boost::filesystem::is_symlink(status), "Cache root must be plain existing directories");
	}
}

void compareAll(CNhArtLoader & prepared, CNhArtLoader & direct, ui64 & comparedBytes)
{
	const auto resources = prepared.getFilteredFiles([](const auto &) { return true; });
	require(resources.size() == EXPECTED_RESOURCES, "Unexpected committed resource count");
	require(resources == direct.getFilteredFiles([](const auto &) { return true; }), "Resource namespace mismatch");
	require(!prepared.existsResource(ResourcePath(".nhart/manifest.json")), "Metadata became a game resource");
	for(const auto & resource : resources)
	{
		const auto cached = read(prepared, resource);
		require(cached == read(direct, resource), "Prepared/direct payload differs");
		comparedBytes += cached.size();
	}
}

void requireSingleCache(const boost::filesystem::path & owned)
{
	const auto version = owned / "new-horizons-art" / "v1";
	size_t children = 0;
	for(const auto & entry : boost::filesystem::directory_iterator(version))
	{
		++children;
		require(entry.path().filename().string() == EXPECTED_PACK_SHA256, "Unexpected cache stage/lock/hash directory");
		require(boost::filesystem::is_directory(entry.symlink_status()) && !boost::filesystem::is_symlink(entry.symlink_status()), "Linked cache directory");
	}
	require(children == 1, "Mounts did not share one prepared cache");
	size_t files = 0;
	for(const auto & entry : boost::filesystem::directory_iterator(version / EXPECTED_PACK_SHA256))
	{
		++files;
		require(boost::filesystem::is_regular_file(entry.symlink_status()) && !boost::filesystem::is_symlink(entry.symlink_status()), "Non-regular cache payload");
	}
	require(files == EXPECTED_RESOURCES + 1, "Cache inventory differs (including hidden manifest)");
}

void measureWarmReads(CNhArtLoader & prepared, CNhArtLoader & direct)
{
	const auto resources = prepared.getFilteredFiles([](const auto &) { return true; });
	for(int loop = 1; loop <= 3; ++loop)
	{
		std::string expected;
		for(auto * loader : {&direct, &prepared})
		{
			const auto start = std::chrono::steady_clock::now();
			std::string hashes;
			for(const auto & resource : resources)
			{
				const auto bytes = read(*loader, resource);
				hashes += nhart::hex(nhart::sha256(std::span<const ui8>(bytes.data(), bytes.size())));
			}
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			const auto combined = digest(hashes);
			if(loader == &direct)
				expected = combined;
			else
				require(combined == expected, "Warm-loop payload hashes differ");
			std::cout << "Warm OS-cache loop " << loop << ' ' << (loader == &direct ? "archive" : "prepared-file")
				<< ": " << elapsed << " ms; ordered payload digest " << combined << '\n';
		}
	}
}
}

int main(int argc, char ** argv)
{
	if(argc != 5 || std::string(argv[1]) != "--pack" || std::string(argv[3]) != "--cache-root")
	{
		std::cerr << "Usage: nhArtStartupRuntimeTest --pack NewHorizons.nhart --cache-root EXISTING-DISPOSABLE-ROOT\n";
		return 2;
	}
	std::string phase = "arguments";
	std::string childName;
	try
	{
		const auto pack = boost::filesystem::canonical(boost::filesystem::path(argv[2]));
		const auto root = boost::filesystem::absolute(boost::filesystem::path(argv[4])).lexically_normal();
		phase = "private-root";
		requirePlainRoot(root);
		phase = "known-sha-vectors";
		checkKnownVectors();
		phase = "committed-pack-pin";
		std::ifstream input(pack.string(), std::ios::binary);
		require(static_cast<bool>(input), "Pack input unavailable");
		require(nhart::hex(nhart::sha256(input, boost::filesystem::file_size(pack))) == EXPECTED_PACK_SHA256, "Unexpected committed pack");
		childName = boost::filesystem::unique_path("nhart-runtime-%%%%-%%%%-%%%%-%%%%").string();
		const auto owned = root / childName;
		require(boost::filesystem::create_directory(owned), "Owned child already exists or is unwritable");
		std::cout << "Owned cache child: " << childName << '\n';
		ui64 comparedBytes = 0;
		{
			phase = "initial-two-scope-preparation";
			const auto preparationStart = std::chrono::steady_clock::now();
			CNhArtLoader builtin("", pack, owned);
			CNhArtLoader mod("MODS/NEW-HORIZONS/", pack, owned);
			std::cout << "Two-mount preparation: "
				<< std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - preparationStart).count()
				<< " ms (OS-cache state uncontrolled)\n";
			CNhArtLoader direct("", pack, owned, CNhArtLoader::Backing::DirectArchiveForTests);
			phase = "all-resource-parity";
			compareAll(builtin, direct, comparedBytes);
			const auto resources = builtin.getFilteredFiles([](const auto &) { return true; });
			for(const auto & resource : resources)
			{
				const ResourcePath mounted("MODS/NEW-HORIZONS/" + resource.getName(), resource.getType());
				require(mod.existsResource(mounted), "Mod-scope identity missing");
				require(read(mod, mounted) == read(builtin, resource), "Mod-scope bytes differ");
			}
			require(mod.getFilteredFiles([](const auto &) { return true; }).size() == EXPECTED_RESOURCES, "Mod-scope resource count");
			requireSingleCache(owned);
			phase = "warm-read-measurements";
			measureWarmReads(builtin, direct);
		}
		// All prepared receipts expired above: this constructor exercises the
		// persisted-cache integrity validation, not merely the in-process hit.
		phase = "reopened-cache-validation";
		{
			CNhArtLoader reopened("", pack, owned);
			CNhArtLoader direct("", pack, owned, CNhArtLoader::Backing::DirectArchiveForTests);
			compareAll(reopened, direct, comparedBytes);
			requireSingleCache(owned);
		}
		std::cout << "PASS: SHA256 vectors; " << EXPECTED_RESOURCES << " resources; both scopes; fresh/reused cache; "
			<< comparedBytes << " parity bytes\n";
		return 0;
	}
	catch(const std::exception &)
	{
		// boost filesystem exceptions can contain caller absolute paths. Keep
		// the public CI report scoped to a phase and our relative owned child.
		std::cerr << "FAIL during " << phase;
		if(!childName.empty())
			std::cerr << "; retained child " << childName;
		std::cerr << " (no caller paths deleted)\n";
		return 1;
	}
}
