/*
 * NewHorizonsIncomingElementDamageTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../SpellPointTestUtils.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include <vcmi/Environment.h>
#include <limits>

namespace
{
SpellID spellFromKey(const char * key)
{
	return SpellID(SpellID::decode(key));
}

std::shared_ptr<Bonus> incomingElementDamage(int32_t percent, SpellDamageElement element)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED,
		BonusSource::OTHER, percent, BonusSourceID(),
		BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(element))));
}

std::shared_ptr<Bonus> generalSpellReduction(int32_t percent)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, percent, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
}

class NewHorizonsIncomingElementDamageTest : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepareBattle()
	{
		startGame();
		startBattle();
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
		ASSERT_NE(target, nullptr);
		beginCombat();
	}

	int64_t adjust(const CSpell * spell, int64_t rawDamage, int finalMultiplier = 100) const
	{
		return spell->adjustRawDamage(nullptr, target, rawDamage, 0, 0, finalMultiplier,
			false, false, false);
	}
};
}

TEST_F(NewHorizonsIncomingElementDamageTest, MatchingModifiersAddAndApplyOnceAfterOrdinaryReduction)
{
	prepareBattle();
	const auto * fireball = spellFromKey("core:fireball").toSpell();
	ASSERT_NE(fireball, nullptr);
	ASSERT_EQ(fireball->getDamageElement(), SpellDamageElement::FIRE);

	target->addNewBonus(generalSpellReduction(20));
	target->addNewBonus(incomingElementDamage(-20, SpellDamageElement::FIRE));
	target->addNewBonus(incomingElementDamage(-30, SpellDamageElement::FIRE));
	target->addNewBonus(incomingElementDamage(90, SpellDamageElement::WATER));

	EXPECT_EQ(adjust(fireball, 101), 40)
		<< "The 20% ordinary defense runs before the summed -50% Fire modifier";
	EXPECT_EQ(adjust(fireball, 3), 1)
		<< "Apply the summed modifier once after defense: floor(floor(3*80%)*50%) = 1";
}

TEST_F(NewHorizonsIncomingElementDamageTest, OnlyTheAuthoredDamageElementMatches)
{
	prepareBattle();
	const auto * fireball = spellFromKey("core:fireball").toSpell();
	const auto * iceBolt = spellFromKey("core:iceBolt").toSpell();
	const auto * implosion = spellFromKey("core:implosion").toSpell();
	ASSERT_NE(fireball, nullptr);
	ASSERT_NE(iceBolt, nullptr);
	ASSERT_NE(implosion, nullptr);
	ASSERT_EQ(fireball->getDamageElement(), SpellDamageElement::FIRE);
	ASSERT_EQ(iceBolt->getDamageElement(), SpellDamageElement::WATER);
	ASSERT_EQ(implosion->getDamageElement(), SpellDamageElement::NONE);

	EXPECT_EQ(adjust(fireball, 101), 101) << "No matching incoming modifier leaves damage unchanged";
	target->addNewBonus(incomingElementDamage(25, SpellDamageElement::WATER));
	EXPECT_EQ(adjust(fireball, 101), 101) << "Water does not modify tagged Fire damage";
	target->addNewBonus(incomingElementDamage(-50, SpellDamageElement::FIRE));
	EXPECT_EQ(adjust(fireball, 101), 50) << "Only the matching Fire modifier now applies";
	EXPECT_EQ(adjust(iceBolt, 101), 126) << "The +25% Water modifier applies with one integer floor";
	EXPECT_EQ(adjust(iceBolt, 3), 3) << "A 125% multiplier floors 3.75 once to 3";
	EXPECT_EQ(adjust(implosion, 101), 101)
		<< "An Earth-school spell with no explicit damage tag is not treated as an element";
}

TEST_F(NewHorizonsIncomingElementDamageTest, NonnegativeMultiplierAndWideScalingAreBounded)
{
	prepareBattle();
	const auto * fireball = spellFromKey("core:fireball").toSpell();
	ASSERT_NE(fireball, nullptr);
	target->addNewBonus(incomingElementDamage(-150, SpellDamageElement::FIRE));
	EXPECT_EQ(adjust(fireball, 101), 0) << "Damage multipliers cannot become negative";

	target->addNewBonus(incomingElementDamage(std::numeric_limits<int32_t>::max(), SpellDamageElement::FIRE));
	EXPECT_EQ(adjust(fireball, std::numeric_limits<int64_t>::max()), std::numeric_limits<int64_t>::max())
		<< "Large positive modifiers saturate instead of overflowing signed damage arithmetic";
}

TEST_F(NewHorizonsIncomingElementDamageTest, AcceptedTaggedSpellCastAppliesIncomingModifierExactlyOnce)
{
	startGame();
	const SpellID fireballId = spellFromKey("core:fireball");
	const auto * fireball = fireballId.toSpell();
	ASSERT_NE(fireball, nullptr);
	ASSERT_EQ(fireball->getDamageElement(), SpellDamageElement::FIRE);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(fireballId);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 23, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 1000);

	startBattle();
	target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1000);
	ASSERT_NE(target, nullptr);
	target->addNewBonus(incomingElementDamage(-50, SpellDamageElement::FIRE));
	beginCombat();

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, fireball);
	const auto mechanics = fireball->battleMechanics(&cast);
	const int64_t rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);
	const int64_t expectedDamage = rawDamage / 2;
	EXPECT_EQ(mechanics->adjustEffectValue(target), expectedDamage);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = fireballId;
	action.aimToUnit(target);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));

	const auto casts = server.castsOf(fireballId);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_EQ(casts.front().damage, expectedDamage)
		<< "The actual spell path applies the target modifier once, not once in forecast and again in resolution";
}

TEST(NewHorizonsIncomingElementDamageWireTest, NewSubtypeRoundTripsAndCannotBeSilentlyDownsaved)
{
	Bonus incoming(BonusDuration::PERMANENT, BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED,
		BonusSource::OTHER, -50, BonusSourceID(),
		BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(SpellDamageElement::FIRE))));
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & incoming);
	Bonus restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.type, BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED);
	EXPECT_EQ(restored.val, -50);
	EXPECT_EQ(restored.subtype.getNum(), static_cast<int32_t>(SpellDamageElement::FIRE));

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_HERO_ACTION_SEQUENCE;
	EXPECT_THROW(oldWriter.oser & incoming, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
}
