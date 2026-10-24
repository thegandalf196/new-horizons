/*
 * ReplayPayload.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ReplayLog.h"
#include "../networkPacks/NetPacksBase.h"
#include "../networkPacks/PacksForClient.h"
#include "../serializer/CMemorySerializer.h"
#include <stdexcept>

void replayPayload::validateStoredVersion(ESerializationVersion version)
{
	if(version != ESerializationVersion::NONE
		&& (version < ESerializationVersion::MINIMAL || version > ESerializationVersion::CURRENT))
		throw std::runtime_error("Unsupported replay payload encoding version");
}
void replayPayload::requireKnownVersion(ESerializationVersion version)
{
	validateStoredVersion(version);
	if(version == ESerializationVersion::NONE)
		throw std::runtime_error("Replay payload encoding version is unknown; historical bytes cannot be replayed safely");
}
ReplayPayload ReplayPackSerializer::write(const CPackForClient & pack)
{
	CMemorySerializer serializer;
	serializer.oser & &pack;
	return {serializer.extractBuffer(), serializer.oser.version};
}
std::unique_ptr<CPack> ReplayPackSerializer::read(const ReplayPayload & payload, IGameInfoCallback * cb)
{
	replayPayload::requireKnownVersion(payload.version);
	CMemorySerializer serializer(payload.bytes);
	serializer.iser.version = payload.version;
	serializer.iser.cb = cb;
	std::unique_ptr<CPack> result;
	serializer.iser & result;
	if(result == nullptr)
		throw std::runtime_error("Failed to read a netpack from the replay log!");
	return result;
}
