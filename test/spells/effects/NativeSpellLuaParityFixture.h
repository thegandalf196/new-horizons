/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "EffectFixture.h"
#include "../../../luascript/LuaModule.h"
#include "../../../luascript/LuaScriptFactory.h"
#include "../../../luascript/LuaScriptPool.h"
#include "../../../lib/spells/effects/native/NativeSpellFactory.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/filesystem/Filesystem.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../mock/mock_UnitEnvironment.h"
#include <random>
#include <stdexcept>

namespace test
{
/// Both implementations receive the same immutable starting world. Each attack
/// receives a fresh production CUnitState, so packet payloads include complete
/// resulting unit state without letting the first run mutate the second run.
class NativeSpellLuaParity : public Test, public EffectFixture
{
protected:
	struct Trace
	{
		std::vector<std::pair<std::string, std::vector<std::byte>>> packets;
		std::vector<std::array<int64_t, 3>> random;
		std::vector<std::string> bonusOrder;
		bool operator==(const Trace &) const = default;
	};
	scripting::LuaModule luaModule;
	scripting::LuaScriptStore luaStore{luaModule};
	scripting::LuaScriptFactory luaFactory{luaStore};
	std::unique_ptr<scripting::LuaScriptPool> luaPool;
	std::shared_ptr<spells::effects::Effect> oracle;
	NiceMock<UnitEnvironmentMock> unitEnvironment;
	Trace trace;
	std::mt19937 engine;

