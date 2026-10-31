/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/OwnedProspectiveCreatureDomain.h"
#include "../../lib/GameLibrary.h"
#include <stdexcept>
namespace
{
using Domain = battle::OwnedProspectiveCreatureDomain;
using Category = battle::BattleFormCategory;
Domain::Species species(int id, int hp, std::optional<Category> category, bool wide = false)
{
	Domain::Species value;
	value.descriptor.id = CreatureID(id); value.descriptor.faction = FactionID(id);
	value.descriptor.key = "owned:form" + std::to_string(id);
	value.descriptor.baseHitPoints = hp; value.descriptor.aiValue = id * 10;
	value.descriptor.doubleWide = wide;
	value.category.emplace(category);
	return value;
}
struct WithoutLibrary
{
	GameLibrary * saved = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = saved; }
};
}
TEST(OwnedProspectiveCreatureDomainTest, FullPoolPreservesRegistryOrderAndBothFootprintsWithoutNativeResolution)
{
	auto noHealth = species(1, 0, std::nullopt); noHealth.category.reset();
	auto nonpositive = species(3, -1, std::nullopt); nonpositive.category.reset();
	Domain domain({species(9, 10, Category::CORE), noHealth, species(5, 11, Category::CORE, true),
		species(2, 12, Category::ELITE), nonpositive});
	WithoutLibrary absent;
	const auto forms = domain.formsFor(CreatureID(9));
	ASSERT_EQ(forms.size(), 2u);
	EXPECT_EQ(forms[0]->getId(), CreatureID(9));
	EXPECT_EQ(forms[1]->getId(), CreatureID(5));
	EXPECT_FALSE(forms[0]->isDoubleWide()); EXPECT_TRUE(forms[1]->isDoubleWide());
	EXPECT_NE(forms[0]->getFactionID(), forms[1]->getFactionID());
	EXPECT_EQ(forms[1]->getAIValue(), 50);
	// Zero/negative HP bypasses unknown category, matching native getter order.
	std::vector<CreatureID> visited;
	domain.forEach([&](const Creature * value, bool & stop) { visited.push_back(value->getId()); stop = true; });
	ASSERT_EQ(visited.size(), 1u); EXPECT_EQ(visited.front(), CreatureID(9));
}
TEST(OwnedProspectiveCreatureDomainTest, KnownUnmappedCategoryIsDistinctFromUnknownAndNoCandidateIsSilentlyPruned)
{
	auto unknown = species(3, 20, Category::CORE); unknown.category.reset(); unknown.descriptor.baseHitPoints.reset();
	Domain domain({species(7, 10, std::nullopt), species(9, 12, Category::CORE), unknown});
	WithoutLibrary absent;
	EXPECT_TRUE(domain.formsFor(CreatureID(7)).empty());
	EXPECT_THROW(domain.formsFor(CreatureID(3)), std::logic_error);
	EXPECT_THROW(domain.formsFor(CreatureID(9)), std::logic_error);
}
TEST(OwnedProspectiveCreatureDomainTest, BlueprintZeroIsCapturedAndSideBoundWhileMissingIsOnlyUnknown)
{
	Domain domain({species(4, 10, Category::CORE)},
		{{BattleSide::ATTACKER, CreatureID(4), 0}, {BattleSide::DEFENDER, CreatureID(4), 17}});
	WithoutLibrary absent;
	const auto first = domain.blueprintFor(CreatureID(4), BattleSide::ATTACKER);
	const auto other = domain.blueprintFor(CreatureID(4), BattleSide::DEFENDER);
	ASSERT_TRUE(first.has_value()); ASSERT_TRUE(other.has_value());
	EXPECT_EQ(*first, 0u); EXPECT_EQ(*other, 17u);
	EXPECT_FALSE(domain.blueprintFor(CreatureID(99), BattleSide::ATTACKER).has_value());
	EXPECT_EQ(domain.getByName("owned:form4")->getId(), CreatureID(4));
	EXPECT_THROW(domain.getByIndex(99), std::logic_error);
}
