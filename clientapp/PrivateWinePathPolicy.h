/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <cstdint>
#include <string_view>

namespace privateWinePathPolicy
{
// Win32 attribute values; this pure policy also builds without Windows headers.
constexpr std::uint32_t DIRECTORY = 0x10;
constexpr std::uint32_t REPARSE_POINT = 0x400;
constexpr std::uint32_t INVALID_ATTRIBUTES = 0xffffffff;

constexpr bool isCanonicalRoot(std::wstring_view path)
{
	return path == L"Z:\\";
}

// Private protocol only: the Linux owner must separately pin the fresh prefix's
// exact z: -> / mapping. No non-root junction/symlink exception is granted here.
constexpr bool isDirectoryAllowed(std::wstring_view path, std::uint32_t attributes)
{
	return attributes != INVALID_ATTRIBUTES && (attributes & DIRECTORY)
		&& (!(attributes & REPARSE_POINT) || isCanonicalRoot(path));
}
}
