/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsFactionSpellDamage.h"
#include "../../../lib/battle/NewHorizonsShroud.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsMagicalAbilityDamage.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/battles/BattlePrimaryStrikeAftermath.h"

namespace
{
namespace faction = newHorizonsFactionSpellDamage;
constexpr std::string_view SHROUD = "new-horizons:shroudOfMalassa";
constexpr std::string_view REBIRTH = "new-horizons:elementalRebirth";
class NewHorizonsFactionSpellDamageTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		useRevisedOrders = true;
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires shipped New Horizons content";
	}
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void select(std::string_view skill, std::string_view perk)
	{
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed,
				attackerSideHero->getPerkOfferWeightContext());
			for(size_t i = 0; i < offers.size(); ++i)
				if(offers[i].selection.skillId == skill && offers[i].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, i, seed, false);
					return;
				}
		}
		FAIL() << "No legal perk offer: " << perk;
	}
	void prepare(std::string_view hero, std::string_view skill, std::string_view basic,
		std::string_view advanced = {})
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(std::string(hero))));
		const SecondarySkill id(SecondarySkill::decode(std::string(skill)));
		attackerSideHero->setSecSkillLevel(id, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select(skill, basic));
		if(!advanced.empty())
		{
			attackerSideHero->setSecSkillLevel(id, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			ASSERT_NO_FATAL_FAILURE(select(skill, advanced));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits())
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}
	void effect(CStack * unit, const Bonus & bonus)
	{
		SetStackEffect packet;
		packet.battleID = BattleID(0);
		packet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{bonus});
		gameHandler->sendAndApply(packet);
	}
	CStack * summon(BattleSide side, BattleHex position, int count, bool genuine = true)
	{
		battle::UnitInfo info;
		info.id = battle()->battleNextUnitId();
		info.type = creatureByName("core:earthElemental");
		info.count = count;
		info.position = position;
		info.side = side;
		info.summoned = true;
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		info.save(packet.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD).data);
		gameHandler->sendAndApply(packet);
		auto * unit = battle()->getStack(info.id, false);
		if(genuine) effect(unit, faction::rebirthOutputMarker());
		return unit;
	}
	void injure(CStack * unit, int64_t damage, uint32_t flags = 0)
	{
		StacksInjured packet;
		packet.battleID = BattleID(0);
		auto & hit = packet.stacks.emplace_back();
		hit.stackAttacked = unit->unitId();
		hit.damageAmount = damage;
		hit.flags = flags;
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), unit->acquireState());
		gameHandler->sendAndApply(packet);
	}
};
}

TEST_F(NewHorizonsFactionSpellDamageTest, ShadowHexPerVictimAndExposureRefreshAndActivationExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:lorelei", SHROUD,
		"new-horizons:shroudOfMalassa.shadowHex", "new-horizons:shroudOfMalassa.exposedWeakness"));
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	auto * rear = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(82), 10);
	auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(120), 100);
	BattleAttackInfo attack(rear, victim, 0, false);
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(attack));
	EXPECT_EQ(faction::bonusPercent(*battle(), attackerSideHero, victim), 15);
	EXPECT_EQ(faction::bonusPercent(*battle(), attackerSideHero, other), 0);
	EXPECT_EQ(faction::bonusPercent(*battle(), defenderSideHero, victim), 0);
	const auto before = faction::exposedWeakness(*battle(), attack);
	EXPECT_EQ(faction::bonusPercent(*battle(), attackerSideHero, victim), 15);
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), before);
	EXPECT_EQ(faction::bonusPercent(*battle(), attackerSideHero, victim), 35);
	const auto refresh = faction::exposedWeakness(*battle(), attack);
	ASSERT_EQ(refresh.remove.size(), 1u);
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), refresh);
	EXPECT_EQ(faction::bonusPercent(*battle(), attackerSideHero, victim), 35);
	attack.shooting = true;
	EXPECT_TRUE(faction::exposedWeakness(*battle(), attack).add.empty());
	BattleSetActiveStack activation;
	activation.battleID = BattleID(0);
	activation.stack = victim->unitId();
	activation.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(activation);
	EXPECT_EQ(faction::bonusPercent(*battle(), attackerSideHero, victim), 15);
}

