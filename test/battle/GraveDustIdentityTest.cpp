/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitEnvironment.h"
#include "mock/mock_UnitInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsGraveDust.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/battle/HealingReceipt.h"
#include "../../lib/bonuses/RecipientBonusOverlay.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/CCreatureHandler.h"
#include <algorithm>
#include <stdexcept>
namespace
{
using namespace ::testing;
class GraveDustIdentityTest : public Test
{
protected:
	UnitInfoMock info;
	UnitEnvironmentMock env;
	BonusBearerMock bonuses;
	battle::CUnitStateDetached state{&info, &bonuses};
	void SetUp() override
	{
		EXPECT_CALL(info, unitBaseAmount()).WillRepeatedly(Return(3));
		EXPECT_CALL(info, unitType()).WillRepeatedly(Return(CreatureID(0).toCreature()));
		EXPECT_CALL(info, unitId()).WillRepeatedly(Return(17));
		EXPECT_CALL(info, unitSide()).WillRepeatedly(Return(BattleSide::ATTACKER));
		EXPECT_CALL(info, unitOwner()).WillRepeatedly(Return(PlayerColor(0)));
		EXPECT_CALL(info, unitSlot()).WillRepeatedly(Return(SlotID(0)));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 100, BonusSourceID()));
		state.localInit(&env);
	}
};
TEST_F(GraveDustIdentityTest, ValidRecordsOwnOneTypedSourceGroupAndZeroCreatesNothing)
{
	EXPECT_TRUE(newHorizonsGraveDust::records(0, PlayerColor::CANNOT_DETERMINE).empty());
	for(const int value : {1, 5000, 10'000})
	{
		const auto group = newHorizonsGraveDust::records(value, PlayerColor(0));
		ASSERT_EQ(group.size(), 2u);
		ASSERT_NO_THROW(newHorizonsGraveDust::validateSourceGroup(group));
		for(const auto & record : group) EXPECT_TRUE(newHorizonsGraveDust::validateRecord(record));
		EXPECT_EQ(physicalAfflictions::markerMetadata(group.front())->kind, "graveDust");
		EXPECT_EQ(group.back().val, value);
	}
	EXPECT_THROW(newHorizonsGraveDust::records(-1, PlayerColor(0)), std::invalid_argument);
	EXPECT_THROW(newHorizonsGraveDust::records(10'001, PlayerColor(0)), std::invalid_argument);
	EXPECT_THROW(newHorizonsGraveDust::records(5000, PlayerColor::CANNOT_DETERMINE), std::invalid_argument);
}
TEST_F(GraveDustIdentityTest, GraveDustKindCannotClaimAnotherSourceOrNumericallyEqualDifferentVariant)
{
	for(int kind = 0; kind < 3; ++kind)
	{
		auto group = newHorizonsGraveDust::records(5000, PlayerColor(0));
		auto & marker = group.front();
		if(kind == 0) marker.source = BonusSource::HERO_SPECIAL;
		if(kind == 1) marker.sid = BonusSourceID(BonusCustomSource(0x4E48));
		if(kind == 2) marker.sid = BonusSourceID(HeroTypeID(0x4E47));
		const auto before = marker.parameters->toCustom<JsonNode>();
		const auto beforeSource = marker.source;
		const auto beforeSID = marker.sid;
		EXPECT_THROW(physicalAfflictions::markerMetadata(marker), std::invalid_argument);
		EXPECT_THROW(newHorizonsGraveDust::validateRecord(marker), std::invalid_argument);
		EXPECT_THROW(physicalAfflictions::stampApplicationOrder(state, group), std::invalid_argument);
		BonusBearerMock isolated;
		isolated.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 100, BonusSourceID()));
		for(const auto & record : group) isolated.addNewBonus(std::make_shared<Bonus>(record));
		battle::CUnitStateDetached victim(&info, &isolated);
		victim.localInit(&env);
		EXPECT_THROW(physicalAfflictions::enumerate(victim), std::invalid_argument);
		physicalAfflictions::Affliction forged;
		forged.kind = "graveDust";
		forged.source = marker.source;
		forged.sourceID = marker.sid;
		EXPECT_THROW(physicalAfflictions::removalPlan(victim, forged), std::invalid_argument);
		EXPECT_EQ(marker.parameters->toCustom<JsonNode>(), before);
		EXPECT_EQ(marker.source, beforeSource);
		EXPECT_TRUE(marker.sid == beforeSID);
	}
}
TEST_F(GraveDustIdentityTest, ReservedPairRejectsOtherKindsAndMalformedCompanions)
{
	auto group = newHorizonsGraveDust::records(5000, PlayerColor(0));
	auto badMarker = group.front();
	auto metadata = badMarker.parameters->toCustom<JsonNode>();
	metadata["kind"].String() = "poison";
	badMarker.parameters = std::make_shared<BonusParameters>(metadata);
	EXPECT_THROW(physicalAfflictions::markerMetadata(badMarker), std::invalid_argument);
	for(int kind = 0; kind < 5; ++kind)
	{
		auto bad = group.back();
		if(kind == 0) bad.type = BonusType::NONE;
		if(kind == 1) std::reverse(bad.statusTags.begin(), bad.statusTags.end());
		if(kind == 2) bad.spellCasterOwner = PlayerColor::CANNOT_DETERMINE;
		if(kind == 3) bad.val = 0;
		if(kind == 4) bad.parameters = std::make_shared<BonusParameters>(1);
		EXPECT_THROW(newHorizonsGraveDust::validateRecord(bad), std::invalid_argument);
	}
}
TEST_F(GraveDustIdentityTest, EnumerationAndRemovalNeverIncludeIntrinsicMoandorRider)
{
	auto intrinsic = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::HERO_SPECIAL, 1,
		BonusSourceID(HeroTypeID(HeroTypeID::decode("core:moandor"))));
	bonuses.addNewBonus(intrinsic);
	for(const auto & record : newHorizonsGraveDust::records(5000, PlayerColor(0)))
		bonuses.addNewBonus(std::make_shared<Bonus>(record));
	const auto groups = physicalAfflictions::enumerate(state);
	ASSERT_EQ(groups.size(), 1u);
	ASSERT_EQ(groups.front().kind, "graveDust");
	const auto removal = physicalAfflictions::removalPlan(state, groups.front());
	ASSERT_EQ(removal.size(), 2u);
	for(const auto & record : removal) EXPECT_TRUE(newHorizonsGraveDust::isReservedSource(record));
	EXPECT_TRUE(state.hasBonus(Selector::source(BonusSource::HERO_SPECIAL, intrinsic->sid)));
}
TEST_F(GraveDustIdentityTest, StandaloneDescriptorDoesNotMakeAnOrphanInstalledGroupValid)
{
	const auto group = newHorizonsGraveDust::records(5000, PlayerColor(0));
	EXPECT_TRUE(newHorizonsGraveDust::validateRecord(group.front()));
	bonuses.addNewBonus(std::make_shared<Bonus>(group.front()));
	EXPECT_THROW(physicalAfflictions::enumerate(state), std::invalid_argument);
	EXPECT_THROW(newHorizonsGraveDust::validateSourceGroup({group.back()}), std::invalid_argument);
}
TEST_F(GraveDustIdentityTest, UnrelatedOldMarkerKindsKeepTheirExistingMetadata)
{
	Bonus old(BonusDuration::PERMANENT, BonusType::PHYSICAL_AFFLICTION,
		BonusSource::OTHER, 0, BonusSourceID(BonusCustomSource(17)));
	JsonNode metadata;
	metadata["kind"].String() = "unrelated-existing-kind";
	old.parameters = std::make_shared<BonusParameters>(metadata);
	EXPECT_FALSE(newHorizonsGraveDust::validateRecord(old));
	ASSERT_NO_THROW(physicalAfflictions::markerMetadata(old));
	EXPECT_EQ(physicalAfflictions::markerMetadata(old)->kind, "unrelated-existing-kind");
}
TEST_F(GraveDustIdentityTest, OrphanReductionAndHiddenDuplicateCompanionsAreRejected)
{
	const auto group = newHorizonsGraveDust::records(5000, PlayerColor(0));
	bonuses.addNewBonus(std::make_shared<Bonus>(group.back()));
	EXPECT_THROW(physicalAfflictions::enumerate(state), std::invalid_argument);
	bonuses.addNewBonus(std::make_shared<Bonus>(group.front()));
	EXPECT_NO_THROW(physicalAfflictions::enumerate(state));
	auto duplicate = group.back();
	duplicate.val = 2500;
	bonuses.addNewBonus(std::make_shared<Bonus>(duplicate));
	EXPECT_THROW(physicalAfflictions::enumerate(state), std::invalid_argument);
}
TEST_F(GraveDustIdentityTest, MalformedInactiveGroupCannotAffectActualHealing)
{
	const auto group = newHorizonsGraveDust::records(5000, PlayerColor(0));
	bonuses.addNewBonus(std::make_shared<Bonus>(group.back()));
	EXPECT_THROW(battle::healingReceivedReduction(state), std::invalid_argument);
	bonuses.addNewBonus(std::make_shared<Bonus>(group.front()));
	EXPECT_EQ(battle::healingReceivedReduction(state), 5000);
	auto duplicate = group.back();
	duplicate.val = 2500;
	bonuses.addNewBonus(std::make_shared<Bonus>(duplicate));
	EXPECT_THROW(battle::healingReceivedReduction(state), std::invalid_argument);
}
TEST_F(GraveDustIdentityTest, SnapshotAdmissionUsesOnlyCanonicalIdentityBeforeGenericSourceGuard)
{
	const auto group = newHorizonsGraveDust::records(5000, PlayerColor(0));
	for(const auto & record : group)
		EXPECT_TRUE(recipientBonusOverlay::isSnapshotHeroSpecialEffect(&record));
	auto forged = group.front();
	forged.source = BonusSource::HERO_SPECIAL;
	EXPECT_THROW(recipientBonusOverlay::isSnapshotHeroSpecialEffect(&forged), std::invalid_argument);
	auto unrelated = group.back();
	unrelated.sid = BonusSourceID(BonusCustomSource(17));
	unrelated.stacking.clear();
	unrelated.statusIdentity.clear();
	EXPECT_FALSE(recipientBonusOverlay::isSnapshotHeroSpecialEffect(&unrelated));
}
TEST_F(GraveDustIdentityTest, MismatchedValidCasterProvenanceCannotAffectActualHealing)
{
	auto group = newHorizonsGraveDust::records(5000, PlayerColor(0));
	group.back().spellCasterOwner = PlayerColor(1);
	for(const auto & record : group) ASSERT_TRUE(newHorizonsGraveDust::validateRecord(record));
	EXPECT_THROW(newHorizonsGraveDust::validateSourceGroup(group), std::invalid_argument);
	for(const auto & record : group) bonuses.addNewBonus(std::make_shared<Bonus>(record));
	EXPECT_THROW(battle::healingReceivedReduction(state), std::invalid_argument);
}
}
