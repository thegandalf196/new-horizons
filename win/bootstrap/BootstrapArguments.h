/* Part of New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace newHorizonsBootstrap
{
inline std::wstring quoteWindowsArgument(std::wstring_view value)
{
	std::wstring result = L"\"";
	size_t slashes = 0;
	for(const wchar_t character : value)
	{
		if(character == L'\\')
		{
			++slashes;
			continue;
		}
		result.append(slashes * (character == L'"' ? 2 : 1), L'\\');
		slashes = 0;
		if(character == L'"')
			result += L'\\';
		result += character;
	}
	result.append(slashes * 2, L'\\');
	return result + L'"';
}

inline std::wstring jsonString(std::wstring_view value)
{
	constexpr wchar_t digits[] = L"0123456789abcdef";
	std::wstring result = L"\"";
	for(const wchar_t character : value)
	{
		if(character == L'"' || character == L'\\')
			result += L'\\';
		if(static_cast<unsigned>(character) < 32)
		{
			result += L"\\u00";
			result += digits[(character >> 4) & 15];
			result += digits[character & 15];
		}
		else
			result += character;
	}
	return result + L'"';
}

inline std::wstring argumentEnvelope(const std::vector<std::wstring> & arguments)
{
	std::wstring result = L"{\"version\":1,\"arguments\":[";
	for(size_t index = 0; index < arguments.size(); ++index)
	{
		if(index)
			result += L',';
		result += jsonString(arguments[index]);
	}
	return result + L"]}";
}
}