TEST_F(NewHorizonsFactionSpellDamageTest, ConduitCountsLivingStacksAndRejectsUnrelatedSummons)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:brissa", REBIRTH,
		"new-horizons:elementalRebirth.elementalConduit"));
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	auto * large = summon(BattleSide::ATTACKER, BattleHex(50), 1000);
	auto * chain = summon(BattleSide::ATTACKER, BattleHex(52), 1);
	summon(BattleSide::ATTACKER, BattleHex(54), 100, false);
	summon(BattleSide::DEFENDER, BattleHex(56), 100);
	EXPECT_EQ(chain->getRebirthOriginalAggregateHP(), 0);
	EXPECT_EQ(faction::livingRebirthCount(*battle(), *attackerSideHero), 2);
	EXPECT_EQ(faction::bonusPercent(*battle(), attackerSideHero, victim), 30);
	injure(large, large->getAvailableHealth());
	EXPECT_EQ(faction::livingRebirthCount(*battle(), *attackerSideHero), 1);
	EXPECT_EQ(faction::bonusPercent(*battle(), attackerSideHero, victim), 15);
}

TEST_F(NewHorizonsFactionSpellDamageTest, CataclysmUsesMaximumCapacityDividesOnceAndAppliesMagicalMitigation)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:brissa", REBIRTH,
		"new-horizons:elementalRebirth.elementalConduit", "new-horizons:elementalRebirth.elementalCataclysm"));
	auto * reborn = summon(BattleSide::ATTACKER, BattleHex(81), 11);
	auto * first = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * second = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(82), 100);
	effect(first, Bonus(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSource::OTHER, 2000, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY)));
	const auto maximum = battle::getMaximumHealth(*reborn);
	injure(reborn, reborn->getMaxHealth() + 3);
	ASSERT_LT(reborn->getCount(), 11);
	const auto beforeFirst = first->getAvailableHealth();
	const auto beforeSecond = second->getAvailableHealth();
	const auto share = faction::cataclysmShare(maximum, 2);
	const auto expectedFirst = newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *first, share);
	const auto expectedSecond = newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *second, share);
	injure(reborn, reborn->getAvailableHealth());
	EXPECT_EQ(beforeFirst - first->getAvailableHealth(), expectedFirst);
	EXPECT_EQ(beforeSecond - second->getAvailableHealth(), expectedSecond);
	EXPECT_LT(expectedFirst, expectedSecond);
	const auto injuryCount = server.injuries.size();
	injure(reborn, 1);
	EXPECT_EQ(server.injuries.size(), injuryCount + 1) << "A repeated dead-unit hit cannot explode twice";
}

TEST_F(NewHorizonsFactionSpellDamageTest, OrdinarySummonsDoNotExplode)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:brissa", REBIRTH,
		"new-horizons:elementalRebirth.elementalConduit", "new-horizons:elementalRebirth.elementalCataclysm"));
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	auto * unrelated = summon(BattleSide::ATTACKER, BattleHex(82), 10, false);
	const auto health = victim->getAvailableHealth();
	injure(unrelated, unrelated->getAvailableHealth());
	EXPECT_EQ(victim->getAvailableHealth(), health);
}

TEST_F(NewHorizonsFactionSpellDamageTest, CurrentMarkerRoundTripKeepsCasterAndRebirthProvenance)
{
	for(const auto & marker : {faction::rebirthOutputMarker(), faction::exposedWeaknessMarker(BattleSide::ATTACKER),
		faction::exposedWeaknessMarker(BattleSide::DEFENDER)})
	{
		CMemorySerializer memory;
		memory.oser & marker;
		Bonus restored;
		memory.iser & restored;
		EXPECT_EQ(restored.duration, marker.duration);
		EXPECT_EQ(restored.stacking, marker.stacking);
		EXPECT_EQ(restored.source, marker.source);
		EXPECT_EQ(restored.sid, marker.sid);
		EXPECT_EQ(faction::isRebirthOutputMarker(&restored), faction::isRebirthOutputMarker(&marker));
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			EXPECT_EQ(faction::isExposedWeakness(&restored, side), faction::isExposedWeakness(&marker, side));
	}
	EXPECT_EQ(faction::cataclysmShare(104, 3), 6);
	EXPECT_EQ(faction::cataclysmShare(4, 1), 0);
	EXPECT_EQ(faction::cataclysmShare(100, 0), 0);
}

