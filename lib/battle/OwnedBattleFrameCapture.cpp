/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedBattleFrameBatch.h"
#include "OwnedBattleFrameSubject.h"
#include "OwnedFrameBatchBuilder.h"
#include "OwnedFrameCaptureEnvironment.h"
#include "OwnedUnitEnvironmentPolicy.h"
#include "OwnedLegacyTimeStopCapture.h"
#include "BattleInfo.h"
#include "CBattleInfoCallback.h"
#include "../CStack.h"
#include <set>
#include <stdexcept>
namespace battle
{
const std::shared_ptr<const OwnedUnitBonusFrame> & OwnedBattleFrameBatch::frame(uint32_t id) const
{
	const Occurrence * found = nullptr;
	for(const auto & occurrence : units)
		if(occurrence.identity.id == id)
		{
			if(found) throw std::invalid_argument("Ambiguous owned active-battle occurrence");
			found = &occurrence;
		}
	if(!found) throw std::out_of_range("Unit was outside the captured callback-visible active battle");
	return found->frame;
}
std::shared_ptr<const OwnedBattleFrameBatch> OwnedBattleFrameCapture::capture(
	const BattleInfo & native, const CBattleInfoCallback & observer, uint32_t activeId, const OwnedBonusCaptureCodec & codec)
{
	if(!observer.getPlayerID() || observer.getBattle() != &native)
		throw std::invalid_argument("Owned capture callback occurrence mismatch");
	const auto roster = observer.battleGetUnitsIf([](const Unit *) { return true; });
	std::vector<const CStack *> visible; std::set<uint32_t> ids;
	for(const auto * unit : roster)
	{
		const auto * source = dynamic_cast<const CStack *>(unit);
		if(!source || source->getBattle() != &native || !ids.insert(source->unitId()).second)
			throw std::invalid_argument("Owned capture requires exact unique callback-visible native occurrences");
		visible.push_back(source); // Both sides/corpses, exact callback order; no cap or target search.
	}
	if(native.activeStack != activeId || !ids.contains(activeId))
		throw std::invalid_argument("Owned capture active occurrence mismatch");
	ownedFrameCapture::Builder builder(visible);
	auto environment = ownedFrameCapture::environmentFacts(observer);
	environment.enginePolicy = OwnedUnitEnvironmentPolicy::capture(observer);
	for(size_t i = 0; i < visible.size(); ++i) builder.recipient(*visible[i], visible, environment, i);
	std::vector<std::shared_ptr<const Bonus>> clockRecords;
	for(const auto & graph : builder.graphs)
	{
		for(const auto & node : graph.nodes) clockRecords.insert(clockRecords.end(), node.local.begin(), node.local.end());
		for(const auto * records : {&graph.extraRecords, &graph.sourceExports, &graph.retainedRecords})
			clockRecords.insert(clockRecords.end(), records->begin(), records->end());
	}
	const auto clockIdentities = ownedFrameCapture::captureLegacyTimeStopIdentities(clockRecords);
	for(auto & recipe : builder.recipes) recipe.inputs.environment.legacyTimeStop = clockIdentities;
	auto frames = OwnedUnitBonusFrameBatch::fromOwnedSourceRecipes(std::move(builder.graphs),
		std::move(builder.recipes), std::move(builder.relations), codec);
	auto result = std::shared_ptr<OwnedBattleFrameBatch>(new OwnedBattleFrameBatch);
	result->battleId = native.getBattleID(); result->activeId = activeId; result->player = *observer.getPlayerID();
	result->serial = native.getActivationSerial();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		result->policies.at(side) = observer.battleGetVisibleHeroPolicy(side).informationOnly();
	for(size_t i = 0; i < frames.size(); ++i)
		result->units.push_back({ownedFrameCapture::identity(*visible[i]), std::move(frames[i])});
	result->scene = OwnedBattleFrameSubject::captureScene(observer, *result);
	result->sealedSubject = OwnedBattleFrameSubject::seal(observer, *result);
	return result; // No native identity/callback/world/callable survives the owning receive interval.
}
}
