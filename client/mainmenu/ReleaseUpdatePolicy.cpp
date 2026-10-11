/* New Horizons release update policy. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "ReleaseUpdatePolicy.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/modding/CModVersion.h"

#include <charconv>
#include <array>
#include <cmath>

namespace releaseUpdates
{
namespace
{
constexpr std::string_view RELEASE_ROOT = "https://github.com/thegandalf196/new-horizons/releases/";
constexpr size_t MAX_RESPONSE = 1024 * 1024;
constexpr size_t MAX_ASSETS = 128;

std::optional<CModVersion> parseVersion(std::string_view text, bool tag)
{
	if(text.empty() || text.size() > 32)
		return {};
	if(tag && text.front() == 'v')
		text.remove_prefix(1);
	std::array<int, 3> values{0, 0, 0};
	size_t count = 0;
	while(!text.empty())
	{
		if(count == values.size())
			return {};
		const auto dot = text.find('.');
		const auto part = text.substr(0, dot);
		if(part.empty() || (part.size() > 1 && part.front() == '0')
			|| part.find_first_not_of("0123456789") != std::string_view::npos)
			return {};
		const auto parsed = std::from_chars(part.data(), part.data() + part.size(), values[count]);
		if(parsed.ec != std::errc() || parsed.ptr != part.data() + part.size())
			return {};
		++count;
		if(dot == std::string_view::npos)
			break;
		text.remove_prefix(dot + 1);
		if(text.empty())
			return {};
	}
	if(count < 2)
		return {};
	// Unlike CModVersion::fromString's wildcard ranks, omitted patch is zero.
	return CModVersion(values[0], values[1], values[2]);
}

bool publishedTimestamp(const JsonNode & value)
{
	if(!value.isString())
		return false;
	const auto & text = value.String();
	if(text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T'
		|| text[13] != ':' || text[16] != ':' || text[19] != 'Z')
		return false;
	for(size_t index = 0; index < 19; ++index)
	{
		if(index == 4 || index == 7 || index == 10 || index == 13 || index == 16)
			continue;
		if(text[index] < '0' || text[index] > '9')
			return false;
	}
	const auto number = [&text](size_t offset, size_t length)
	{
		int result = 0;
		for(size_t index = offset; index < offset + length; ++index)
			result = result * 10 + text[index] - '0';
		return result;
	};
	const int year = number(0, 4);
	const int month = number(5, 2);
	const int day = number(8, 2);
	if(year == 0 || month < 1 || month > 12 || day < 1
		|| number(11, 2) > 23 || number(14, 2) > 59 || number(17, 2) > 59)
		return false;
	constexpr std::array<int, 12> DAYS{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	return day <= DAYS[month - 1] + (month == 2 && leap ? 1 : 0);
}

bool playableName(const std::string & name, Platform platform, const CModVersion & version)
{
	const std::string target = platform == Platform::WindowsX64 ? "Windows-x64" : "Linux-x64";
	const std::string extension = platform == Platform::WindowsX64 ? ".zip" : ".tar.gz";
	const auto full = version.toString();
	const auto shortVersion = std::to_string(version.major) + "." + std::to_string(version.minor);
	if(name == "New-Horizons-" + full + "-" + target + extension
		|| (version.patch == 0 && name == "New-Horizons-" + shortVersion + "-" + target + extension)
		|| name == "New-Horizons-" + target + extension)
		return true;
	const std::string prefix = "New-Horizons-" + target + "-";
	if(!name.starts_with(prefix) || !name.ends_with(extension))
		return false;
	const auto revision = std::string_view(name).substr(prefix.size(), name.size() - prefix.size() - extension.size());
	return (revision.size() == 12 || revision.size() == 40)
		&& revision.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

bool positiveSize(const JsonNode & value)
{
	if(value.getType() == JsonNode::JsonType::DATA_INTEGER)
		return value.Integer() > 0;
	if(value.getType() == JsonNode::JsonType::DATA_FLOAT)
		return std::isfinite(value.Float()) && value.Float() > 0 && std::floor(value.Float()) == value.Float();
	return false;
}
}

std::optional<Offer> selectOffer(std::string_view installedVersion, Platform platform,
	int httpStatus, std::string_view response)
{
	if(httpStatus != 200 || response.empty() || response.size() > MAX_RESPONSE
		|| (platform != Platform::LinuxX64 && platform != Platform::WindowsX64))
		return {};
	const auto installed = parseVersion(installedVersion, false);
	if(!installed)
		return {};
	try
	{
		JsonParsingSettings parsing;
		parsing.mode = JsonParsingSettings::JsonFormatMode::JSON;
		parsing.strict = true;
		parsing.maxDepth = 16;
		const JsonNode release(response.data(), response.size(), parsing, "release update response");
		if(!release.isStruct() || !release["draft"].isBool() || release["draft"].Bool()
			|| !release["prerelease"].isBool() || release["prerelease"].Bool()
			|| !publishedTimestamp(release["published_at"]) || !release["tag_name"].isString())
			return {};
		const auto & tag = release["tag_name"].String();
		const auto version = parseVersion(tag, true);
		if(!version || !(*installed < *version))
			return {};
		const std::string page = std::string(RELEASE_ROOT) + "tag/" + tag;
		if(!release["html_url"].isString() || release["html_url"].String() != page
			|| !release["assets"].isVector() || release["assets"].Vector().size() > MAX_ASSETS)
			return {};
		const std::string downloadRoot = std::string(RELEASE_ROOT) + "download/" + tag + "/";
		for(const auto & asset : release["assets"].Vector())
		{
			if(!asset.isStruct() || !asset["name"].isString() || !asset["state"].isString()
				|| asset["state"].String() != "uploaded" || !positiveSize(asset["size"])
				|| !asset["browser_download_url"].isString())
				continue;
			const auto & name = asset["name"].String();
			if(playableName(name, platform, *version)
				&& asset["browser_download_url"].String() == downloadRoot + name)
				return Offer{version->toString(), page};
		}
	}
	catch(const std::exception &)
	{
		// Expected malformed network data is silent, including strict JSON errors.
	}
	return {};
}
}
