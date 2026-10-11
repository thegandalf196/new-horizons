/*
 * CNhArtLoader.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "ISimpleResourceLoader.h"
#include "ResourcePath.h"
namespace nhart { struct PreparedCache; }

/// Prepares verified file backing before any resource is made available.
class DLL_LINKAGE CNhArtLoader : public ISimpleResourceLoader
{
public:
	/// DirectArchiveForTests is an explicit benchmark seam, never a runtime fallback.
	enum class Backing { PreparedFiles, DirectArchiveForTests };
	CNhArtLoader(std::string mountPoint, boost::filesystem::path archive,
		boost::filesystem::path cacheRoot = {}, Backing backing = Backing::PreparedFiles);
	std::unique_ptr<CInputStream> load(const ResourcePath & resourceName) const override;
	bool existsResource(const ResourcePath & resourceName) const override;
	std::string getMountPoint() const override;
	void updateFilteredFiles(std::function<bool(const std::string &)> filter) override {}
	std::unordered_set<ResourcePath> getFilteredFiles(std::function<bool(const ResourcePath &)> filter) const override;
	std::string getFullFileURI(const ResourcePath & resourceName) const override;
	std::time_t getLastWriteTime(const ResourcePath & resourceName) const override;

private:
	struct Entry
	{
		std::string name;
		si64 offset;
		si64 length;
		size_t record;
	};
	boost::filesystem::path archive;
	std::string mountPoint;
	std::unordered_map<ResourcePath, Entry> entries;
	std::shared_ptr<const nhart::PreparedCache> cache;
	Backing backing;
};
