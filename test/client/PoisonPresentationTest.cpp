/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/battle/PoisonPresentation.h"

TEST(PoisonPresentation, AcceptedOrdinaryCastDoesNotInheritEarlierStrongPoison)
{
	EXPECT_TRUE(poisonPresentation::acceptedCastOverride(SpellID::POISON, false).empty());
}

TEST(PoisonPresentation, AcceptedStrongerPoisonSelectsRedAlias)
{
	EXPECT_EQ(poisonPresentation::acceptedCastOverride(SpellID::POISON, true),
		AnimationPath::builtin("NH_broghild_poison_red"));
	EXPECT_TRUE(poisonPresentation::acceptedCastOverride(SpellID::MAGIC_ARROW, true).empty());
}

TEST(PoisonPresentation, OverrideChangesOnlyCanonicalPoisonCloud)
{
	const auto red = AnimationPath::builtin("NH_broghild_poison_red");
	const auto ordinary = AnimationPath::builtin("SP11_.DEF");
	EXPECT_EQ(poisonPresentation::queueResource(SpellID::POISON, ordinary, red), red);
	EXPECT_EQ(poisonPresentation::queueResource(SpellID::MAGIC_ARROW, ordinary, red), ordinary);
	const auto other = AnimationPath::builtin("SP01_");
	EXPECT_EQ(poisonPresentation::queueResource(SpellID::POISON, other, red), other);
}

TEST(PoisonPresentation, MissingOrUnrecognizedAliasPreservesOrdinaryResource)
{
	const auto ordinary = AnimationPath::builtin("SP11_");
	EXPECT_EQ(poisonPresentation::queueResource(SpellID::POISON, ordinary, {}), ordinary);
	EXPECT_EQ(poisonPresentation::queueResource(SpellID::POISON, ordinary,
		AnimationPath::builtin("unrelatedEffect")), ordinary);
}
