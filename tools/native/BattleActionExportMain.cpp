/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../../lib/StdInc.h"
#include "BattleActionExport.h"
#include "SaveParityInput.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/filesystem/NhArtSha256.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/modding/CModHandler.h"
#include <boost/filesystem/operations.hpp>
#include <charconv>
#include <fstream>
#include <iostream>

namespace
{
std::string inputDigest(const boost::filesystem::path & path)
{
	if(boost::filesystem::is_symlink(path) || !boost::filesystem::is_regular_file(path)
		|| boost::filesystem::file_size(path) > 128 * 1024 * 1024)
		throw std::runtime_error("Inspection requires a regular save of at most 128 MiB");
	std::ifstream input(path.string(), std::ios::binary);
	if(!input)
		throw std::runtime_error("Cannot read the input save");
	return nhart::hex(nhart::sha256(input, boost::filesystem::file_size(path)));
}
void requireShippingMods()
{
	const auto & mods = LIBRARY->modh->getActiveMods();
	if(std::find(mods.begin(), mods.end(), "vcmi") == mods.end()
		|| std::find(mods.begin(), mods.end(), "new-horizons") == mods.end()
		|| std::any_of(mods.begin(), mods.end(), [](const auto & mod)
		{ return mod != "core" && mod != "vcmi" && mod != "new-horizons"; }))
		throw std::runtime_error("Save inspection requires the ordinary shipping mod preset");
}
std::optional<size_t> selection(int argc, char ** argv)
{
	if(argc == 4 && std::string_view(argv[3]) == "--list")
		return std::nullopt;
	if(argc != 5 || std::string_view(argv[3]) != "--battle")
		throw std::runtime_error("Specify --list or --battle INDEX");
	size_t index = 0;
	const std::string_view text(argv[4]);
	const auto parsed = std::from_chars(text.data(), text.data() + text.size(), index);
	if(parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
		throw std::runtime_error("Battle index must be a nonnegative decimal integer");
	return index;
}
}
int main(int argc, char ** argv)
{
	if(argc < 4)
	{
		std::cerr << "Usage: nhBattleActions SAVE.vsgm1 SOURCE_LABEL --list | --battle INDEX\n";
		return 2;
	}
	try
	{
		const auto selected = selection(argc, argv);
		const std::string label(argv[2]);
		if(label.empty() || label.size() > 128)
			throw std::runtime_error("Source label must contain 1 to 128 bytes");
		const auto digest = inputDigest(argv[1]);
		NativeBonusGraphAccess graphAccess;
		auto library = std::make_unique<GameLibrary>();
		LIBRARY = library.get();
		library->initializeFilesystem(false);
		requireShippingMods();
		library->initializeLibrary();
		auto loaded = saveParity::readSave(argv[1]);
		auto historical = std::make_unique<CGameState>();
		historical->preInit(LIBRARY);
		auto result = battleActionExport::inspect(loaded->world->replayLog, historical.get(), selected);
		result["inputSHA256"].String() = digest;
		result["sourceLabel"].String() = label;
		result["sourceLabelAuthority"].String() = "caller supplied; match to separately sealed candidate evidence";
		result["exporterBuild"].String() = NH_ACTION_EXPORT_BUILD;
		result["savedFormat"].Integer() = static_cast<int32_t>(loaded->version);
		if(inputDigest(argv[1]) != digest)
			throw std::runtime_error("Input save changed during inspection");
		const auto json = result.toString();
		if(json.size() > battleActionExport::Limits{}.outputBytes)
			throw std::runtime_error("Action export exceeds its final JSON budget");
		std::cout << json << '\n';
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << "Action export refused: " << error.what() << '\n';
		return 2;
	}
}
