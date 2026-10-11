/*
 * CNhArtLoader.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CNhArtLoader.h"

#include "CFileInputStream.h"
#include "NhArtCache.h"
#include "../VCMIDirs.h"
#include "../json/JsonParser.h"
#include "../texts/TextOperations.h"

#include <array>
#include <limits>

namespace
{
constexpr ui64 HEADER_SIZE = 32;
constexpr ui64 RECORD_SIZE = 56;
constexpr ui64 MAX_ENTRIES = 1000000;
constexpr ui64 MAX_INDEX_SIZE = 256 * 1024 * 1024;
constexpr ui32 MAX_NAME_SIZE = 4096;

void require(bool condition, const std::string & reason, const std::string & archiveName)
{
	if(!condition)
		throw std::runtime_error("Invalid NHART archive '" + archiveName + "': " + reason);
}

template<size_t Size>
ui64 readLE(CInputStream & stream, const std::string & archiveName)
{
	std::array<ui8, Size> bytes{};
	require(stream.read(bytes.data(), Size) == Size, "truncated integer field", archiveName);
	ui64 result = 0;
	for(size_t i = 0; i < Size; ++i)
		result |= static_cast<ui64>(bytes[i]) << (i * 8);
	return result;
}

bool validUtf8(const std::string & name)
{
	for(size_t i = 0; i < name.size();)
	{
		const auto first = static_cast<ui8>(name[i++]);
		if(first < 0x80)
			continue;
		unsigned int following;
		ui32 code;
		ui32 minimum;
		if(first >= 0xc2 && first <= 0xdf)
		{
			following = 1;
			code = first & 0x1f;
			minimum = 0x80;
		}
		else if(first >= 0xe0 && first <= 0xef)
		{
			following = 2;
			code = first & 0x0f;
			minimum = 0x800;
		}
		else if(first >= 0xf0 && first <= 0xf4)
		{
			following = 3;
			code = first & 0x07;
			minimum = 0x10000;
		}
		else
			return false;
		if(following > name.size() - i)
			return false;
		while(following--)
		{
			const auto next = static_cast<ui8>(name[i++]);
			if((next & 0xc0) != 0x80)
				return false;
			code = (code << 6) | (next & 0x3f);
		}
		if(code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
			return false;
	}
	return true;
}

bool validName(const std::string & name)
{
	if(name == ".nhart/manifest.json")
		return true;
	if(name.empty() || name.front() == '/' || name.find_first_of("\\:") != std::string::npos
		|| name.find('\0') != std::string::npos || !validUtf8(name))
		return false;
	for(size_t start = 0; start <= name.size();)
	{
		const auto end = name.find('/', start);
		const auto part = name.substr(start, end == std::string::npos ? end : end - start);
		if(part.empty() || part == "." || part == ".." || part.back()=='.' || part.back()==' '
			|| std::any_of(part.begin(),part.end(),[](unsigned char c){return c<32 || c==127;}))
			return false;
		auto stem=part.substr(0,part.find('.'));
		std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
		if(stem=="CON" || stem=="PRN" || stem=="AUX" || stem=="NUL"
			|| (stem.size()==4 && (stem.substr(0,3)=="COM" || stem.substr(0,3)=="LPT") && stem[3]>='1' && stem[3]<='9'))
			return false;
		if(end == std::string::npos)
			break;
		start = end + 1;
	}
	return true;
}

/// Each instance owns its own file stream. Empty entries never invoke the
/// CFileInputStream convention that size zero means the entire file.
class NhArtInputStream final : public CInputStream
{
	std::unique_ptr<CFileInputStream> file;
	std::string archiveName;
	si64 length;
	si64 position = 0;
public:
	NhArtInputStream(const boost::filesystem::path & archive, si64 offset, si64 length)
		: archiveName(archive.filename().string()), length(length)
	{
		if(length > 0)
			file = std::make_unique<CFileInputStream>(archive, offset, length);
	}
	si64 read(ui8 * data, si64 size) override
	{
		if(size < 0)
			return -1;
		const auto count = std::min(size, length - position);
		if(count == 0)
			return 0;
		const auto actual = file->read(data, count);
		require(actual == count, "truncated payload read", archiveName);
		position += actual;
		return actual;
	}
	si64 seek(si64 target) override
	{
		if(target < 0)
			return -1;
		target = std::min(target, length);
		if(file)
			require(file->seek(target) == target, "payload seek failed", archiveName);
		position = target;
		return position;
	}
	si64 skip(si64 delta) override
	{
		if(delta < -position)
			return -1;
		const auto previous = position;
		seek(delta > length - position ? length : position + delta);
		return position - previous;
	}
	si64 tell() override { return position; }
	si64 getSize() override { return length; }
};
}

CNhArtLoader::CNhArtLoader(std::string mountPoint, boost::filesystem::path archive,
	boost::filesystem::path cacheRoot, Backing backing)
	: archive(std::move(archive)), mountPoint(std::move(mountPoint)), backing(backing)
{
	const auto archiveName = this->archive.filename().string();
	// Source aliases are normal in developer installs; resolve exactly once.
	// Cache targets, unlike source mounts, never permit links/reparse traversal.
	const auto source=boost::filesystem::canonical(this->archive);
	const auto check = [&archiveName](bool condition, const std::string & reason)
	{
		require(condition, reason, archiveName);
	};
	const auto fileSize = boost::filesystem::file_size(source);
	check(fileSize >= HEADER_SIZE && fileSize <= static_cast<ui64>(std::numeric_limits<si64>::max()),
		"header missing or archive size exceeds signed stream range");
	CFileInputStream stream(source);
	std::array<ui8, 8> magic{};
	check(stream.read(magic.data(), magic.size()) == magic.size(), "truncated header magic");
	check(magic == std::array<ui8, 8>{'N', 'H', 'A', 'R', 'T', '\r', '\n', 0x1a}, "incorrect magic");
	check(readLE<4>(stream, archiveName) == 1, "unsupported version");
	check(readLE<4>(stream, archiveName) == 0, "unsupported header flags");
	const auto indexOffset = readLE<8>(stream, archiveName);
	const auto count = readLE<8>(stream, archiveName);
	check(indexOffset >= HEADER_SIZE && indexOffset <= fileSize, "index offset outside archive bounds");
	const auto indexSize = fileSize - indexOffset;
	check(count <= MAX_ENTRIES, "entry count exceeds limit");
	check(indexSize <= MAX_INDEX_SIZE, "index size exceeds limit");
	check(count <= indexSize / RECORD_SIZE, "entry count cannot fit in index");
	check(stream.seek(static_cast<si64>(indexOffset)) == indexOffset, "index seek failed");
	std::vector<std::pair<ui64, ui64>> ranges;
	std::vector<nhart::Record> records;
	ranges.reserve(count);
	for(ui64 i = 0; i < count; ++i)
	{
		check(fileSize - stream.tell() >= RECORD_SIZE, "truncated index record");
		const auto nameSize = readLE<4>(stream, archiveName);
		check(nameSize >= 1 && nameSize <= MAX_NAME_SIZE, "name length outside 1..4096");
		check(readLE<4>(stream, archiveName) == 0, "nonzero record reserved field");
		const auto offset = readLE<8>(stream, archiveName);
		const auto length = readLE<8>(stream, archiveName);
		std::array<ui8, 32> digest{};
		check(stream.read(digest.data(), digest.size()) == digest.size(), "truncated digest field");
		check(nameSize <= fileSize - stream.tell(), "name extends past index EOF");
		std::string name(nameSize, '\0');
		check(stream.read(reinterpret_cast<ui8 *>(name.data()), nameSize) == nameSize, "truncated resource name");
		check(validName(name), "invalid UTF-8 or unsafe relative resource name");
		check(offset >= HEADER_SIZE && offset <= indexOffset && length <= indexOffset - offset,
			"payload offset/length outside payload bounds");
		ResourcePath resource(this->mountPoint + name);
		if(resource == ResourcePath(this->mountPoint + ".nhart/manifest.json"))
			check(name == ".nhart/manifest.json", "reserved manifest name must use its exact spelling");
		check(entries.emplace(resource, Entry{name, static_cast<si64>(offset), static_cast<si64>(length),records.size()}).second,
			"normalized resource identity collision");
		records.push_back({name,offset,length,digest});
		if(length > 0)
			ranges.emplace_back(offset, offset + length);
	}
	check(stream.tell() == fileSize, "index does not end exactly at EOF");
	std::sort(ranges.begin(), ranges.end());
	for(size_t i = 1; i < ranges.size(); ++i)
		check(ranges[i - 1].second <= ranges[i].first, "overlapping payload ranges");
	const auto metadata=std::find_if(records.begin(),records.end(),[](const auto & record){return record.name==".nhart/manifest.json";});
	check(metadata!=records.end() && metadata->size<=MAX_INDEX_SIZE,"missing or oversized embedded manifest");
	std::vector<ui8> manifestBytes(static_cast<size_t>(metadata->size));
	check(stream.seek(metadata->offset)==metadata->offset && stream.read(manifestBytes.data(),manifestBytes.size())==manifestBytes.size(),"manifest read failed");
	check(nhart::sha256(manifestBytes)==metadata->digest,"manifest SHA256 mismatch");
	JsonParsingSettings parsing;parsing.mode=JsonParsingSettings::JsonFormatMode::JSON;
	JsonParser parser(reinterpret_cast<const char *>(manifestBytes.data()),manifestBytes.size(),parsing);
	const auto manifest=parser.parse("NHART embedded manifest");
	check(parser.isValid() && manifest.isStruct() && manifest["format"].isNumber() && manifest["format"].Float()==1
		&& manifest["entries"].isVector(),"invalid embedded manifest");
	std::map<std::string,const nhart::Record *> selected;
	for(const auto & record:records)if(record.name!=".nhart/manifest.json")selected.emplace(record.name,&record);
	check(manifest["entries"].Vector().size()==selected.size(),"manifest inventory count mismatch");
	for(const auto & declared:manifest["entries"].Vector())
	{
		check(declared.isStruct() && declared["resource"].isString() && declared["size"].isNumber() && declared["sha256"].isString(),"invalid manifest inventory row");
		const auto found=selected.find(declared["resource"].String());
		check(found!=selected.end() && declared["size"].Float()>=0 && declared["size"].Float()==static_cast<double>(found->second->size)
			&& declared["sha256"].String()==nhart::hex(found->second->digest),"manifest inventory mismatch");
		selected.erase(found);
	}
	check(selected.empty(),"missing manifest inventory row");
	if(backing==Backing::PreparedFiles)
		cache=nhart::prepareCache(source,cacheRoot.empty()?VCMIDirs::get().userCachePath():cacheRoot,records);
	// Self-contained packaging metadata participates in all structural checks
	// above, but is not a gameplay resource.
	entries.erase(ResourcePath(this->mountPoint + ".nhart/manifest.json"));
}

std::unique_ptr<CInputStream> CNhArtLoader::load(const ResourcePath & resourceName) const
{
	const auto & entry = entries.at(resourceName);
	logGlobal->trace("Loading NHART resource %s from %s", entry.name, archive.filename().generic_string());
	return backing==Backing::PreparedFiles
		? std::make_unique<NhArtInputStream>(cache->path/nhart::cacheFileName(entry.record),0,entry.length)
		: std::make_unique<NhArtInputStream>(archive,entry.offset,entry.length);
}

bool CNhArtLoader::existsResource(const ResourcePath & resourceName) const
{
	return entries.count(resourceName) != 0;
}

std::string CNhArtLoader::getMountPoint() const
{
	return mountPoint;
}

std::unordered_set<ResourcePath> CNhArtLoader::getFilteredFiles(std::function<bool(const ResourcePath &)> filter) const
{
	std::unordered_set<ResourcePath> result;
	for(const auto & [resource, entry] : entries)
		if(filter(resource))
			result.insert(resource);
	return result;
}

std::string CNhArtLoader::getFullFileURI(const ResourcePath & resourceName) const
{
	return TextOperations::filesystemPathToUtf8(archive) + '/' + entries.at(resourceName).name;
}

std::time_t CNhArtLoader::getLastWriteTime(const ResourcePath & resourceName) const
{
	entries.at(resourceName);
	return boost::filesystem::last_write_time(archive);
}
