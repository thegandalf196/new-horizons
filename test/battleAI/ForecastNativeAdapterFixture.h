/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "ProjectedTargetHitFixture.h"
#include "../../lib/ScopeGuard.h"
#include <cstdlib>
#include "../../AI/BattleAI/ForecastNativeAdapter.h"
#include "../../AI/BattleAI/FocusMagicProjection.h"
#include "../../AI/BattleAI/ForecastEffectState.h"
#include "../../AI/BattleAI/SelectedAttackReceipt.h"
#include "../../AI/BattleAI/VisibleCandidateWorld.h"
#include "../../AI/BattleAI/SearchBudget.h"
#include "../../lib/battle/FocusMagicValueState.h"
#include "../../AI/BattleAI/ForecastPerkPolicy.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapObjects/CGCreature.h"
#include "../../lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "../../server/battles/BattleProcessor.h"
namespace forecastNativeAdapterTest
{
class ForecastNativeAdapterTest : public TinyMapGameTest
{
protected:
	RecordingGameServer server;
	std::shared_ptr<CGameHandler> handler;
	std::unique_ptr<projectedHitTest::Environment> environment;
	std::shared_ptr<CBattleInfoCallback> visible;
	std::shared_ptr<HypotheticBattle> parent;
	Services * gameServices() override { return LIBRARY; }
	// Keep random starting artifacts out of these controlled battle rosters.
	void configurePlayer(PlayerSettings & settings) const override { settings.bonus = PlayerStartingBonus::GOLD; }
	void TearDown() override
	{
		parent.reset(); visible.reset(); environment.reset(); handler.reset();
		server.gameState.reset(); TinyMapGameTest::TearDown();
	}
	std::shared_ptr<CArmedInstance> army(PlayerColor owner, const char * name, const char * creature = "core:pikeman", int32_t count = 100, bool ammoCart = false)
	{
		const CreatureID body(CreatureID::decode(creature));
		auto value = std::make_shared<CGCreature>(gameState().get());
		value->ID = Obj::MONSTER;
		value->subID = MapObjectSubID(body.getNum());
		value->tempOwner = owner;
		value->pos = int3(4, 4, 0);
		// Saving a registered map army also saves its template's object subtype.
		// A default ObjectTemplate has NO_OBJ/0 and cannot be serialized.
		const auto templates = value->getObjectHandler()->getTemplates();
		if(templates.empty()) throw std::runtime_error("Forecast army needs a registered creature template");
		value->appearance = templates.front();
		value->instanceName = name;
		value->setCreature(SlotID(0), body, count);
		if(ammoCart) value->setCreature(SlotID(1), CreatureID::AMMO_CART, 1);
		map()->addNewObject(value);
		value->attachToBonusSystem(*gameState());
		return value;
	}
	void prepare(bool conflux = false, bool oversized = false, bool selectedStatic = false,
		const char * ordinaryAttacker = "core:pikeman", bool ammoCart = false, bool commandHero = false, const char * heroOverride = nullptr, bool splitEnemy = false, bool neutralDefender = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("Compact native forecast")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({15, 15, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
			.town({25, 25, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({});
		const bool withHero = conflux || oversized || commandHero || heroOverride;
		if(withHero)
			builder.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(heroOverride ? heroOverride : oversized ? "core:neela" : "core:aenain")), PlayerColor(0)).heroGarrison({
				{CreatureID(CreatureID::decode(oversized ? "core:gremlin" : "core:pikeman")), static_cast<uint16_t>(oversized ? 1000 : 14)},
				{CreatureID(CreatureID::decode("core:airElemental")), 2},
				{CreatureID(CreatureID::decode("core:waterElemental")), 2}});
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder), EMapDifficulty::NORMAL));
		server.gameState = gameState();
		handler = std::make_shared<CGameHandler>(server, gameState());
		auto * hero = withHero ? findHeroByOwner(PlayerColor(0)) : nullptr;
		if(withHero) { ASSERT_NE(hero, nullptr); ASSERT_TRUE(hero->getPerkState().getSelected().empty()); }
		if(conflux)
		{
			// SOD encodes only its original creature range. Runtime mod bodies
			// must be installed after map load, before the actual native battle.
			hero->clearSlots();
			hero->setCreature(SlotID(0), CreatureID(CreatureID::decode("new-horizons:wisp")), 14);
			hero->setCreature(SlotID(1), CreatureID(CreatureID::decode("core:airElemental")), 2);
			hero->setCreature(SlotID(2), CreatureID(CreatureID::decode("core:waterElemental")), 2);
		}
		if(commandHero)
			hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:command")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(selectedStatic)
		{
			ASSERT_NE(hero, nullptr);
			const auto grant = [&](const std::string & skill, MasteryLevel::Type rank, const std::string & perk)
			{
				hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), rank, ChangeValueMode::ABSOLUTE);
				EXPECT_TRUE(hero->getPerkState().canAcquirePerk(skill, perk, rank));
				hero->applyPerkSelection({skill, perk});
				EXPECT_TRUE(hero->hasActivePerk(skill, perk));
			};
			grant("new-horizons:authority", MasteryLevel::BASIC, "new-horizons:authority.massedRanks");
			grant("new-horizons:offense", MasteryLevel::BASIC, "new-horizons:offense.shockAssault");
			grant("new-horizons:offense", MasteryLevel::ADVANCED, "new-horizons:offense.armorPiercer");
			grant("new-horizons:sorceryMagic", MasteryLevel::BASIC, "new-horizons:sorceryMagic.overcharger");
			grant("new-horizons:sorceryMagic", MasteryLevel::ADVANCED, "new-horizons:sorceryMagic.countermage");
			grant("new-horizons:metamagic", MasteryLevel::BASIC, "new-horizons:metamagic.conservedFormula");
			if(conflux) grant("new-horizons:elementalRebirth", MasteryLevel::BASIC, "new-horizons:elementalRebirth.elementalConduit");
			ASSERT_TRUE(forecast::ordinaryPerksSupported(*hero));
		}
		auto left = withHero ? std::shared_ptr<CArmedInstance>{} : army(PlayerColor(0), "compactLeft", ordinaryAttacker, 100, ammoCart);
		const auto defenderOwner = neutralDefender ? PlayerColor::NEUTRAL : PlayerColor(1);
		auto right = army(defenderOwner, "compactRight", conflux ? "core:gnoll" : "core:pikeman", conflux ? 24 : 100);
		if(splitEnemy) right->setCreature(SlotID(1), CreatureID(CreatureID::decode("core:pikeman")), 1000);
		BattleLayout layout;
		layout.units[BattleSide::ATTACKER][0] = BattleHex(oversized ? 2 : 6, 5);
		layout.units[BattleSide::ATTACKER][1] = BattleHex(6, 3);
		layout.units[BattleSide::ATTACKER][2] = BattleHex(6, 7);
		layout.units[BattleSide::DEFENDER][0] = BattleHex(oversized ? 13 : 7, 5);
		if(splitEnemy) layout.units[BattleSide::DEFENDER][1] = BattleHex(11, 5);
		layout.obstaclesAllowed = false; layout.tacticsAllowed = false;
		handler->battles->startBattle(hero ? static_cast<CArmedInstance *>(hero) : left.get(), right.get(), int3(4, 4, 0), hero, nullptr, layout, nullptr);
		const auto * battle = gameState()->getBattle(BattleID(0));
		ASSERT_NE(battle, nullptr);
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		visible = std::make_shared<CPlayerBattleCallback>(battle, battle->getSidePlayer(BattleSide::ATTACKER));
		parent = std::make_shared<HypotheticBattle>(environment.get(), visible);
		if(commandHero && parent->battleGetRound() == 0) parent->nextRound();
	}
};
}

using forecastNativeAdapterTest::ForecastNativeAdapterTest;
