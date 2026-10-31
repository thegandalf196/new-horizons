/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/GameLibrary.h"
#include <stdexcept>
namespace
{
using Resolver = battle::OwnedUnitCreatureResolver;
Resolver::Creature prototype(int id)
{
	Resolver::Creature value;
	value.id = CreatureID(id); value.faction = FactionID(2); value.level = 3;
	value.key = "owned:prospective" + std::to_string(id);
	value.pluralTextID = "owned.plural"; value.singularTextID = "owned.singular";
	return value;
}
struct WithoutLibrary
{
	GameLibrary * saved = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = saved; }
};
}
TEST(OwnedProspectiveCreatureMetadataTest, CopiedHpAndAiValueRemainIndependentAfterSourceAndLibraryRetire)
{
	CBonusSystemNode sentinel(BonusNodeType::UNKNOWN, true);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	const auto tree = sentinel.getTreeVersion();
	auto input = prototype(7); input.baseHitPoints = 13; input.aiValue = 29;
	Resolver resolver({input});
	input.baseHitPoints = 99; input.aiValue = 101;
	const auto * creature = resolver.creature(CreatureID(7));
	{
		WithoutLibrary absent;
		EXPECT_EQ(creature->getBaseHitPoints(), 13);
		EXPECT_EQ(creature->getAIValue(), 29);
		EXPECT_EQ(creature->getId(), CreatureID(7));
		EXPECT_EQ(creature->getFactionID(), FactionID(2));
		EXPECT_EQ(resolver.decode("owned:prospective7"), 7);
		EXPECT_THROW(creature->getBaseAttack(), std::logic_error);
		EXPECT_THROW(creature->getBonusBearer(), std::logic_error);
	}
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(sentinel.getTreeVersion(), tree);
}
TEST(OwnedProspectiveCreatureMetadataTest, MissingFactsStayUnknownAndDoNotBorrowAnotherPrototypeOrLibrary)
{
	auto hpOnly = prototype(1); hpOnly.baseHitPoints = 12;
	auto aiOnly = prototype(2); aiOnly.aiValue = 17;
	Resolver resolver({hpOnly, aiOnly, prototype(3)});
	WithoutLibrary absent;
	EXPECT_EQ(resolver.creature(CreatureID(1))->getBaseHitPoints(), 12);
	EXPECT_THROW(resolver.creature(CreatureID(1))->getAIValue(), std::logic_error);
	EXPECT_EQ(resolver.creature(CreatureID(2))->getAIValue(), 17);
	EXPECT_THROW(resolver.creature(CreatureID(2))->getBaseHitPoints(), std::logic_error);
	EXPECT_THROW(resolver.creature(CreatureID(3))->getBaseHitPoints(), std::logic_error);
	EXPECT_THROW(resolver.creature(CreatureID(3))->getAIValue(), std::logic_error);
}
TEST(OwnedProspectiveCreatureMetadataTest, CapturedZeroAndSignedPrototypeValuesAreNotReplacedOrClamped)
{
	auto zero = prototype(4); zero.baseHitPoints = 0; zero.aiValue = 0;
	auto signedValue = prototype(5); signedValue.baseHitPoints = -1; signedValue.aiValue = -7;
	Resolver resolver({zero, signedValue});
	WithoutLibrary absent;
	EXPECT_EQ(resolver.creature(CreatureID(4))->getBaseHitPoints(), 0);
	EXPECT_EQ(resolver.creature(CreatureID(4))->getAIValue(), 0);
	EXPECT_EQ(resolver.creature(CreatureID(5))->getBaseHitPoints(), -1);
	EXPECT_EQ(resolver.creature(CreatureID(5))->getAIValue(), -7);
	// Canonical form eligibility/ranking owns <=0 HP exclusion and max(0,AIValue).
	// The descriptor must preserve facts, not invent a gameplay correction here.
}
