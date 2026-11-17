/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "mock/mock_BonusBearer.h"
#include "mock/mock_UnitInfo.h"
#include "mock/mock_UnitEnvironment.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/UnitShieldState.h"
#include "../../lib/battle/UnitStateValueExchange.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include "../../lib/battle/ConsecratedArms.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/JsonSerializer.h"
#include "../../lib/serializer/JsonDeserializer.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <limits>
namespace
{
using namespace battle;
using namespace testing;
TEST(UnitShieldStateTest, FixedCombatStartCapacityRefreshPreservesSpentPool)
{
	RedemptionShieldState pool;
	pool.initialize(1000);
	EXPECT_EQ(pool.capacity, 200);
	EXPECT_EQ(pool.gain(400, 3000, false), 120);
	int64_t damage = 70;
	EXPECT_EQ(pool.absorb(damage), 70);
	EXPECT_EQ(damage, 0);
	pool.initialize(5000);
	EXPECT_EQ(pool.capacity, 200);
	EXPECT_EQ(pool.remaining, 50);
	EXPECT_EQ(pool.gain(1000, 5000, false), 150);
	EXPECT_EQ(pool.remaining, 200);
}
TEST(UnitShieldStateTest, FractionalSelfAndCommunionConversionNeverDoubleCredit)
{
	RedemptionShieldState self, allied;
	self.initialize(1000); allied.initialize(1000);
	for(int i = 0; i < 10; ++i) { self.gain(1, 1000, false); allied.gain(1, 1000, true); }
	EXPECT_EQ(self.remaining, 1);
	EXPECT_EQ(allied.remaining, 0);
	for(int i = 0; i < 10; ++i) allied.gain(1, 1000, true);
	EXPECT_EQ(allied.remaining, 1);
}
TEST(UnitShieldStateTest, ExhaustionDoesNotMeanUninitializedAndLargeConversionIsBounded)
{
	RedemptionShieldState pool;
	pool.initialize(std::numeric_limits<int64_t>::max());
	pool.gain(std::numeric_limits<int64_t>::max(), 5000, false);
	EXPECT_EQ(pool.remaining, pool.capacity);
	int64_t damage = std::numeric_limits<int64_t>::max();
	pool.absorb(damage);
	EXPECT_EQ(pool.remaining, 0);
	EXPECT_TRUE(pool.initialized);
	pool.initialize(100);
	EXPECT_EQ(pool.capacity, std::numeric_limits<int64_t>::max() / 5);
}
TEST(UnitShieldStateTest, VowRoundsUpAndSharesCasualtiesAcrossRiders)
{
	auto quota = VowCasualtyQuota::capture(7, 100, 6000);
	EXPECT_EQ(quota.remainingCasualties, 60);
	EXPECT_EQ(quota.maximumBodyDamage(100, 4, 10), 603);
	quota.consume(100, 40);
	EXPECT_EQ(quota.maximumBodyDamage(40, 1, 10), 0);
	EXPECT_EQ(VowCasualtyQuota::capture(1, 1, 4000).remainingCasualties, 1);
	EXPECT_EQ(VowCasualtyQuota::capture(1, 3, 4000).remainingCasualties, 2);
}
TEST(UnitShieldStateTest, DamageClassIsIndependentFromOriginAndCostsAreExcluded)
{
	EXPECT_TRUE(redemptionCovers({DamageClass::PHYSICAL, DamageOrigin::HERO_SPELL}));
	EXPECT_TRUE(redemptionCovers({DamageClass::MAGICAL, DamageOrigin::CREATURE_ABILITY}));
	EXPECT_TRUE(redemptionCovers({DamageClass::MAGICAL, DamageOrigin::PERIODIC}));
	EXPECT_FALSE(redemptionCovers({DamageClass::HEALTH_COST, DamageOrigin::HERO_SPELL}));
	EXPECT_FALSE(redemptionCovers({DamageClass::UNCLASSIFIED, DamageOrigin::CREATURE_SPELL}));
}
class ShieldInfo : public NiceMock<UnitInfoMock>
{
public:
	std::optional<UnitPreparationHealthBasis> unitPreparationHealthBasis() const override
	{ return UnitPreparationHealthBasis{1000, 0}; }
};
class ConsecratedShieldUnitTest : public Test
{
protected:
	ShieldInfo info;
	NiceMock<UnitEnvironmentMock> environment;
	BonusBearerMock bonuses;
	CUnitStateDetached unit{&info, &bonuses};
	std::vector<std::shared_ptr<Bonus>> prayer;
	void SetUp() override
	{
		ON_CALL(info, unitBaseAmount()).WillByDefault(Return(100));
		ON_CALL(info, unitId()).WillByDefault(Return(7));
		ON_CALL(info, unitType()).WillByDefault(Return(CreatureID(0).toCreature()));
		ON_CALL(info, unitSide()).WillByDefault(Return(BattleSide::ATTACKER));
		ON_CALL(info, unitOwner()).WillByDefault(Return(PlayerColor(0)));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::OTHER, 10, BonusSourceID()));
		unit.localInit(&environment);
		unit.position = BattleHex(5, 5);
	}
	void redemption()
	{
		consecratedArms::Policy policy;
		policy.rank = 1;
		for(const auto & bonus : consecratedArms::prayerBonuses(
			consecratedArms::Prayer::VICARIOUS_REDEMPTION, 200, policy, 3, 1))
		{
			auto value = std::make_shared<Bonus>(bonus);
			bonuses.addNewBonus(value); prayer.push_back(value);
		}
		unit.synchronizeRedemptionShield();
	}
};
TEST_F(ConsecratedShieldUnitTest, GuardianThenRedemptionThenTemporaryBodyAndSharedVow)
{
	redemption();
	unit.redemptionShield.remaining = 50;
	unit.guardianSpiritHitPoints = 20; unit.guardianSpiritRoundsRemaining = 3;
	unit.health.addTemporaryHitPoints(50);
	auto quota = VowCasualtyQuota::capture(7, 100, 6000);
	DamageContext context{DamageClass::PHYSICAL, DamageOrigin::CREATURE_STRIKE, &quota};
	const auto preview = previewShieldDamage(unit, 10000, DamageProvenance::PHYSICAL_CREATURE, &context);
	ASSERT_TRUE(preview);
	EXPECT_EQ(preview->appliedDamage, 659);
	int64_t damage = 10000;
	int64_t damageWitness = -1;
	unit.damageWithContext(damage, false, DamageProvenance::PHYSICAL_CREATURE, context, &damageWitness);
	EXPECT_EQ(damageWitness, 659);
	EXPECT_EQ(damage, 659);
	EXPECT_EQ(unit.getCount(), 40);
	EXPECT_EQ(unit.getFirstHPleft(), 1);
	EXPECT_EQ(unit.guardianSpiritHitPoints, 0);
	EXPECT_EQ(unit.redemptionShield.remaining, 0);
	EXPECT_EQ(quota.remainingCasualties, 0);
	damage = 10000;
	unit.damageWithContext(damage, false, DamageProvenance::MAGICAL_CREATURE, context);
	EXPECT_EQ(unit.getCount(), 40);
	EXPECT_EQ(damage, 0);
}
TEST_F(ConsecratedShieldUnitTest, MagicalCreatureDamageSkipsGuardianButConsumesRedemption)
{
	redemption();
	unit.redemptionShield.remaining = 50;
	unit.guardianSpiritHitPoints = 20; unit.guardianSpiritRoundsRemaining = 3;
	int64_t damage = 80;
	unit.damageWithContext(damage, false, DamageProvenance::MAGICAL_CREATURE,
		{DamageClass::MAGICAL, DamageOrigin::CREATURE_ABILITY});
	EXPECT_EQ(unit.guardianSpiritHitPoints, 20);
	EXPECT_EQ(unit.redemptionShield.remaining, 0);
	EXPECT_EQ(damage, 30);
	EXPECT_EQ(unit.getMagicalCasualties(), 3);
}
TEST_F(ConsecratedShieldUnitTest, RefreshDoesNotRefillAndExpiredPrayerCannotAbsorb)
{
	redemption();
	unit.redemptionShield.remaining = 50;
	int64_t damage = 30;
	unit.damage(damage, false, DamageProvenance::SPELL);
	unit.synchronizeRedemptionShield();
	EXPECT_EQ(unit.redemptionShield.remaining, 20);
	for(auto & marker : prayer) marker->turnsRemain = 0;
	unit.synchronizeRedemptionShield();
	EXPECT_TRUE(unit.redemptionShield.empty());
	damage = 10;
	unit.damage(damage, false, DamageProvenance::SPELL);
	EXPECT_EQ(damage, 10);
}
TEST_F(ConsecratedShieldUnitTest, CompletedActionSurvivesExtraTurnsAndWaitButNotNewRound)
{
	unit.completedActionThisRound = true;
	unit.afterWait();
	EXPECT_TRUE(unit.hasCompletedActionThisRound());
	unit.afterGetsTurn(BattleUnitTurnReason::MORALE);
	EXPECT_TRUE(unit.hasCompletedActionThisRound());
	unit.afterGetsTurn(BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION);
	EXPECT_TRUE(unit.hasCompletedActionThisRound());
	unit.afterNewRound();
	EXPECT_FALSE(unit.hasCompletedActionThisRound());
}
TEST_F(ConsecratedShieldUnitTest, ValueCopiesOwnIndependentShieldAndCompletedReceipts)
{
	redemption();
	unit.redemptionShield.remaining = 73;
	unit.completedActionThisRound = true;
	auto copy = unit.acquireState();
	int64_t damage = 30;
	copy->damage(damage, false, DamageProvenance::SPELL);
	EXPECT_EQ(unit.redemptionShield.remaining, 73);
	EXPECT_EQ(copy->redemptionShield.remaining, 43);
	EXPECT_TRUE(copy->hasCompletedActionThisRound());
	exchangePreparedUnitStateValues(unit, *copy);
	EXPECT_EQ(unit.redemptionShield.remaining, 43);
	EXPECT_EQ(copy->redemptionShield.remaining, 73);
}
TEST_F(ConsecratedShieldUnitTest, FullStateJsonRetainsConsumedPoolAndRejectsMalformedBeforeMutation)
{
	redemption();
	unit.redemptionShield.remaining = 73;
	unit.completedActionThisRound = true;
	auto encoded = unit.save();
	EXPECT_TRUE(hasConsecratedUnitState(encoded));
	auto copy = unit.acquireState();
	copy->load(encoded);
	EXPECT_EQ(copy->redemptionShield.remaining, 73);
	EXPECT_TRUE(copy->hasCompletedActionThisRound());
	encoded["state"]["redemptionShieldHP"].String() = "73";
	EXPECT_THROW(copy->load(encoded), std::runtime_error);
	EXPECT_EQ(copy->redemptionShield.remaining, 73);
}
TEST_F(ConsecratedShieldUnitTest, DeathClearsPoolAndResurrectionDoesNotRecreateStoredShield)
{
	redemption();
	unit.redemptionShield.remaining = 73;
	int64_t damage = 100000;
	unit.damage(damage, false, DamageProvenance::SPELL);
	EXPECT_FALSE(unit.alive());
	EXPECT_TRUE(unit.redemptionShield.empty());
	int64_t heal = 100;
	unit.heal(heal, EHealLevel::RESURRECT, EHealPower::PERMANENT);
	unit.synchronizeRedemptionShield();
	EXPECT_EQ(unit.redemptionShield.remaining, 0);
}
TEST_F(ConsecratedShieldUnitTest, VowUsesActualWoundedRearCohortsInsteadOfFullCreatureApproximation)
{
	JsonNode health;
	JsonSerializer writer(nullptr, health);
	unit.health.serializeJson(writer);
	health["firstHPleft"].Integer() = 4;
	health["fullUnits"].Integer() = 0;
	health["capacityHealthTracking"].Bool() = true;
	health["capacityHealthMax"].Integer() = 10;
	JsonNode weak, strong;
	weak["hitPoints"].Integer() = 3; weak["count"].Integer() = 5;
	strong["hitPoints"].Integer() = 8; strong["count"].Integer() = 4;
	health["capacityHealthCohorts"].Vector() = {weak, strong};
	JsonDeserializer reader(nullptr, health);
	unit.health.serializeJson(reader);
	ASSERT_EQ(unit.getCount(), 10);
	EXPECT_EQ(unit.health.maximumDamageForCasualties(4), 15);
	auto quota = VowCasualtyQuota::capture(7, 10, 4000);
	DamageContext context{DamageClass::PHYSICAL, DamageOrigin::CREATURE_STRIKE, &quota};
	int64_t damage = 10000;
	unit.damageWithContext(damage, false, DamageProvenance::PHYSICAL_CREATURE, context);
	EXPECT_EQ(damage, 15);
	EXPECT_EQ(unit.getCount(), 6);
	EXPECT_EQ(unit.getFirstHPleft(), 1);
}
TEST_F(ConsecratedShieldUnitTest, TemporaryHpBypassDoesNotBypassVowButHealthCostDoes)
{
	unit.health.addTemporaryHitPoints(50);
	auto quota = VowCasualtyQuota::capture(7, 100, 6000);
	DamageContext context{DamageClass::MAGICAL, DamageOrigin::HERO_SPELL, &quota};
	int64_t damage = 10000;
	unit.damageWithContext(damage, false, DamageProvenance::SPELL, context, nullptr, true);
	EXPECT_EQ(unit.getCount(), 40);
	EXPECT_EQ(unit.health.getTemporaryHitPoints(), 50);
	context.damageClass = DamageClass::HEALTH_COST;
	damage = 10000;
	unit.damageWithContext(damage, false, DamageProvenance::OTHER, context, nullptr, true);
	EXPECT_FALSE(unit.alive());
}
TEST_F(ConsecratedShieldUnitTest, ShieldAndCompletionUnitUpdatesRejectOlderWriterBeforeDirectAndOuterPrefix)
{
	redemption();
	unit.redemptionShield.remaining = 73;
	unit.completedActionThisRound = true;
	UnitChanges update(7, UnitChanges::EOperation::UPDATE);
	update.data = unit.save();
	CMemorySerializer direct;
	direct.oser.version = static_cast<ESerializationVersion>(1163);
	EXPECT_THROW(direct.oser & update, std::runtime_error);
	EXPECT_TRUE(direct.extractBuffer().empty());
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0); packet.changedStacks.push_back(update);
	CMemorySerializer outer;
	outer.oser.version = static_cast<ESerializationVersion>(1163);
	EXPECT_THROW(outer.oser & packet, std::runtime_error);
	EXPECT_TRUE(outer.extractBuffer().empty());
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::NEW_HORIZONS_CONSECRATED_ARMS;
	current.iser.version = current.oser.version;
	current.oser & update;
	UnitChanges decoded;
	current.iser & decoded;
	EXPECT_EQ(decoded.data, update.data);
}
TEST(UnitShieldStateTest, BinaryStackDoesNotPretendToPersistActiveShieldWithoutHealth)
{
	CStack descriptor;
	descriptor.redemptionShield.initialize(1000);
	descriptor.redemptionShield.remaining = 73;
	CMemorySerializer bytes;
	EXPECT_THROW(bytes.oser & descriptor, std::runtime_error);
	EXPECT_TRUE(bytes.extractBuffer().empty());
}
TEST_F(ConsecratedShieldUnitTest, GuardianSnapshotRetainsGrantCapacityAndHidesExpiredMarker)
{
	auto marker = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT,
		BonusSource::SPELL_EFFECT, 100, BonusSourceID());
	marker->turnsRemain = 3;
	bonuses.addNewBonus(marker);
	unit.guardianSpiritHitPoints = 50; unit.guardianSpiritRoundsRemaining = 3;
	const auto shown = shieldSnapshot(unit);
	EXPECT_EQ(shown.pools[0].remaining, 50);
	EXPECT_EQ(shown.pools[0].capacity, 100);
	marker->turnsRemain = 0;
	EXPECT_EQ(shieldSnapshot(unit).pools[0].remaining, 0);
}

