/*
 * ProjectedUnitEnvironmentFixture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "../mock/mock_battle_IBattleState.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/ProjectedUnitEnvironment.h"
#include "../../lib/battle/ProjectedUnitEnvironmentState.h"
#include "../../lib/battle/LiveUnitEnvironmentContext.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/UnitStateValueFields.h"
#include "../../lib/bonuses/ProjectedBonusGraph.h"
#include "../../lib/bonuses/ProjectedBonusRoster.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <array>
#include <stdexcept>

namespace test::environmentFixture
{
class Source : public BattleStateMock
{
public:
	struct Side
	{
		PlayerColor color;
		const CGHeroInstance * hero = nullptr;
		int rank = 0;
		int cap = 0;
		int speed = 0;
		int retaliations = 0;
		int pain = 0;
		int damage = 0;
		SylvanLuckState fortune;
	};
	BattleSideArray<Side> sides;
	JsonNode rules;
	bool rejectCapture = false;
	PlayerColor getSidePlayer(BattleSide side) const override { return sides.at(side).color; }
	const CGHeroInstance * getSideHero(BattleSide side) const override { return sides.at(side).hero; }
	const JsonNode & getMagicRules() const override { return rules; }
	int32_t getBloodrageRank(BattleSide side) const override { return sides.at(side).rank; }
	int32_t getBloodrageCapPercent(BattleSide side) const override
	{
		if(rejectCapture)
			throw std::runtime_error("synthetic metadata capture refusal");
		return sides.at(side).cap;
	}
	int32_t getBloodrageSpeedBonus(BattleSide side) const override { return sides.at(side).speed; }
	int32_t getBloodrageAdditionalRetaliations(BattleSide side) const override { return sides.at(side).retaliations; }
	int32_t getBloodragePainIncrement(BattleSide side) const override { return sides.at(side).pain; }
	int32_t getBloodrageDamagePercent(BattleSide side) const override { return sides.at(side).damage; }
	SylvanLuckState getSylvanLuckState(BattleSide side) const override { return sides.at(side).fortune; }
};

class Callback final : public CBattleInfoCallback
{
	const IBattleInfo & source;
public:
	explicit Callback(const IBattleInfo & source) : source(source) {}
	const IBattleInfo * getBattle() const override { return &source; }
	std::optional<PlayerColor> getPlayerID() const override { return std::nullopt; }
};

inline std::shared_ptr<Bonus> bonus(BonusType type, int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID());
}

/// Shared concrete unit/graph setup, not a packet/world acceptance harness.
struct Projection
{
	CStackBasicDescriptor regular{CreatureID(0), 8};
	CStackBasicDescriptor cart{CreatureID(CreatureID::AMMO_CART), 1};
	CStackInstance base{nullptr, CreatureID(0), 8, true};
	CStack subject{&regular, PlayerColor(0), 17, BattleSide::ATTACKER};
	CStack firstCart{&cart, PlayerColor(0), 18, BattleSide::ATTACKER};
	CStack secondCart{&cart, PlayerColor(0), 19, BattleSide::ATTACKER};
	CStack supporter{&regular, PlayerColor(0), 20, BattleSide::ATTACKER};
	std::array<CStack *, 4> originals{&subject, &firstCart, &secondCart, &supporter};
	::testing::NiceMock<Source> source;
	Callback callback{source};
	battle::LiveUnitEnvironmentContext liveContext{callback};
	battle::UnitEnvironmentQueries live{liveContext};
	std::unique_ptr<battle::ProjectedUnitEnvironmentState> state;
	ProjectedBonusGraph graph;
	std::unique_ptr<ProjectedBonusRoster> roster;
	std::unique_ptr<battle::ProjectedUnitEnvironment> environment;
	std::array<std::unique_ptr<battle::CUnitStateDetached>, 4> units;

	Projection()
	{
		using namespace ::testing;
		source.sides.at(BattleSide::ATTACKER).color = PlayerColor(0);
		source.sides.at(BattleSide::DEFENDER).color = PlayerColor(1);
		auto & attacker = source.sides.at(BattleSide::ATTACKER);
		attacker.rank = 2;
		attacker.cap = 40;
		attacker.speed = 3;
		attacker.retaliations = 1;
		attacker.pain = 8;
		attacker.damage = 12;
		attacker.fortune.speedUnits.insert(17);
		auto & defender = source.sides.at(BattleSide::DEFENDER);
		defender.rank = 3;
		defender.cap = 60;
		defender.speed = 5;
		defender.retaliations = 2;
		defender.pain = 12;
		source.rules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		for(auto * original : originals)
		{
			auto health = bonus(BonusType::STACK_HEALTH, 20);
			health->source = BonusSource::CREATURE_ABILITY;
			health->sid = BonusSourceID(original->unitType()->getId());
			original->addNewBonus(health);
			original->base = &base;
			original->health.init();
			original->position = BattleHex(12, 8);
		}
		subject.position = BattleHex(5, 5);
		supporter.position = BattleHex(6, 5);
		subject.personalBloodrageIncrement = 8;
		subject.addNewBonus(bonus(BonusType::MAGIC_RESISTANCE, 20));
		supporter.addNewBonus(bonus(BonusType::SPELL_RESISTANCE_AURA, 25));
		ON_CALL(source, getUnitsIf(_)).WillByDefault(Invoke([this](const battle::UnitFilter & predicate)
		{
			battle::Units result;
			for(const auto * unit : originals)
				if(predicate(unit))
					result.push_back(unit);
			return result;
		}));
	}

	void initialize()
	{
		state = std::make_unique<battle::ProjectedUnitEnvironmentState>(battle::ProjectedUnitEnvironmentState::capture(source));
		std::vector<const battle::Unit *> ordered;
		for(size_t index = 0; index < units.size(); ++index)
		{
			units[index] = std::make_unique<battle::CUnitStateDetached>(originals[index], &graph.view(*originals[index]));
			battle::UnitStateValueFields::copy(*units[index], *originals[index]);
			ordered.push_back(units[index].get());
		}
		roster = std::make_unique<ProjectedBonusRoster>(std::move(ordered));
		for(size_t index = 0; index < units.size(); ++index)
			graph.bind(*originals[index], *units[index], *roster);
		environment = std::make_unique<battle::ProjectedUnitEnvironment>(*roster, graph, *state);
		for(auto & unit : units)
			unit->bindStateEnvironment(environment.get());
	}
};
}
