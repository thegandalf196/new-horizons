/* Part of VCMI; GPL v2 or later, see license.txt. */
#include "StdInc.h"

#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
newHorizonsHeroes::PerkOfferCandidate candidate()
{
	return {{"new-horizons:offense", "new-horizons:offense.pressTheAttack"},
		"Press the Attack", "Placeholder effect description.", 2};
}
}

TEST(NewHorizonsPerkOfferWire, CurrentLevelUpPacketPreservesOrderedServerOfferAndSeed)
{
	HeroLevelUp outgoing;
	outgoing.heroId = ObjectInstanceID(7);
	outgoing.player = PlayerColor(0);
	outgoing.skills = {SecondarySkill::OFFENCE};
	outgoing.perks = {candidate()};
	outgoing.perkOfferSeed = 0xfedcba98ULL;

	CMemorySerializer wire;
	wire.oser & outgoing;
	HeroLevelUp incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.heroId, outgoing.heroId);
	EXPECT_EQ(incoming.skills, outgoing.skills);
	EXPECT_EQ(incoming.perks, outgoing.perks);
	EXPECT_EQ(incoming.perkOfferSeed, outgoing.perkOfferSeed);
}

TEST(NewHorizonsPerkOfferWire, OlderPacketOmitsEmptyOfferAndRejectsInformationLoss)
{
	HeroLevelUp legacy;
	legacy.heroId = ObjectInstanceID(7);
	legacy.player = PlayerColor(0);
	legacy.skills = {SecondarySkill::OFFENCE};

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_PERKS;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_PERKS;
	old.oser & legacy;
	HeroLevelUp decoded;
	decoded.perks = {candidate()};
	decoded.perkOfferSeed = 99;
	old.iser & decoded;
	EXPECT_TRUE(decoded.perks.empty());
	EXPECT_EQ(decoded.perkOfferSeed, 0u);

	legacy.perks = {candidate()};
	CMemorySerializer rejected;
	rejected.oser.version = ESerializationVersion::NEW_HORIZONS_PERKS;
	EXPECT_THROW(rejected.oser & legacy, std::runtime_error);
}

TEST(NewHorizonsPerkOfferWire, ChosenPacketPreservesCanonicalSelection)
{
	HeroPerkChosen outgoing;
	outgoing.hero = ObjectInstanceID(9);
	outgoing.selection = candidate().selection;

	CMemorySerializer wire;
	wire.oser & outgoing;
	HeroPerkChosen incoming;
	wire.iser & incoming;
	EXPECT_EQ(incoming.hero, outgoing.hero);
	EXPECT_EQ(incoming.selection, outgoing.selection);

	const CPackForClient & base = outgoing;
	auto polymorphic = CMemorySerializer::deepCopy(base);
	const auto * registered = dynamic_cast<const HeroPerkChosen *>(polymorphic.get());
	ASSERT_NE(registered, nullptr);
	EXPECT_EQ(registered->hero, outgoing.hero);
	EXPECT_EQ(registered->selection, outgoing.selection);
}
