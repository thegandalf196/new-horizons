/* New Horizons client startup. GPL-2.0-or-later. */
#include "StdInc.h"
#include "ClientStartup.h"

#include "../client/CMT.h"
#include "../lib/VCMIDirs.h"
#include "../lib/filesystem/Filesystem.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif

namespace clientStartup
{
#ifdef _WIN32
namespace
{
void signalWindowsLauncher()
{
	const char * token = std::getenv("NH_LAUNCHER_READY_EVENT");
	if(!token)
		return;

	constexpr std::string_view prefix = "Local\\NewHorizonsLauncherReady-";
	const std::string_view name(token);
	if(!name.starts_with(prefix) || name.size() != prefix.size() + 32
		|| name.find_first_not_of("0123456789abcdef", prefix.size()) != std::string_view::npos)
		return;

	if(const HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, token))
	{
		SetEvent(event);
		CloseHandle(event);
	}
}
}
#endif

void verifyGameResources()
{
	const auto verify = [](const std::string & resource, const std::string & message)
	{
		if(!CResourceHandler::get()->existsResource(ResourcePath(resource)))
			handleFatalError(message, false);
	};

	verify("DATA/HELP.TXT", "VCMI requires Heroes III: Shadow of Death or Heroes III: Complete data files to run!");
	verify("MODS/VCMI/MOD.JSON", "VCMI installation is corrupted!\nBuilt-in mod was not found!");
	verify("DATA/NOTOSERIF-MEDIUM.TTF", "VCMI installation is corrupted!\nBuilt-in font was not found!\nManually deleting '" + VCMIDirs::get().userDataPath().string() + "/Mods/VCMI' directory (if it exists)\nor clearing app data and reimporting Heroes III files may fix this problem.");
	verify("DATA/PLAYERS.PAL", "Heroes III data files (Data/H3Bitmap.lod) are incomplete or corruped!\n Please reinstall them.");
	verify("SPRITES/DEFAULT.DEF", "Heroes III data files (Data/H3Sprite.lod) are incomplete or corruped!\n Please reinstall them.");
}

void notifyLauncherReady()
{
#ifdef _WIN32
	signalWindowsLauncher();
#endif
	const char * enabled = std::getenv("NH_LAUNCHER_STATUS");
	if(enabled && std::string_view(enabled) == "1")
		std::cout << "NH_LAUNCHER_READY_V1\n" << std::flush;
}
}
