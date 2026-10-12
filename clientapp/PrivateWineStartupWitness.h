/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

// Opt-in test protocol only. Normal startup neither writes nor waits.
#ifdef VCMI_WINDOWS
#include <windows.h>
#include "../lib/json/JsonNode.h"
#include "../lib/VCMIDirs.h"
#include <fstream>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include "PrivateWinePathPolicy.h"

namespace privateWineStartup
{
// Fixed private-protocol codes, not API diagnoses. In particular, an exception
// identifies only the phase being checked. Never print paths, values or nonce.
enum class RefusalStage : unsigned int
{
	nonce = 1,
	local = 2,
	audioVideo = 3,
	executable = 4,
	facade = 5,
	gate = 6,
	configRead = 7,
	configShape = 8,
	expansion = 9,
	resolvedDirs = 10,
	proofCreate = 11,
	proofWrite = 12,
	ack = 13
};

inline int refusal(RefusalStage stage)
{
	std::fprintf(stderr, "NH_WINE_PRE_SDL_REFUSAL stage=%u\n", static_cast<unsigned int>(stage));
	std::fflush(stderr);
	return 86;
}

enum class LocalRefusalDetail : unsigned int
{
	readActual = 1,
	readExpected = 2,
	actualMissing = 3,
	actualOversized = 4,
	actualNewline = 5,
	expectedMissing = 6,
	expectedOversized = 7,
	expectedNewline = 8,
	unequal = 9,
	unbounded = 10,
	tooShort = 11,
	prefix = 12,
	dots = 13,
	trailingSeparator = 14,
	suffix = 15
};

inline int localRefusal(LocalRefusalDetail detail)
{
	std::fprintf(stderr, "NH_WINE_PRE_SDL_LOCAL_DETAIL detail=%u\n", static_cast<unsigned int>(detail));
	std::fflush(stderr);
	return refusal(RefusalStage::local);
}

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

// Same bounded read as environment(), with fixed codes only for stage two.
// A failure during allocation/read retains the actual/expected phase code.
inline std::wstring localEnvironment(const wchar_t * name, bool expected, LocalRefusalDetail & detail)
{
	detail = expected ? LocalRefusalDetail::readExpected : LocalRefusalDetail::readActual;
	wchar_t buffer[2048];
	const DWORD length = GetEnvironmentVariableW(name, buffer, 2048);
	if(length == 0)
	{
		detail = expected ? LocalRefusalDetail::expectedMissing : LocalRefusalDetail::actualMissing;
		throw std::runtime_error("Missing or oversized private gate environment");
	}
	if(length >= 2048)
	{
		detail = expected ? LocalRefusalDetail::expectedOversized : LocalRefusalDetail::actualOversized;
		throw std::runtime_error("Missing or oversized private gate environment");
	}
	std::wstring value(buffer, length);
	if(value.find_first_of(L"\r\n") != std::wstring::npos)
	{
		detail = expected ? LocalRefusalDetail::expectedNewline : LocalRefusalDetail::actualNewline;
		throw std::runtime_error("Invalid private gate environment");
	}
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
		if(!privateWinePathPolicy::isDirectoryAllowed(current.wstring(), attributes))
			return false;
		if(privateWinePathPolicy::isCanonicalRoot(current.wstring()) || current == current.parent_path())
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
	auto stage = RefusalStage::nonce;
	auto localDetail = LocalRefusalDetail::readActual;
	try
	{
		const auto nonce = environment(L"NH_PRIVATE_WINE_NONCE");
		if(nonce.size() != 64 || nonce.find_first_not_of(L"0123456789abcdef") != std::wstring::npos)
			return refusal(stage);
		stage = RefusalStage::local;
		const auto local = localEnvironment(L"LOCALAPPDATA", false, localDetail);
		const auto expected = localEnvironment(L"NH_PRIVATE_WINE_LOCALAPPDATA", true, localDetail);
		if(!boundedPath(local))
			return localRefusal(LocalRefusalDetail::unbounded);
		if(local != expected)
			return localRefusal(LocalRefusalDetail::unequal);
		if(local.size() < 20)
			return localRefusal(LocalRefusalDetail::tooShort);
		if(local.substr(0, 3) != L"Z:\\")
			return localRefusal(LocalRefusalDetail::prefix);
		if(local.find(L"..") != std::wstring::npos)
			return localRefusal(LocalRefusalDetail::dots);
		if(local.back() == L'\\')
			return localRefusal(LocalRefusalDetail::trailingSeparator);
		if(local.substr(local.size() - 13) != L"\\localappdata")
			return localRefusal(LocalRefusalDetail::suffix);
		stage = RefusalStage::audioVideo;
		if(environment(L"SDL_AUDIODRIVER") != L"dummy" || environment(L"SDL_AUDIO_DRIVER") != L"dummy"
			|| environment(L"SDL_VIDEODRIVER") != L"windows" || environment(L"SDL_VIDEO_DRIVER") != L"windows")
			return refusal(stage);
		stage = RefusalStage::executable;
		const auto executable = modulePath(nullptr);
		HMODULE facade = GetModuleHandleW(L"VCMI_lib.dll");
		if(!facade || !boundedPath(executable) || executable != environment(L"NH_PRIVATE_WINE_EXECUTABLE"))
			return refusal(stage);
		stage = RefusalStage::facade;
		const auto library = modulePath(facade);
		if(!boundedPath(library) || library != environment(L"NH_PRIVATE_WINE_FACADE")
			|| !plainFile(boost::filesystem::path(executable)) || !plainFile(boost::filesystem::path(library)))
			return refusal(stage);
		stage = RefusalStage::gate;
		const boost::filesystem::path gate = boost::filesystem::path(local) / L".nh-private-wine-gate";
		if(!boundedPath((gate / L"proof.utf16le").wstring())
			|| !boundedPath((gate / L"approved.utf16le").wstring()) || !plainAncestors(gate))
			return refusal(stage);

		// Verify shipped routes before VCMIDirs::get() can create anything.
		stage = RefusalStage::configRead;
		const auto config = boost::filesystem::path(executable).parent_path() / "config" / "dirs.json";
		if(!boundedPath(config.wstring()) || !plainFile(config))
			return refusal(stage);
		std::ifstream input(config.c_str(), std::ios::binary);
		if(!input)
			return refusal(stage);
		std::array<char, 4097> configBytes;
		input.read(configBytes.data(), configBytes.size());
		const auto length = input.gcount();
		if(length <= 0 || length > 4096 || input.bad())
			return refusal(stage);
		stage = RefusalStage::configShape;
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
			return refusal(stage);
		for(const auto & [key, value] : required)
			if(!routes[key].isString() || routes[key].String() != value)
				return refusal(stage);
		const std::array<std::wstring, 5> suffixes = {
			L"\\HeroesIII-NewHorizons\\content", L"\\HeroesIII-NewHorizons\\config",
			L"\\HeroesIII-NewHorizons\\cache", L"\\HeroesIII-NewHorizons\\logs", L"\\new-horizons\\Saves"};
		// The owner precreates these paths; reject aliases before init() writes.
		stage = RefusalStage::expansion;
		for(size_t i = 0; i < suffixes.size(); ++i)
			if(!exactBoundedExpansion(required[i].second, local + suffixes[i])
				|| !plainAncestors(boost::filesystem::path(local + suffixes[i])))
				return refusal(stage);
		// Later extraction uses this derived cache path, not a dirs.json route.
		if(!boundedPath(local + suffixes[2] + L"\\extracted"))
			return refusal(stage);
		stage = RefusalStage::resolvedDirs;
		const auto & dirs = VCMIDirs::get();
		const std::array<boost::filesystem::path, 5> paths = {
			dirs.userDataPath(), dirs.userConfigPath(), dirs.userCachePath(), dirs.userLogsPath(), dirs.userSavePath()};
		std::wstring proof = L"NH_WINE_PRE_SDL_V1\n" + nonce + L"\n" + std::to_wstring(GetCurrentProcessId())
			+ L"\n" + executable + L"\n" + library + L"\n" + local + L"\ndummy\ndummy\nwindows\nwindows\n";
		for(size_t i = 0; i < paths.size(); ++i)
		{
			if(paths[i].wstring() != local + suffixes[i] || !plainAncestors(paths[i]))
				return refusal(stage);
			proof += paths[i].wstring() + L"\n";
		}
		const auto destination = gate / L"proof.utf16le";
		stage = RefusalStage::proofCreate;
		if(proof.size() * sizeof(wchar_t) > 8192)
			return refusal(stage);
		HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
		if(file == INVALID_HANDLE_VALUE)
			return refusal(stage);
		stage = RefusalStage::proofWrite;
		DWORD written = 0;
		const DWORD count = static_cast<DWORD>(proof.size() * sizeof(wchar_t));
		const bool success = WriteFile(file, proof.data(), count, &written, nullptr) && written == count && FlushFileBuffers(file);
		CloseHandle(file);
		if(!success)
			return refusal(stage);
		// Owner authorizes only after exact post-loader PE/DLL and ancestry checks.
		stage = RefusalStage::ack;
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
				return accepted ? 0 : refusal(stage);
			}
			Sleep(10);
		}
	}
	catch(...)
	{
		// Stage only: exception text can contain private paths or input values.
	}
	if(stage == RefusalStage::local)
		return localRefusal(localDetail);
	return refusal(stage);
}
}
#endif