TEST_F(ConsecratedShieldUnitTest, LancePrimaryBypassesAbsorptionButSecondaryDoesNotAndBothKeepVow)
{
	redemption();
	unit.redemptionShield.remaining = 50;
	unit.health.addTemporaryHitPoints(50);
	auto secondary = unit.acquireState();
	auto primaryQuota = VowCasualtyQuota::capture(7, 100, 6000);
	DamageContext primary{DamageClass::MAGICAL, DamageOrigin::HERO_SPELL, &primaryQuota};
	primary.bypassMagicalAbsorption = true;
	const auto preview = previewShieldDamage(unit, 10000, DamageProvenance::SPELL, &primary);
	ASSERT_TRUE(preview);
	EXPECT_EQ(preview->appliedDamage, 609);
	int64_t damage = 10000;
	unit.damageWithContext(damage, false, DamageProvenance::SPELL, primary);
	EXPECT_EQ(unit.getCount(), 40);
	EXPECT_EQ(unit.health.getTemporaryHitPoints(), 50);
	EXPECT_EQ(unit.redemptionShield.remaining, 50);
	auto secondaryQuota = VowCasualtyQuota::capture(7, 100, 6000);
	DamageContext spill{DamageClass::MAGICAL, DamageOrigin::HERO_SPELL, &secondaryQuota};
	damage = 10000;
	secondary->damageWithContext(damage, false, DamageProvenance::SPELL, spill);
	EXPECT_EQ(secondary->getCount(), 40);
	EXPECT_EQ(secondary->health.getTemporaryHitPoints(), 0);
	EXPECT_EQ(secondary->redemptionShield.remaining, 0);
}
TEST_F(ConsecratedShieldUnitTest, AbsorptionBypassCannotBeGrantedToAnUnclassifiedProducer)
{
	DamageContext malformed;
	malformed.bypassMagicalAbsorption = true;
	int64_t damage = 10;
	EXPECT_THROW(unit.damageWithContext(damage, false, DamageProvenance::OTHER, malformed), std::logic_error);
	EXPECT_THROW(previewShieldDamage(unit, damage, DamageProvenance::OTHER, &malformed), std::logic_error);
	EXPECT_EQ(unit.getAvailableHealth(), 1000);
}
TEST_F(ConsecratedShieldUnitTest, ExhaustedActiveRedemptionRetainsCapturedPresentationDenominator)
{
	redemption();
	unit.redemptionShield.remaining = 10;
	int64_t damage = 10;
	unit.damage(damage, false, DamageProvenance::SPELL);
	EXPECT_EQ(shieldSnapshot(unit).pools[1].remaining, 0);
	EXPECT_EQ(shieldSnapshot(unit).pools[1].capacity, 200);
}
TEST_F(ConsecratedShieldUnitTest, PhysicalPoisonKeepsImmutableProducerAfterSourceRetirementAndStateCopy)
{
	unit.physicalPoisonBaseDamage = 10;
	unit.physicalPoisonActivationsRemaining = 3;
	unit.physicalPoisonSourceStackId = 99;
	unit.physicalPoisonDescriptor = creatureAbility::nativeAffliction(creatureAbility::Native::PHYSICAL_POISON);
	const auto expected = unit.physicalPoisonDescriptor;
	auto copy = unit.acquireState();
	EXPECT_EQ(copy->physicalPoisonDescriptor, expected);
	const auto encoded = copy->save();
	copy->load(encoded);
	ASSERT_TRUE(copy->physicalPoisonDescriptor);
	EXPECT_EQ(*copy->physicalPoisonDescriptor, *expected);
	EXPECT_EQ(copy->physicalPoisonSourceStackId, 99); // No actor lookup required on decode.
	EXPECT_TRUE(hasConsecratedUnitState(encoded));
	UnitChanges update(7, UnitChanges::EOperation::UPDATE); update.data = encoded;
	CMemorySerializer old;
	old.oser.version = static_cast<ESerializationVersion>(1163);
	EXPECT_THROW(old.oser & update, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}
TEST_F(ConsecratedShieldUnitTest, PoisonDescriptorWithoutPayloadRefusesBeforeLiveStateMutation)
{
	unit.physicalPoisonBaseDamage = 10; unit.physicalPoisonActivationsRemaining = 3;
	unit.physicalPoisonDescriptor = creatureAbility::nativeAffliction(creatureAbility::Native::PHYSICAL_POISON);
	auto malformed = unit.save();
	malformed["state"]["physicalPoisonBaseDamage"].Integer() = 0;
	EXPECT_THROW(unit.load(malformed), std::exception);
	EXPECT_EQ(unit.physicalPoisonBaseDamage, 10);
	ASSERT_TRUE(unit.physicalPoisonDescriptor);
}

TEST_F(ConsecratedShieldUnitTest, OwnedSnapshotFreezesDescriptorAliasAndRestoresShieldWithoutOwnerReplacement)
{
	redemption();
	unit.redemptionShield.remaining = 73;
	unit.completedActionThisRound = true;
	unit.physicalPoisonBaseDamage = 10; unit.physicalPoisonActivationsRemaining = 3;
	auto mutableProducer = std::make_shared<CreatureAbilityDescriptor>(
		*creatureAbility::nativeAffliction(creatureAbility::Native::PHYSICAL_POISON));
	unit.physicalPoisonDescriptor = mutableProducer;
	const auto snapshot = UnitStateValueSnapshot::capture(unit);
	mutableProducer->family = creatureAbility::Family::SKILL;
	unit.redemptionShield.remaining = 0;
	unit.completedActionThisRound = false;
	snapshot.restore(unit);
	EXPECT_EQ(unit.redemptionShield.remaining, 73);
	EXPECT_TRUE(unit.hasCompletedActionThisRound());
	ASSERT_TRUE(unit.physicalPoisonDescriptor);
	EXPECT_EQ(unit.physicalPoisonDescriptor->family, creatureAbility::Family::CREATURE_ABILITY);
	EXPECT_NE(unit.physicalPoisonDescriptor.get(), mutableProducer.get());
	EXPECT_EQ(unit.unitId(), 7u);
	EXPECT_EQ(unit.getAvailableHealth(), 1000);
}

}
