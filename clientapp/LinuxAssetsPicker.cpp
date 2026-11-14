/*
 * LinuxAssetsPicker.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <unistd.h>

namespace
{
int launch(int argc, char ** argv)
{
	std::error_code error;
	const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
	const auto script = executable.parent_path() / "Play-New-Horizons.sh";
	if(error || !std::filesystem::is_regular_file(script, error))
	{
		std::fputs("New Horizons: the packaged Play-New-Horizons.sh entry is missing.\n", stderr);
		return 1;
	}
	const auto path = script.string();
	std::vector<char *> arguments{const_cast<char *>("bash"), const_cast<char *>(path.c_str())};
	for(int index = 1; index < argc; ++index)
		arguments.push_back(argv[index]);
	arguments.push_back(nullptr);
	execv("/bin/bash", arguments.data());
	std::perror("New Horizons launcher");
	return 1;
}

struct Selection
{
	std::mutex mutex;
	bool complete = false;
	bool failed = false;
	std::string path;
};

void selected(void * userdata, const char * const * paths, int)
{
	auto & selection = *static_cast<Selection *>(userdata);
	const std::lock_guard lock(selection.mutex);
	try
	{
		selection.failed = paths == nullptr;
		if(paths != nullptr && paths[0] != nullptr)
			selection.path = paths[0];
	}
	catch(...)
	{
		selection.failed = true;
	}
	selection.complete = true;
}

int chooseFolder(const char * initial)
{
	Selection selection;
	SDL_ShowOpenFolderDialog(selected, &selection, nullptr, initial, false);
	for(;;)
	{
		SDL_PumpEvents();
		{
			const std::lock_guard lock(selection.mutex);
			if(selection.complete)
				break;
		}
		SDL_Delay(10);
	}
	if(selection.failed)
	{
		std::fputs("The desktop folder chooser could not open. Use --assets with your Heroes III Complete folder.\n", stderr);
		return 1;
	}
	if(selection.path.empty())
		return 2;
	// A single NUL-terminated UTF-8 path avoids shell interpretation and also
	// represents spaces, newlines and non-ASCII installation names faithfully.
	const bool written = std::fwrite(selection.path.c_str(), 1, selection.path.size() + 1, stdout)
		== selection.path.size() + 1 && std::fflush(stdout) == 0;
	return written ? 0 : 1;
}
}

int main(int argc, char ** argv)
{
	const std::string operation = argc > 1 ? argv[1] : "";
	if(operation != "--choose-folder" && operation != "--error" && operation != "--info")
		return launch(argc, argv);
	if(argc > 3)
		return 64;
	if(!SDL_Init(SDL_INIT_VIDEO))
	{
		std::fputs("Cannot initialize the desktop folder chooser. Use --assets from a terminal.\n", stderr);
		return 1;
	}
	int result;
	if(operation == "--choose-folder")
		result = chooseFolder(argc == 3 && *argv[2] ? argv[2] : nullptr);
	else if(argc == 3)
		result = SDL_ShowSimpleMessageBox(operation == "--error" ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION,
			"New Horizons — original game files", argv[2], nullptr) ? 0 : 1;
	else
		result = 64;
	SDL_Quit();
	return result;
}
