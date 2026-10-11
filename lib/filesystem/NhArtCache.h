/* Verified pre-game NHART cache, GPL-2.0-or-later. */
#pragma once
#include "NhArtSha256.h"

namespace nhart
{
struct DLL_LINKAGE Record
{
	std::string name;
	ui64 offset;
	ui64 size;
	Digest digest;
};
struct DLL_LINKAGE PreparedCache
{
	boost::filesystem::path path;
	ui64 archiveSize;
	std::time_t archiveTime;
	std::vector<Record> records;
};
/// Records have already passed the loader's complete structural/identity checks.
/// Cache names are opaque record numbers, never archive-provided native paths.
DLL_LINKAGE std::shared_ptr<const PreparedCache> prepareCache(const boost::filesystem::path & archive,
	const boost::filesystem::path & root, const std::vector<Record> & records);
DLL_LINKAGE std::string cacheFileName(size_t record);
}
