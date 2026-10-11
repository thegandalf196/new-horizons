/* CPU-only release policy regression. GPL-2.0-or-later; see license.txt. */
#include "../../lib/StdInc.h"
#include "../mainmenu/ReleaseUpdatePolicy.h"
#include "../../lib/json/JsonNode.h"

#include <iostream>

namespace
{
using releaseUpdates::Platform;
constexpr auto WINDOWS = Platform::WindowsX64;
constexpr auto LINUX = Platform::LinuxX64;
const std::string ROOT = "https://github.com/thegandalf196/new-horizons/releases/";

void require(bool condition)
{
	if(!condition)
		throw std::runtime_error("Release policy assertion failed");
}

JsonNode fixture(std::string tag = "v0.86.0", std::string name = "New-Horizons-0.86-Windows-x64.zip")
{
	JsonNode result;
	result["tag_name"] = JsonNode(tag);
	result["draft"] = JsonNode(false);
	result["prerelease"] = JsonNode(false);
	result["published_at"] = JsonNode("2026-10-10T12:34:56Z");
	result["html_url"] = JsonNode(ROOT + "tag/" + tag);
	JsonNode asset;
	asset["name"] = JsonNode(name);
	asset["state"] = JsonNode("uploaded");
	asset["size"] = JsonNode(int64_t(123456));
	asset["browser_download_url"] = JsonNode(ROOT + "download/" + tag + "/" + name);
	result["assets"].Vector().push_back(asset);
	return result;
}

bool offered(const JsonNode & release, Platform platform = WINDOWS, std::string_view installed = "0.85.0", int status = 200)
{
	return releaseUpdates::selectOffer(installed, platform, status, release.toCompactString()).has_value();
}

void run(const char * name, const std::function<void()> & test, int & count)
{
	test();
	++count;
	std::cout << "PASS " << name << '\n';
}
}

