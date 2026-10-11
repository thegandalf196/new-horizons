/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

// Opt-in test protocol only. Normal startup neither writes nor waits.
#ifdef VCMI_WINDOWS
#include <windows.h>
#include "../lib/json/JsonNode.h"
#include "../lib/VCMIDirs.h"
#include <fstream>
#include <array>
#include <stdexcept>
#include <string>

namespace privateWineStartup
{
inline std::wstring environment(const wchar_t * name)
{
	wchar_t buffer[2048];
	const DWORD length = GetEnvironmentVariableW(name, buffer, 2048);
	if(length == 0 || length >= 2048)
		throw std::runtime_error("Missing or oversized private gate environment");
	std::wstring value(buffer, length);
	if(value.find_first_of(L"\r\n") != std::wstring::npos)
		throw std::runtime_error("Invalid private gate environment");
	return value;
}

inline std::wstring modulePath(HMODULE module)
{
	wchar_t buffer[2048];
	const DWORD length = GetModuleFileNameW(module, buffer, 2048);
	if(length == 0 || length >= 2048)
		throw std::runtime_error("Missing private gate module");
	return std::wstring(buffer, length);
}

inline bool plainAncestors(const boost::filesystem::path & path)
{
	for(auto current = path; !current.empty(); current = current.parent_path())
	{
		const DWORD attributes = GetFileAttributesW(current.c_str());
		if(attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)
			|| (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
			return false;
		if(current == current.parent_path())
			break;
	}
	return true;
}

inline bool boundedPath(const std::wstring & path)
{
	// Existing VCMIDirsWIN32 buffers include the terminating NUL in MAX_PATH.
	return !path.empty() && path.size() < MAX_PATH;
}

inline bool plainFile(const boost::filesystem::path & path)
{
	if(!plainAncestors(path.parent_path()))
		return false;
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES
		&& !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

inline bool exactBoundedExpansion(const char * raw, const std::wstring & expected)
{
	if(!boundedPath(expected))
		return false;
	const std::string source(raw);
	const std::wstring wide(source.begin(), source.end()); // fixed ASCII routes
	wchar_t expanded[MAX_PATH];
	const DWORD count = ExpandEnvironmentStringsW(wide.c_str(), expanded, MAX_PATH);
	return count != 0 && count <= MAX_PATH && std::wstring(expanded, count - 1) == expected;
}

inline int verifyBeforeSDL()
{
	SetLastError(ERROR_SUCCESS);
	const DWORD enabled = GetEnvironmentVariableW(L"NH_PRIVATE_WINE_NONCE", nullptr, 0);
	if(enabled == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
		return 0;
	try
	{
		const auto nonce = environment(L"NH_PRIVATE_WINE_NONCE");
		if(nonce.size() != 64 || nonce.find_first_not_of(L"0123456789abcdef") != std::wstring::npos)
			return 86;
		const auto local = environment(L"LOCALAPPDATA");
		const auto expected = environment(L"NH_PRIVATE_WINE_LOCALAPPDATA");
		if(!boundedPath(local) || local != expected || local.size() < 20 || local.substr(0, 3) != L"Z:\\"
			|| local.find(L"..") != std::wstring::npos || local.back() == L'\\'
			|| local.substr(local.size() - 13) != L"\\localappdata")
			return 86;
		if(environment(L"SDL_AUDIODRIVER") != L"dummy" || environment(L"SDL_AUDIO_DRIVER") != L"dummy"
			|| environment(L"SDL_VIDEODRIVER") != L"windows" || environment(L"SDL_VIDEO_DRIVER") != L"windows")
			return 86;
		const auto executable = modulePath(nullptr);
		HMODULE facade = GetModuleHandleW(L"VCMI_lib.dll");
		if(!facade || !boundedPath(executable) || executable != environment(L"NH_PRIVATE_WINE_EXECUTABLE"))
			return 86;
		const auto library = modulePath(facade);
		if(!boundedPath(library) || library != environment(L"NH_PRIVATE_WINE_FACADE")
			|| !plainFile(boost::filesystem::path(executable)) || !plainFile(boost::filesystem::path(library)))
			return 86;
		const boost::filesystem::path gate = boost::filesystem::path(local) / L".nh-private-wine-gate";
		if(!boundedPath((gate / L"proof.utf16le").wstring())
			|| !boundedPath((gate / L"approved.utf16le").wstring()) || !plainAncestors(gate))
			return 86;

		// Verify shipped routes before VCMIDirs::get() can create anything.
		const auto config = boost::filesystem::path(executable).parent_path() / "config" / "dirs.json";
		if(!boundedPath(config.wstring()) || !plainFile(config))
			return 86;
		std::ifstream input(config.c_str(), std::ios::binary);
		if(!input)
			return 86;
		std::array<char, 4097> configBytes;
		input.read(configBytes.data(), configBytes.size());
		const auto length = input.gcount();
		if(length <= 0 || length > 4096 || input.bad())
			return 86;
		const std::string bytes(configBytes.data(), static_cast<size_t>(length));
		const JsonNode routes(reinterpret_cast<const std::byte *>(bytes.data()), bytes.size(), "private-wine-dirs");
		const std::array<std::pair<const char *, const char *>, 5> required = {{
			{"userDataPath", "%LOCALAPPDATA%\\HeroesIII-NewHorizons\\content"},
			{"userConfigPath", "%LOCALAPPDATA%\\HeroesIII-NewHorizons\\config"},
			{"userCachePath", "%LOCALAPPDATA%\\HeroesIII-NewHorizons\\cache"},
			{"userLogsPath", "%LOCALAPPDATA%\\HeroesIII-NewHorizons\\logs"},
			{"userSavePath", "%LOCALAPPDATA%\\new-horizons\\Saves"}
		}};
		if(!routes.isStruct() || routes.Struct().size() != required.size())
			return 86;
		for(const auto & [key, value] : required)
			if(!routes[key].isString() || routes[key].String() != value)
				return 86;
		const std::array<std::wstring, 5> suffixes = {
			L"\\HeroesIII-NewHorizons\\content", L"\\HeroesIII-NewHorizons\\config",
			L"\\HeroesIII-NewHorizons\\cache", L"\\HeroesIII-NewHorizons\\logs", L"\\new-horizons\\Saves"};
		// The owner precreates these paths; reject aliases before init() writes.
		for(size_t i = 0; i < suffixes.size(); ++i)
			if(!exactBoundedExpansion(required[i].second, local + suffixes[i])
				|| !plainAncestors(boost::filesystem::path(local + suffixes[i])))
				return 86;
		// Later extraction uses this derived cache path, not a dirs.json route.
		if(!boundedPath(local + suffixes[2] + L"\\extracted"))
			return 86;
		const auto & dirs = VCMIDirs::get();
		const std::array<boost::filesystem::path, 5> paths = {
			dirs.userDataPath(), dirs.userConfigPath(), dirs.userCachePath(), dirs.userLogsPath(), dirs.userSavePath()};
		std::wstring proof = L"NH_WINE_PRE_SDL_V1\n" + nonce + L"\n" + std::to_wstring(GetCurrentProcessId())
			+ L"\n" + executable + L"\n" + library + L"\n" + local + L"\ndummy\ndummy\nwindows\nwindows\n";
		for(size_t i = 0; i < paths.size(); ++i)
		{
			if(paths[i].wstring() != local + suffixes[i] || !plainAncestors(paths[i]))
				return 86;
			proof += paths[i].wstring() + L"\n";
		}
		const auto destination = gate / L"proof.utf16le";
		if(proof.size() * sizeof(wchar_t) > 8192)
			return 86;
		HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if(file == INVALID_HANDLE_VALUE)
			return 86;
		DWORD written = 0;
		const DWORD count = static_cast<DWORD>(proof.size() * sizeof(wchar_t));
		const bool success = WriteFile(file, proof.data(), count, &written, nullptr) && written == count && FlushFileBuffers(file);
		CloseHandle(file);
		if(!success)
			return 86;
		// Owner authorizes only after exact post-loader PE/DLL and ancestry checks.
		const auto approval = gate / L"approved.utf16le";
		const ULONGLONG deadline = GetTickCount64() + 5000;
		while(GetTickCount64() < deadline)
		{
			HANDLE ack = CreateFileW(approval.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
			if(ack != INVALID_HANDLE_VALUE)
			{
				wchar_t buffer[65] = {};
				DWORD read = 0;
				BY_HANDLE_FILE_INFORMATION info;
				const bool accepted = GetFileInformationByHandle(ack, &info)
					&& !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
					&& info.nFileSizeHigh == 0 && info.nFileSizeLow == 128
					&& ReadFile(ack, buffer, sizeof(buffer), &read, nullptr) && read == 128
					&& std::wstring(buffer, 64) == nonce;
				CloseHandle(ack);
				return accepted ? 0 : 86;
			}
			Sleep(10);
		}
	}
	catch(...)
	{
		// No paths or normal-startup log messages from this private protocol.
	}
	return 86;
}
}
#endif
