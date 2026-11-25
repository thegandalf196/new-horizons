/*
 * StackHotStats.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"
#include "ProjectedAdjacencyBonuses.h"
#include "../../lib/CStack.h"
#include <typeinfo>

namespace
{
enum StackHotStatsFlag : uint16_t
{
	HOT_STATS_VETERAN_COHESION = 1 << 0,
	HOT_STATS_FORM_STATE = 1 << 1,
	HOT_STATS_GHOST = 1 << 2,
	HOT_STATS_EXACT_LOCAL_EFFECTS = 1 << 3,
};

/// splitmix64 step: an order-sensitive digest of identities and sizes.
void hotStatsMix(uint64_t & digest, uint64_t value) noexcept
{
	uint64_t z = digest ^ (value + 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	digest = z ^ (z >> 31);
}

void hotStatsMixPointer(uint64_t & digest, const void * pointer) noexcept
{
	hotStatsMix(digest, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer)));
}

template<typename Records>
void hotStatsMixIdentities(uint64_t & digest, const Records & records) noexcept
{
	hotStatsMix(digest, records.size());
	for(const auto & record : records)
		hotStatsMixPointer(digest, record.get());
}

/// Value records may be rewritten in place; mix what a query can read of them.
void hotStatsMixValues(uint64_t & digest, const std::optional<std::vector<Bonus>> & records)
{
	if(!records)
	{
		hotStatsMix(digest, 0x51);
		return;
	}
	hotStatsMix(digest, records->size());
	for(const auto & record : *records)
	{
		hotStatsMix(digest, static_cast<uint64_t>(record.type) | (static_cast<uint64_t>(record.source) << 16)
			| (static_cast<uint64_t>(record.duration) << 32));
		hotStatsMix(digest, static_cast<uint32_t>(record.val) | (static_cast<uint64_t>(static_cast<uint32_t>(record.turnsRemain)) << 32));
		hotStatsMix(digest, static_cast<uint32_t>(record.subtype.getNum()) | (static_cast<uint64_t>(static_cast<uint32_t>(record.sid.getNum())) << 32));
	}
}

template<typename Records>
void hotStatsMixOptionalIdentities(uint64_t & digest, const std::optional<Records> & records) noexcept
{
	if(records)
		hotStatsMixIdentities(digest, *records);
	else
		hotStatsMix(digest, 0x52);
}
}

void StackWithBonuses::mixHotStatsOverlay(uint64_t & digest) const
{
	hotStatsMixPointer(digest, this);
	hotStatsMix(digest, static_cast<uint32_t>(treeVersionLocal));
	hotStatsMix(digest, static_cast<uint32_t>(getBattleFormViewRevision()));
	hotStatsMixIdentities(digest, bonusesToAdd);
	hotStatsMixIdentities(digest, bonusesToUpdate);
	hotStatsMixIdentities(digest, bonusesToRemove);
	hotStatsMixValues(digest, projectedEffects);
	hotStatsMixOptionalIdentities(digest, projectedUnstackedEffects);
	hotStatsMixValues(digest, damageScoringBonuses);
	hotStatsMixOptionalIdentities(digest, damageScoringUnstackedBonuses);
	hotStatsMix(digest, capturedPhysicalAfflictionGroups.size());
	hotStatsMix(digest, (veteranCohesionEarned ? 1 : 0) | (hasBattleFormState() ? 2 : 0));
}

bool StackWithBonuses::hotStatsStamp(StackHotStatsStamp & stamp) const
{
	stamp.bearer = origBearer;
	stamp.branchEpoch = ProjectedAdjacencyBonuses::unitStateEpoch(*owner);
	stamp.branchVersion = owner->getTreeVersion();
	stamp.bearerVersion = origBearer ? origBearer->getTreeVersion() : 0;
	stamp.localVersion = treeVersionLocal;
	stamp.formRevision = getBattleFormViewRevision();
	stamp.round = owner->getRound();
	stamp.activeStack = owner->getActiveStackID();
	stamp.count = getCount();
	stamp.firstHitPoints = getFirstHPleft();
	stamp.position = static_cast<int16_t>(position.toInt());
	stamp.flags = static_cast<uint16_t>((veteranCohesionEarned ? HOT_STATS_VETERAN_COHESION : 0)
		| (hasBattleFormState() ? HOT_STATS_FORM_STATE : 0) | (isGhost() ? HOT_STATS_GHOST : 0)
		| (exactLocalSpellEffects ? HOT_STATS_EXACT_LOCAL_EFFECTS : 0));
	// The same layer walk as mayHaveBonusType: projected layers down to the
	// native or foreign provider, whose bonus-tree version is already above.
	const StackWithBonuses * layer = this;
	for(stamp.layers = 1; ; ++stamp.layers)
	{
		layer->mixHotStatsOverlay(stamp.overlays);
		const battle::Unit * below = layer->originalUnit;
		if(!below || stamp.layers >= 512 || below->getBonusBearer() != layer->origBearer)
			return stamp.layers < 512;
		if(typeid(*below) == typeid(StackWithBonuses))
		{
			layer = static_cast<const StackWithBonuses *>(below);
			continue;
		}
		// A derived projected layer may hold overlays this walk cannot see.
		return typeid(*below) == typeid(CStack) || !dynamic_cast<const StackWithBonuses *>(below);
	}
}

battle::UnitHotStatsAccess StackWithBonuses::hotStatsAccess() const
{
	// Only the views whose bonus-tree version BonusCache already trusts
	// (isBonusCacheable): owned frames, captured modifier programmes and
	// initial-export subjects key their answers internally and bypass. Derived
	// test views count or reroute queries, and another thread never touches
	// the unsynchronised stamp.
	if(hotStats.slots.mode() == battle::UnitHotStats::Mode::OFF || typeid(*this) != typeid(StackWithBonuses)
		|| modifierBaseProgram || ownedQuerySource || !hotStats.ownedByThisThread() || owner->initialExportSubject())
		return {};
	StackHotStatsStamp stamp;
	if(!hotStatsStamp(stamp))
		return {};
	// Aura and committed-operation branches re-run this view's limiters against
	// the whole projected roster. The stamp holds this view's own state and the
	// branch versions, not every neighbour's scalars, so the slot whose bonuses
	// carry spatial limiters (the dynamic damage passives) is not reused there.
	const uint32_t excluded = ProjectedAdjacencyBonuses::projectsRoster(*owner)
		? battle::hotStatBit(battle::HotStat::DYNAMIC_DAMAGE_PASSIVE) : 0;
	return {&hotStats.slots, hotStats.keyFor(stamp), excluded};
}
