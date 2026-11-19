/* Native NHART cache paths, GPL-2.0-or-later. */
#pragma once

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace nhart::detail
{
inline size_t cacheUncRootLength(std::wstring_view spelling)
{
	if(!spelling.starts_with(L"\\\\"))
		return 0;
	const auto serverEnd = spelling.find(L'\\', 2);
	const auto shareEnd = serverEnd == std::wstring_view::npos ? serverEnd : spelling.find(L'\\', serverEnd + 1);
	const auto end = shareEnd == std::wstring_view::npos ? spelling.size() : shareEnd;
	if(serverEnd == std::wstring_view::npos || serverEnd == 2 || end <= serverEnd + 1
		|| spelling.substr(2, serverEnd - 2) == L"." || spelling.substr(2, serverEnd - 2) == L".."
		|| spelling.substr(serverEnd + 1, end - serverEnd - 1) == L"."
		|| spelling.substr(serverEnd + 1, end - serverEnd - 1) == L"..")
		throw std::runtime_error("Unsupported NHART cache UNC root");
	return end;
}

/// Keep ordinary components for the ancestor walk and apply extended Windows
/// syntax only at filesystem operation boundaries. UNC normalization operates
/// on the tail so standard libraries cannot rewrite the server into a drive.
inline std::filesystem::path absoluteCachePath(const std::filesystem::path & location)
{
#ifdef _WIN32
	auto spelling = location.native();
	std::replace(spelling.begin(), spelling.end(), L'/', L'\\');
	if(spelling.starts_with(L"\\\\?\\"))
	{
		const bool unc = spelling.size() >= 8
			&& (spelling[4] == L'U' || spelling[4] == L'u')
			&& (spelling[5] == L'N' || spelling[5] == L'n')
			&& (spelling[6] == L'C' || spelling[6] == L'c') && spelling[7] == L'\\';
		const bool drive = spelling.size() >= 7
			&& ((spelling[4] >= L'A' && spelling[4] <= L'Z') || (spelling[4] >= L'a' && spelling[4] <= L'z'))
			&& spelling[5] == L':' && spelling[6] == L'\\';
		if(unc)
			spelling = L"\\\\" + spelling.substr(8);
		else if(drive)
			spelling.erase(0, 4);
		else
			throw std::runtime_error("Unsupported NHART cache path namespace");
	}
	if(spelling.starts_with(L"\\\\.\\") || spelling.find(L'\0') != std::wstring::npos)
		throw std::runtime_error("Unsupported NHART cache path namespace");
	if(const auto rootLength = cacheUncRootLength(spelling))
	{
		const auto tailStart = spelling.find_first_not_of(L'\\', rootLength);
		if(tailStart == std::wstring::npos)
			return std::filesystem::path(spelling.substr(0, rootLength));
		const auto tail = std::filesystem::path(spelling.substr(tailStart)).lexically_normal();
		if(tail.has_root_path() || std::any_of(tail.begin(), tail.end(), [](const auto & part) { return part == L".."; }))
			throw std::runtime_error("NHART cache path leaves its UNC share");
		if(tail == L".")
			return std::filesystem::path(spelling.substr(0, rootLength));
		return std::filesystem::path(spelling.substr(0, rootLength) + L"\\" + tail.native());
	}
	const std::filesystem::path ordinary(spelling);
	return (ordinary.is_absolute() ? ordinary : std::filesystem::absolute(ordinary)).lexically_normal();
#else
	return std::filesystem::absolute(location).lexically_normal();
#endif
}

struct CachePathComponents
{
	std::filesystem::path root;
	std::filesystem::path relative;
};

/// Windows UNC roots include the server and share even on standard libraries
/// that recognize only drive roots. The share is opened, never created.
inline CachePathComponents cachePathComponents(const std::filesystem::path & path)
{
#ifdef _WIN32
	const auto & spelling = path.native();
	if(const auto rootLength = cacheUncRootLength(spelling))
	{
		const auto tailStart = spelling.find_first_not_of(L'\\', rootLength);
		return {std::filesystem::path(spelling.substr(0, rootLength)), tailStart == std::wstring::npos
			? std::filesystem::path() : std::filesystem::path(spelling.substr(tailStart))};
	}
#endif
	return {path.root_path(), path.relative_path()};
}

/// Explicit extended syntax removes the Win32 MAX_PATH dependency without
/// requiring a process manifest or a system-wide long-path setting.
inline std::filesystem::path nativeCachePath(const std::filesystem::path & location)
{
#ifdef _WIN32
	const auto path = absoluteCachePath(location).native();
	if(path.starts_with(L"\\\\"))
		return std::filesystem::path(L"\\\\?\\UNC\\" + path.substr(2));
	return std::filesystem::path(L"\\\\?\\" + path);
#else
	return location;
#endif
}
}