	NativeSpellLuaParity() : EffectFixture("core:damage") {}
	void SetUp() override
	{
		EffectFixture::setUp();
		luaPool = std::make_unique<scripting::LuaScriptPool>(luaModule, &environmentMock);
		EXPECT_CALL(*battleFake, getScriptContextPool()).Times(AnyNumber()).WillRepeatedly(ReturnRef(*luaPool));
		EXPECT_CALL(*battleFake, getUnitsIf(_)).Times(AnyNumber());
		EXPECT_CALL(mechanicsMock, getHeroCaster()).Times(AnyNumber()).WillRepeatedly(Return(nullptr));
		EXPECT_CALL(mechanicsMock, getUnitCaster()).Times(AnyNumber()).WillRepeatedly(Return(nullptr));
		EXPECT_CALL(mechanicsMock, usesNewHorizonsMagicV3()).Times(AnyNumber()).WillRepeatedly(Return(false));
		EXPECT_CALL(mechanicsMock, isNegativeSpell()).Times(AnyNumber()).WillRepeatedly(Return(false));
		EXPECT_CALL(mechanicsMock, isSelectiveDispel()).Times(AnyNumber()).WillRepeatedly(Return(false));
		EXPECT_CALL(mechanicsMock, getEffectDuration()).Times(AnyNumber()).WillRepeatedly(Return(7));
		EXPECT_CALL(mechanicsMock, getEffectPower()).Times(AnyNumber()).WillRepeatedly(Return(11));
		EXPECT_CALL(mechanicsMock, getEffectLevel()).Times(AnyNumber()).WillRepeatedly(Return(2));
		EXPECT_CALL(serverMock, describeChanges()).Times(AnyNumber()).WillRepeatedly(Return(false));
		EXPECT_CALL(serverMock, getRNG()).Times(AnyNumber()).WillRepeatedly(Return(&rngMock));
		EXPECT_CALL(rngMock, nextInt(Matcher<int>(_), Matcher<int>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](int lo, int hi)
		{
			const int value = std::uniform_int_distribution<int>(lo, hi)(engine);
			trace.random.push_back({lo, hi, value});
			return value;
		}));
		EXPECT_CALL(rngMock, nextInt64(_, _)).Times(AnyNumber()).WillRepeatedly(Invoke([this](int64_t lo, int64_t hi)
		{
			const auto value = std::uniform_int_distribution<int64_t>(lo, hi)(engine);
			trace.random.push_back({lo, hi, value});
			return value;
		}));
		EXPECT_CALL(serverMock, apply(Matcher<StacksInjured &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](StacksInjured & p) { capture("injury", p); }));
		EXPECT_CALL(serverMock, apply(Matcher<BattleUnitsChanged &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](BattleUnitsChanged & p) { capture("units", p); }));
		EXPECT_CALL(serverMock, apply(Matcher<BattleStackMoved &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](BattleStackMoved & p) { capture("movement", p); }));
		EXPECT_CALL(serverMock, apply(Matcher<SetStackEffect &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](SetStackEffect & p)
		{
			capture("bonus", p);
			for(const auto & [id, bonuses] : p.toUpdate)
				for(const auto & bonus : bonuses) trace.bonusOrder.push_back(std::to_string(static_cast<int>(bonus.type)) + "/" + bonus.subtype.toString());
		}));
		EXPECT_CALL(serverMock, apply(Matcher<CatapultAttack &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](CatapultAttack & p) { capture("catapult", p); }));
		EXPECT_CALL(serverMock, apply(Matcher<BattleObstaclesChanged &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](BattleObstaclesChanged & p) { capture("obstacle", p); }));
		EXPECT_CALL(serverMock, apply(Matcher<BattleLogMessage &>(_))).Times(AnyNumber()).WillRepeatedly(Invoke([this](BattleLogMessage & p) { capture("log", p); }));
	}
	template<class Packet> void capture(const char * kind, Packet & packet)
	{
		CMemorySerializer wire;
		wire.oser & packet;
		trace.packets.emplace_back(kind, wire.extractBuffer());
	}
	void prepare(const std::string & effect, const JsonNode & config, const std::string & scope = "core")
	{
		ScriptTypeDescription description;
		description.identifier = effect;
		description.modScope = scope;
		description.scriptId = scope + ":" + effect;
		description.sourcePath = "spells/" + effect;
		description.kind = ScriptKind::SPELL_EFFECT;
		if(!CResourceHandler::get(scope)->existsResource(ScriptPath::builtinTODO(description.sourcePath).addPrefix("SCRIPTS/")))
			throw std::runtime_error("Missing unchanged Lua oracle source: " + scope + ":" + description.sourcePath);
		if(effect == "timed") description.patches = {{"core", "spells/patches/timedShield"}, {"core", "spells/patches/timedBind"}};
		luaFactory.initialize(description);
		luaPool = std::make_unique<scripting::LuaScriptPool>(luaModule, &environmentMock);
		luaStore.registerScripts(luaPool.get());
		EXPECT_CALL(*battleFake, getScriptContextPool()).Times(AnyNumber()).WillRepeatedly(ReturnRef(*luaPool));
		oracle = luaFactory.createSpellEffect(description.scriptId);
		subject = nativeSpells::create(description);
		if(!oracle || !subject)
			throw std::runtime_error("Cannot create paired spell effects: " + description.scriptId);
		oracle->init(config);
		subject->init(config);
	}
	void spell(SpellID id)
	{
		EXPECT_CALL(mechanicsMock, getSpellId()).Times(AnyNumber()).WillRepeatedly(Return(id));
		EXPECT_CALL(mechanicsMock, getSpell()).Times(AnyNumber()).WillRepeatedly(Return(id.toSpell()));
	}
	battle::UnitFake & unit(uint32_t id, BattleHex hex = BattleHex(40))
	{
		auto & result = unitsFake.add(BattleSide::ATTACKER);
		EXPECT_CALL(result, unitId()).Times(AnyNumber()).WillRepeatedly(Return(id));
		EXPECT_CALL(result, getPosition()).Times(AnyNumber()).WillRepeatedly(Return(hex));
		EXPECT_CALL(result, alive()).Times(AnyNumber()).WillRepeatedly(Return(true));
		EXPECT_CALL(result, isValidTarget(_)).Times(AnyNumber()).WillRepeatedly(Return(true));
		EXPECT_CALL(result, unitBaseAmount()).Times(AnyNumber()).WillRepeatedly(Return(12));
		EXPECT_CALL(result, getCount()).Times(AnyNumber()).WillRepeatedly(Return(12));
		EXPECT_CALL(result, getFirstHPleft()).Times(AnyNumber()).WillRepeatedly(Return(100));
		EXPECT_CALL(result, doubleWide()).Times(AnyNumber()).WillRepeatedly(Return(false));
		EXPECT_CALL(result, isInvincible()).Times(AnyNumber()).WillRepeatedly(Return(false));
		EXPECT_CALL(result, isGhost()).Times(AnyNumber()).WillRepeatedly(Return(false));
		EXPECT_CALL(result, creatureLevel()).Times(AnyNumber()).WillRepeatedly(Return(1));
		result.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH, BonusSource::CREATURE_ABILITY, 100, BonusSourceID()));
		result.expectAnyBonusSystemCall();
		EXPECT_CALL(result, acquireState()).Times(AnyNumber()).WillRepeatedly(Invoke([this, &result]
		{
			auto state = std::make_shared<::battle::CUnitStateDetached>(&result, &result);
			state->localInit(&unitEnvironment);
			return state;
		}));
		EXPECT_CALL(result, acquire()).Times(AnyNumber()).WillRepeatedly(Invoke([&result]() -> std::shared_ptr<::battle::Unit>
		{
			return result.acquireState();
		}));
		return result;
	}
	Trace run(const std::shared_ptr<spells::effects::Effect> & effect, const Target & target, uint32_t seed = 1234, const Mechanics * mechanics = nullptr, ServerCallback * server = nullptr)
	{
		trace = {};
		engine.seed(seed);
		effect->apply(server ? server : &serverMock, mechanics ? mechanics : &mechanicsMock, target);
		return trace;
	}
	void expectApplyParity(const Target & target, uint32_t seed = 1234)
	{
		const auto expected = run(oracle, target, seed);
		const auto actual = run(subject, target, seed);
		EXPECT_EQ(actual.random, expected.random);
		EXPECT_EQ(actual.packets.size(), expected.packets.size());
		for(size_t index = 0; index < std::min(actual.packets.size(), expected.packets.size()); ++index)
		{
			SCOPED_TRACE(index);
			if(actual.packets[index].first != "obstacle" || expected.packets[index].first != "obstacle") continue;
			CMemorySerializer actualWire(actual.packets[index].second), expectedWire(expected.packets[index].second);
			BattleObstaclesChanged actualPacket, expectedPacket;
			actualWire.iser & actualPacket;
			expectedWire.iser & expectedPacket;
			const auto & actualData = actualPacket.change.data;
			const auto & expectedData = expectedPacket.change.data;
			EXPECT_EQ(actualData, expectedData);
			const auto & actualCapture = actualData["obstacle"]["mdrPenetration"];
			const auto & expectedCapture = expectedData["obstacle"]["mdrPenetration"];
			EXPECT_EQ(actualCapture.getModScope(), expectedCapture.getModScope());
		}
		EXPECT_EQ(actual, expected);
		EXPECT_FALSE(actual.packets.empty());
	}
};
}
