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
#include "../../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../../lib/bonuses/BonusList.h"
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
	CStack * summon(BattleSide side, BattleHex position, int count)
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

TEST_F(NewHorizonsFactionSpellDamageTest, CurrentConfluxCastProfileDoesNotDependOnSummonRoster)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:brissa", REBIRTH,
		"new-horizons:elementalRebirth.elementalConduit", "new-horizons:elementalRebirth.elementalCataclysm"));
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	const auto capture = [&](SpellID spell)
	{
		return newHorizonsMetamagic::captureHeroCastProfile(attackerSideHero, battle(), true, spell);
	};
	EXPECT_EQ(capture(SpellID::MAGIC_ARROW).factionSpellDamageBonus, 15);
	EXPECT_EQ(capture(SpellID::FIREBALL).factionSpellDamageBonus, 40);
	auto * large = summon(BattleSide::ATTACKER, BattleHex(50), 1000);
	summon(BattleSide::ATTACKER, BattleHex(52), 1);
	summon(BattleSide::DEFENDER, BattleHex(56), 100);
	EXPECT_EQ(capture(SpellID::MAGIC_ARROW).factionSpellDamageBonus, 15);
	EXPECT_EQ(capture(SpellID::FIREBALL).factionSpellDamageBonus, 40);
	EXPECT_EQ(faction::bonusPercent(*battle(), attackerSideHero, victim), 0);
	injure(large, large->getAvailableHealth());
	EXPECT_EQ(capture(SpellID::MAGIC_ARROW).factionSpellDamageBonus, 15);
	EXPECT_EQ(capture(SpellID::FIREBALL).factionSpellDamageBonus, 40);
}

TEST_F(NewHorizonsFactionSpellDamageTest, OrdinarySummonsDoNotExplode)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:brissa", REBIRTH,
		"new-horizons:elementalRebirth.elementalConduit", "new-horizons:elementalRebirth.elementalCataclysm"));
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	auto * unrelated = summon(BattleSide::ATTACKER, BattleHex(82), 10);
	const auto health = victim->getAvailableHealth();
	injure(unrelated, unrelated->getAvailableHealth());
	EXPECT_EQ(victim->getAvailableHealth(), health);
}

TEST_F(NewHorizonsFactionSpellDamageTest, CurrentExposureMarkerRoundTripKeepsBothCasterSides)
{
	for(const auto & marker : {faction::exposedWeaknessMarker(BattleSide::ATTACKER),
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
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			EXPECT_EQ(faction::isExposedWeakness(&restored, side), faction::isExposedWeakness(&marker, side));
	}
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
