/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSpecialtyProcFixture.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/battle/NewHorizonsOrdinaryStrike.h"
namespace
{
class NewHorizonsOrdinaryStrikeSpecialtyTest : public NewHorizonsSpecialtyProcFixture {};
TEST_F(NewHorizonsOrdinaryStrikeSpecialtyTest, PerfectMountDoesNotCreateTheRetiredInitiativeRider)
{
	prepare("core:aeris", "core:silverPegasus");
	noMorale(*actor); noMorale(*enemy); blockRetaliation(actor);
	const int initiative = enemy->getInitiative(0);
	BattleAttackInfo strike(actor, enemy, 0, false);
	EXPECT_FALSE(newHorizonsHeroes::ordinaryCreatureStrikeBonus(*battle(), strike));
	beginCombat();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(actor, enemy, actor->getPosition())));
	EXPECT_EQ(enemy->getInitiative(0), initiative);
	EXPECT_FALSE(enemy->hasBonus(CSelector([](const Bonus * bonus)
	{ return bonus && bonus->statusIdentity == "wingbeat"; })));
	EXPECT_TRUE(actor->hasBonusOfType(BonusType::MAGICAL_DAMAGE_IMMUNITY));
}
TEST_F(NewHorizonsOrdinaryStrikeSpecialtyTest, NullifyingShotDoesNotCreateTheRetiredDirectMdrStrip)
{
	prepare("core:theodorus", "core:archMage");
	noMorale(*actor); noMorale(*enemy);
	enemy->setPosition(BattleHex(12, 5));
	enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 50, BonusSourceID()));
	BattleAttackInfo strike(actor, enemy, 0, true);
	EXPECT_FALSE(newHorizonsHeroes::ordinaryCreatureStrikeBonus(*battle(), strike));
	beginCombat();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, enemy)));
	EXPECT_EQ(enemy->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION), 50);
	EXPECT_FALSE(enemy->hasBonusOfType(BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION));
}
class NewHorizonsStrikeCaptureTest : public NewHorizonsSpecialtyProcFixture, public ::testing::WithParamInterface<bool> {};
TEST_P(NewHorizonsStrikeCaptureTest, CurrentCapturedPolicyAndStartingDevelopmentSurviveWorldRoundtrip)
{
	prepare(GetParam() ? "core:theodorus" : "core:aeris", GetParam() ? "core:archMage" : "core:silverPegasus");
	const auto id = attackerSideHero->id;
	CGameState restored; restored.preInit(LIBRARY); restored.loadFromMemory(gameState()->saveToMemory());
	const auto * hero = restored.getHero(id);
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), attackerSideHero->getPrimaryGrowthRules());
	EXPECT_EQ(hero->secSkills, attackerSideHero->secSkills);
	EXPECT_EQ(hero->getSpecialtyPresentation().name, GetParam() ? "Nullifying Shot" : "Perfect Mount");
}
INSTANTIATE_TEST_SUITE_P(CurrentNativePolicies, NewHorizonsStrikeCaptureTest, ::testing::Bool());
TEST(OrdinaryStrikeBonusSerialization, NewTargetMdrEnumRejects1144BeforeDirectAndPacketPrefixes)
{
	Bonus effect(BonusDuration::STACK_GETS_TURN, BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION,
		BonusSource::OTHER, 20, BonusSourceID());
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	EXPECT_THROW(old.oser & effect, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(7, std::vector<Bonus>{effect});
	CMemorySerializer oldPacket;
	oldPacket.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	EXPECT_THROW(packet.serialize(oldPacket.oser), std::runtime_error);
	EXPECT_TRUE(oldPacket.extractBuffer().empty());
	CMemorySerializer current;
	EXPECT_NO_THROW(current.oser & effect);
	Bonus restored;
	EXPECT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.toJsonNode(), effect.toJsonNode());
}
TEST(OrdinaryStrikeBonusSerialization, Legacy1144PacketRetainsExactFieldOrderAndRoundtrip)
{
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(7, std::vector<Bonus>{Bonus(BonusDuration::STACK_GETS_TURN,
		BonusType::STACKS_INITIATIVE, BonusSource::OTHER, -15, BonusSourceID())});
	CMemorySerializer actual;
	actual.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	ASSERT_NO_THROW(packet.serialize(actual.oser));
	CMemorySerializer original;
	original.oser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	original.oser & packet.battleID;
	original.oser & packet.toAdd;
	original.oser & packet.toUpdate;
	original.oser & packet.toRemove;
	original.oser & packet.exchange;
	const auto bytes = actual.extractBuffer();
	EXPECT_EQ(bytes, original.extractBuffer());
	CMemorySerializer reader(bytes);
	reader.iser.version = ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES;
	SetStackEffect restored;
	ASSERT_NO_THROW(restored.serialize(reader.iser));
	ASSERT_EQ(restored.toAdd.size(), 1);
	ASSERT_EQ(restored.toAdd.front().second.size(), 1);
	EXPECT_EQ(restored.toAdd.front().second.front().toJsonNode(), packet.toAdd.front().second.front().toJsonNode());
}

}