TEST_F(NewHorizonsFactionSpellDamageTest, ActualRebirthAndChainPublishPersistentProvenance)
{
	startGame();
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
	const SecondarySkill skill(SecondarySkill::decode(std::string(REBIRTH)));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(select(REBIRTH, "new-horizons:elementalRebirth.elementalConduit"));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(select(REBIRTH, "new-horizons:elementalRebirth.rebirthChain"));
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), 101));
	startBattle();
	CStack * source = nullptr;
	for(const auto * unit : battle()->battleGetAllUnits())
		if(unit->unitSide() == BattleSide::ATTACKER && unit->unitSlot() == SlotID(0))
			source = battle()->getStack(unit->unitId(), false);
	ASSERT_NE(source, nullptr);
	const auto firstId = battle()->battleNextUnitId();
	injure(source, source->getAvailableHealth());
	auto * first = battle()->getStack(firstId, false);
	ASSERT_NE(first, nullptr);
	ASSERT_TRUE(faction::isRebirthOutput(*first));
	EXPECT_GT(first->getRebirthOriginalAggregateHP(), 0);
	EXPECT_EQ(faction::livingRebirthCount(*battle(), *attackerSideHero), 1);
	const auto chainId = battle()->battleNextUnitId();
	injure(first, first->getAvailableHealth());
	auto * chain = battle()->getStack(chainId, false);
	ASSERT_NE(chain, nullptr);
	EXPECT_EQ(chain->getRebirthOriginalAggregateHP(), 0);
	EXPECT_TRUE(faction::isRebirthOutput(*chain));
	EXPECT_EQ(faction::livingRebirthCount(*battle(), *attackerSideHero), 1);
	const auto afterChainId = battle()->battleNextUnitId();
	injure(chain, chain->getAvailableHealth());
	EXPECT_EQ(battle()->battleNextUnitId(), afterChainId);
	EXPECT_EQ(faction::livingRebirthCount(*battle(), *attackerSideHero), 0);
}

TEST_F(NewHorizonsFactionSpellDamageTest, ActualHeroDamageSpellReceivesCurrentPerRecipientFlankingBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:lorelei", SHROUD, "new-horizons:shroudOfMalassa.shadowHex"));
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(50), 10);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	setTestSpellPointTotal(attackerSideHero, 100);
	beginCombat();
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, target));
	const auto ordinary = before - target->getAvailableHealth();
	ASSERT_GT(ordinary, 0);
	advanceRound();
	auto * rear = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(82), 10);
	ASSERT_TRUE(battle()->battleIsShroudFlankingAttack(BattleAttackInfo(rear, target, 0, false)));
	const auto beforeEnhanced = target->getAvailableHealth();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::MAGIC_ARROW, target));
	EXPECT_EQ(beforeEnhanced - target->getAvailableHealth(), ordinary * 115 / 100);
}

TEST_F(NewHorizonsFactionSpellDamageTest, CataclysmInvincibleRecipientKeepsPoolShareWithoutTakingDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:brissa", REBIRTH,
		"new-horizons:elementalRebirth.elementalConduit", "new-horizons:elementalRebirth.elementalCataclysm"));
	auto * reborn = summon(BattleSide::ATTACKER, BattleHex(81), 11);
	auto * invincible = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * ordinary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(82), 100);
	effect(invincible, Bonus(BonusDuration::ONE_BATTLE, BonusType::INVINCIBLE,
		BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(invincible->isInvincible());
	const auto captured = faction::captureCataclysm(*battle(), *reborn);
	ASSERT_TRUE(captured);
	ASSERT_EQ(captured->targets.size(), 2u) << "Invincibility blocks damage, not adjacency or pool participation";
	const auto share = faction::cataclysmShare(captured->maximumAggregateHP, 2);
	ASSERT_GT(share, 0);
	EXPECT_EQ(faction::cataclysmDamage(*battle(), *invincible, share), 0);
	const auto ordinaryDamage = newHorizonsMagicalAbilityDamage::adjustDamage(*battle(), *ordinary, share);
	const auto protectedHealth = invincible->getAvailableHealth();
	const auto ordinaryHealth = ordinary->getAvailableHealth();
	injure(reborn, reborn->getAvailableHealth());
	EXPECT_EQ(invincible->getAvailableHealth(), protectedHealth);
	EXPECT_EQ(ordinaryHealth - ordinary->getAvailableHealth(), ordinaryDamage);
	ASSERT_FALSE(server.injuries.empty());
	for(const auto & hit : server.injuries.back().stacks)
		EXPECT_NE(hit.stackAttacked, invincible->unitId());
}
