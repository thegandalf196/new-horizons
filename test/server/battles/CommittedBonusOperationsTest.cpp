/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/battle/CommittedBonusOperations.h"
#include "../../../lib/battle/LiveUnitEnvironmentContext.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/OwnedBattleFrameRefusals.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/bonuses/Limiters.h"
#include <stdexcept>
namespace
{
/// Deliberately self-certifies an unregistered same-ID CStack. A callback's
/// virtual lookup is not proof of native occurrence ownership.
class ForeignRoster final : public CPlayerBattleCallback
{
	const battle::Unit & foreign;
public:
	ForeignRoster(const BattleInfo * native, PlayerColor observer, const battle::Unit & foreign)
		: CPlayerBattleCallback(native, observer), foreign(foreign) {}
	battle::Units battleGetUnitsIf(const battle::UnitFilter & predicate) const override
	{
		return predicate(&foreign) ? battle::Units{&foreign} : battle::Units{};
	}
	const battle::Unit * battleGetUnitByID(uint32_t id) const override
	{
		return id == foreign.unitId() ? &foreign : nullptr;
	}
};
}
class CommittedBonusOperationsTest : public BattleTestFixture {};
TEST_F(CommittedBonusOperationsTest, ExactNativeBothObserverCapturesDoNotMutateUnitOrBonusGraph)
{
	startGame();
	startBattle();
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(80), 100);
	auto operation = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PHYSICAL_DEFENSE,
		BonusSource::HERO_SPECIAL, 0, BonusSourceID());
	operation->updater = std::make_shared<TimesStackSizeUpdater>(0, 10, 10, 1);
	operation->stacking = "fixture:private-count-source";
	operation->limiter = std::make_shared<CCreatureTypeLimiter>(*unit->unitType(), true);
	unit->addNewBonus(operation);
	const auto before = unit->save();
	const auto version = unit->getTreeVersion();
	const battle::LiveUnitEnvironmentContext authority(*battle());
	EXPECT_NE(authority.committedBonusOperations(), nullptr);
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		const CPlayerBattleCallback observer(battle(), player);
		const battle::LiveUnitEnvironmentContext view(observer);
		EXPECT_NE(view.committedBonusOperations(), nullptr);
	}
	EXPECT_EQ(unit->save(), before);
	EXPECT_EQ(unit->getTreeVersion(), version);
}
TEST_F(CommittedBonusOperationsTest, CallbackCannotSelfCertifyForeignSameIdOccurrenceEvenWithNativeBattlePointer)
{
	startGame();
	startBattle();
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(80), 100);
	CStackBasicDescriptor descriptor(unit->creatureId(), unit->unitBaseAmount());
	CStack foreign(&descriptor, unit->unitOwner(), unit->unitId(), unit->unitSide(), unit->unitSlot());
	foreign.initialPosition = BattleHex(82);
	foreign.localInit(battle()); // Real source attachment, deliberately never registered.
	ASSERT_EQ(foreign.getBattle(), battle());
	ASSERT_EQ(foreign.unitId(), unit->unitId());
	ASSERT_NE(battle()->battleGetStackByID(unit->unitId(), false), &foreign);
	const ForeignRoster forged(battle(), PlayerColor(1), foreign);
	ASSERT_EQ(forged.battleGetUnitByID(unit->unitId()), &foreign);
	const battle::LiveUnitEnvironmentContext context(forged);
	EXPECT_THROW(context.committedBonusOperations(), std::invalid_argument);
}
TEST(CommittedBonusOperationAdmissionTest, NonNativeAndMissingSourcesHaveNoNumericOperationAuthority)
{
	const battle::ownedFrameCapture::RefusingBattleInfo unknown;
	const CPlayerBattleCallback sealed(&unknown, PlayerColor(0));
	const battle::LiveUnitEnvironmentContext unsupported(sealed);
	EXPECT_EQ(unsupported.committedBonusOperations(), nullptr);
	const CPlayerBattleCallback absent(nullptr, PlayerColor(0));
	EXPECT_EQ(battle::LiveUnitEnvironmentContext(absent).committedBonusOperations(), nullptr);
}

TEST(CommittedBonusOperationAdmissionTest, EmptyNativeSourceDoesNotAdmitAnInactiveHotQueryLane)
{
	BattleInfo empty(nullptr);
	const battle::LiveUnitEnvironmentContext context(empty);
	EXPECT_EQ(context.committedBonusOperations(), nullptr);
}

TEST_F(CommittedBonusOperationsTest, ClassifiedBonusCannotSmuggleWorldDependentLimiterIntoNumericAuthority)
{
	startGame();
	startBattle();
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(80), 100);
	auto operation = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PHYSICAL_DEFENSE,
		BonusSource::HERO_SPECIAL, 0, BonusSourceID());
	operation->updater = std::make_shared<TimesStackSizeUpdater>(0, 10, 10, 1);
	operation->limiter = std::make_shared<HasAnotherBonusLimiter>(BonusType::PRIMARY_SKILL);
	unit->addNewBonus(operation);
	const battle::LiveUnitEnvironmentContext source(*battle());
	EXPECT_THROW(source.committedBonusOperations(), std::invalid_argument);
}
