/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameSettings.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <tuple>
namespace
{
class RebirthPacketEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RebirthPacketEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class KnownNativeRebirthPacketTest : public HeroCommandFixture
{
protected:
	const battle::Unit * source = nullptr;
	std::unique_ptr<RebirthPacketEnvironment> environment;
	std::shared_ptr<HypotheticBattle> projected;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate New Horizons native profile";
	}
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare()
	{
		startGame();
		attackerSideHero->clearSlots(); defenderSideHero->clearSlots();
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:elementalRebirth")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:pikeman")), 100));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), CreatureID(CreatureID::decode("core:phoenix")), 1));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), CreatureID(CreatureID::decode("core:pikeman")), 100));
		startBattle();
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->unitSide() == BattleSide::DEFENDER && unit->unitSlot() == SlotID(0)) source = unit;
		ASSERT_NE(source, nullptr);
		ASSERT_EQ(source->valOfBonuses(BonusType::REBIRTH), 20);
		environment = std::make_unique<RebirthPacketEnvironment>(gameState());
		projected = std::make_shared<HypotheticBattle>(environment.get(),
			std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
		ASSERT_TRUE(projected->captureElementalRebirthSource(*source));
		ASSERT_TRUE(projected->hasReadyNativeRebirth(source));
	}
	auto randomState()
	{
		auto * concrete = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
		auto * fixture = dynamic_cast<CRandomGenerator *>(getRNG());
		if(!concrete || !fixture) throw std::runtime_error("Expected concrete fixture random generators");
		CMemorySerializer authority, world, local, defaults;
		authority.oser & *concrete; world.oser & *gameHandler->randomizer; local.oser & *fixture;
		defaults.oser & CRandomGenerator::getDefault();
		return std::tuple{authority.extractBuffer(), world.extractBuffer(), local.extractBuffer(), defaults.extractBuffer()};
	}
	void resolve(int mode)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto live = source->acquireState()->save();
		const auto rng = randomState();
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(),
			std::shared_ptr<CBattleInfoCallback>(gameState(), battle()));
		auto target = projected->getForUpdate(source->unitId());
		if(mode == 1)
			target->addUnitBonus(std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
				BonusType::REBIRTH, BonusSource::OTHER, 80, BonusSourceID())});
		if(mode == 3) target->cloned = true;
		BattleStackAttacked hit;
		hit.stackAttacked = target->unitId(); hit.damageAmount = target->getAvailableHealth();
		if(mode == 2)
		{
			// An unresolved forecast deliberately has no accepted native outcome flags.
			auto state = target->acquireState(); int64_t lethal = state->getAvailableHealth(); state->damage(lethal);
			hit.newState.id = target->unitId(); hit.newState.operation = UnitChanges::EOperation::UPDATE;
			hit.newState.healthDelta = -lethal; hit.newState.data = state->save();
			ASSERT_FALSE(hit.killed()); ASSERT_FALSE(hit.willRebirth());
		}
		else
		{
			// Exercise native preparation with the existing deterministic forecast RNG,
			// rather than inventing a KILLED/REBIRTH combination or drawing native RNG.
			CStack::prepareAttacked(hit, *projected->getServerCallback()->getRNG(), target->acquireState());
			ASSERT_TRUE(hit.killed());
			ASSERT_EQ(hit.willRebirth(), mode == 1);
			ASSERT_EQ(hit.cloneKilled(), mode == 3);
		}
		StacksInjured packet; packet.battleID = BattleID(0); packet.stacks.push_back(hit);
		projected->getServerCallback()->apply(packet);
		EXPECT_EQ(projected->battleGetUnitByID(source->unitId())->alive(), mode == 1);
		const auto & spawns = projected->getElementalRebirthSpawnUnitIds();
		EXPECT_EQ(spawns.size(), mode == 0 ? 1u : 0u);
		if(mode == 0)
		{
			const auto * spawned = projected->battleGetUnitByID(*spawns.begin());
			ASSERT_NE(spawned, nullptr); EXPECT_TRUE(spawned->alive()); EXPECT_TRUE(spawned->isSummoned());
			EXPECT_NE(spawned->unitId(), source->unitId());
		}
		EXPECT_EQ(source->acquireState()->save(), live);
		EXPECT_EQ(sibling->battleGetUnitByID(source->unitId())->acquireState()->save(), live);
		EXPECT_TRUE(sibling->getElementalRebirthSpawnUnitIds().empty());
		EXPECT_EQ(randomState(), rng);
	}
};
TEST_F(KnownNativeRebirthPacketTest, AcceptedFailedNativeRebirthAllowsSeparateElementalRebirth) { resolve(0); }
TEST_F(KnownNativeRebirthPacketTest, AcceptedSuccessfulNativeRebirthKeepsSameUnitAndSuppressesElemental) { resolve(1); }
TEST_F(KnownNativeRebirthPacketTest, FlaglessForecastRetainsReadinessFallback) { resolve(2); }
TEST_F(KnownNativeRebirthPacketTest, AcceptedCloneDestructionRemainsIneligibleForElementalRebirth) { resolve(3); }
}