int main()
{
	int count = 0;
	try
	{
		run("NewerProductReleaseHasConstructedOfficialPage", []
		{
			const auto release = fixture();
			const auto offer = releaseUpdates::selectOffer("0.85.0", WINDOWS, 200, release.toCompactString());
			require(offer && offer->version == "0.86.0" && offer->releasePage == ROOT + "tag/v0.86.0");
		}, count);
		run("EqualOlderAndWithdrawnResponsesAreSilent", []
		{
			require(!offered(fixture(), WINDOWS, "0.86.0"));
			require(!offered(fixture(), WINDOWS, "1.0.0"));
			for(int status : {0, 404, 403, 429, 500, 304})
				require(!offered(fixture(), WINDOWS, "0.85.0", status));
		}, count);
		run("ShortProductVersionNormalizesPatchZero", []
		{
			require(offered(fixture(), WINDOWS, "0.85"));
			require(!offered(fixture("v0.85", "New-Horizons-0.85-Windows-x64.zip")));
			require(offered(fixture("0.86", "New-Horizons-0.86-Windows-x64.zip")));
		}, count);
		run("VersionsAreNumericNotLexical", []
		{
			require(offered(fixture("v0.100.0", "New-Horizons-0.100-Windows-x64.zip"), WINDOWS, "0.99.9"));
			require(!offered(fixture("v0.9.0", "New-Horizons-0.9-Windows-x64.zip"), WINDOWS, "0.10.0"));
		}, count);
		run("MalformedOrUnstableVersionsReject", []
		{
			for(const std::string bad : {"", "1", "0.86.", "0..86", "00.86.0", "0.086.0", "-1.86.0", "0.86.0-rc1", "0.86.0+build", "0.86.0.1", "v", "V0.86.0", " 0.86.0", "0.86.0/evil", "999999999999.0.0"})
			{
				require(!offered(fixture(bad)));
				require(!offered(fixture(), WINDOWS, bad));
			}
			require(!offered(fixture(), WINDOWS, "v0.85.0"));
		}, count);
		run("DraftPrereleaseAndUnpublishedReject", []
		{
			for(const std::string flag : {"draft", "prerelease"})
			{
				for(const JsonNode bad : {JsonNode(true), JsonNode("false"), JsonNode()})
				{
					auto release = fixture();
					release[flag] = bad;
					require(!offered(release));
				}
			}
			for(const JsonNode bad : {JsonNode(), JsonNode(false), JsonNode(""), JsonNode("yesterday"), JsonNode("2026-02-30T12:34:56Z"), JsonNode("2026-10-10T25:34:56Z")})
			{
				auto release = fixture();
				release["published_at"] = bad;
				require(!offered(release));
			}
		}, count);
		run("MissingEmptyOrWrongPlatformAssetsReject", []
		{
			auto release = fixture();
			release["assets"].Vector().clear();
			require(!offered(release));
			release["assets"].clear();
			require(!offered(release));
			require(!offered(fixture(), LINUX));
			require(!offered(fixture("v0.86.0", "New-Horizons-0.86-Linux-x64.tar.gz"), WINDOWS));
		}, count);
		run("LinuxAndMaintainedRevisionPackagesAccepted", []
		{
			for(const std::string name : {"New-Horizons-0.86.0-Linux-x64.tar.gz", "New-Horizons-Linux-x64.tar.gz", "New-Horizons-Linux-x64-012345abcdef.tar.gz"})
				require(offered(fixture("v0.86.0", name), LINUX));
			require(offered(fixture("v0.86.0", "New-Horizons-Windows-x64-012345abcdef.zip")));
		}, count);
		run("SourceDependenciesWrongVersionAndUnsafeNamesReject", []
		{
			for(const std::string name : {"source.zip", "New-Horizons-Windows-x64-012345abcdef-source.zip", "New-Horizons-0.86-Windows-x64-dependency-sources.tar.gz", "New-Horizons-0.85-Windows-x64.zip", "New-Horizons-0.86-Windows-arm64.zip", "../New-Horizons-0.86-Windows-x64.zip", "New-Horizons-Windows-x64-notarevision.zip"})
				require(!offered(fixture("v0.86.0", name)));
		}, count);
		run("AssetMustBeUploadedAndNonempty", []
		{
			for(const JsonNode bad : {JsonNode(0), JsonNode(-1), JsonNode("123"), JsonNode(1.5), JsonNode()})
			{
				auto release = fixture();
				release["assets"].Vector()[0]["size"] = bad;
				require(!offered(release));
			}
			auto release = fixture();
			release["assets"].Vector()[0]["state"] = JsonNode("open");
			require(!offered(release));
		}, count);
		run("OfficialPageAndDownloadIdentityRequired", []
		{
			for(const std::string key : {"html_url", "browser_download_url"})
			{
				auto release = fixture();
				auto & target = key == "html_url" ? release : release["assets"].Vector()[0];
				target[key] = JsonNode("https://github.com.evil.invalid/thegandalf196/new-horizons/releases/tag/v0.86.0");
				require(!offered(release));
			}
		}, count);
		run("MalformedJsonAndResourceBoundsRejectSilently", []
		{
			for(const std::string response : {"", "{", "[]", "null", "{} trailing", "{\"draft\":false,\"draft\":true}"})
				require(!releaseUpdates::selectOffer("0.85.0", WINDOWS, 200, response));
			require(!releaseUpdates::selectOffer("0.85.0", WINDOWS, 200, std::string(1024 * 1024 + 1, ' ')));
			auto release = fixture();
			release["assets"].Vector().resize(129, release["assets"].Vector().front());
			require(!offered(release));
		}, count);
		run("ExcessiveJsonNestingRejects", []
		{
			auto response = fixture().toCompactString();
			response.pop_back();
			response += ",\"unused\":" + std::string(17, '[') + "0" + std::string(17, ']') + "}";
			require(!releaseUpdates::selectOffer("0.85.0", WINDOWS, 200, response));
		}, count);
		run("LeapDayCalendarBoundaries", []
		{
			for(const std::string date : {"2024-02-29T12:34:56Z", "2000-02-29T12:34:56Z"})
			{
				auto release = fixture();
				release["published_at"] = JsonNode(date);
				require(offered(release));
			}
			for(const std::string date : {"2025-02-29T12:34:56Z", "2100-02-29T12:34:56Z"})
			{
				auto release = fixture();
				release["published_at"] = JsonNode(date);
				require(!offered(release));
			}
		}, count);
		run("BooleanAssetSizeRejects", []
		{
			for(bool size : {false, true})
			{
				auto release = fixture();
				release["assets"].Vector()[0]["size"] = JsonNode(size);
				require(!offered(release));
			}
		}, count);
		run("MalformedFirstAssetDoesNotHideLaterPlayableAsset", []
		{
			auto release = fixture();
			const auto valid = release["assets"].Vector().front();
			release["assets"].Vector().front() = JsonNode(false);
			require(!offered(release));
			release["assets"].Vector().push_back(valid);
			require(offered(release));
		}, count);
		std::cout << "PASS: " << count << " pure release-policy cases (no network or SDL)\n";
		return 0;
	}
	catch(const std::exception &)
	{
		std::cerr << "FAIL: release-policy case " << count + 1 << '\n';
		return 1;
	}
}
