/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/StackFormSourceIdentity.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
namespace
{
std::unique_ptr<CStack> source(CreatureID creature)
{
	CStackBasicDescriptor descriptor(creature, 3);
	auto result = std::make_unique<CStack>(&descriptor, PlayerColor(0), 17, BattleSide::ATTACKER, SlotID(0), true);
	result->attachToSource(*creature.toCreature()); result->health.init();
	return result;
}
}
TEST(StackFormSourceIdentityTest, OrdinarySourceIsExactMetadataWithoutCreatingAFormSource)
{
	const CreatureID original(CreatureID::decode("core:peasant")); auto stack = source(original);
	const auto generation = PreparedBonusGraphStorage::currentGeneration(); const auto version = stack->getTreeVersion();
	const auto receipt = battle::stackFormSourceIdentity(*stack);
	EXPECT_EQ(receipt.baseCreature, original); EXPECT_EQ(receipt.attached, nullptr);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation); EXPECT_EQ(stack->getTreeVersion(), version);
}
TEST(StackFormSourceIdentityTest, FormedSourceRetainsBaseAndExactAttachedNodeWithoutInvalidation)
{
	const CreatureID original(CreatureID::decode("core:peasant")), alternate(CreatureID::decode("core:imp"));
	ASSERT_NE(original, alternate); auto stack = source(original); stack->beginBattleForm(alternate, 2);
	ASSERT_EQ(stack->unitType()->getId(), alternate);
	const auto selector = Selector::type()(BonusType::STACK_HEALTH);
	const auto cached = stack->getAllBonuses(selector, "stack-source-identity-sentinel");
	const auto generation = PreparedBonusGraphStorage::currentGeneration(); const auto version = stack->getTreeVersion();
	const auto receipt = battle::stackFormSourceIdentity(*stack);
	EXPECT_EQ(receipt.baseCreature, original); EXPECT_EQ(receipt.attached, alternate.toCreature());
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation); EXPECT_EQ(stack->getTreeVersion(), version);
	EXPECT_EQ(stack->getAllBonuses(selector, "stack-source-identity-sentinel"), cached);
	stack->endBattleForm();
	const auto restored = battle::stackFormSourceIdentity(*stack);
	EXPECT_EQ(restored.baseCreature, original); EXPECT_EQ(restored.attached, nullptr);
}
