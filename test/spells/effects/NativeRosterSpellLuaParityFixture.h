/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "EffectFixture.h"
#include "../../battleAI/ProjectedTargetHitFixture.h"
#include "../../mock/mock_spells_Problem.h"
#include "../../../luascript/StdInc.h"
#include "../../../luascript/LuaContext.h"
#include "../../../luascript/LuaModule.h"
#include "../../../luascript/LuaScriptInstance.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/filesystem/Filesystem.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/effects/native/NativeSpellFactory.h"
#include "../../../lib/scripting/ScriptTypeDescription.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include <functional>
#include <tuple>

namespace test
{
namespace rosterSpellParity
{
// As in the existing event parity fixture, apply every packet to a real
// HypotheticBattle. New units and follow-up trims/links therefore exist before
// the next script operation. Only the direct RNG is owned by the recorder.
class Recorder final : public ServerCallback
{
	ServerCallback & delegate;
	CRandomGenerator random{192837};
	template<typename Pack> void record(Pack & packet)
	{
		CMemorySerializer memory;
		CPackForClient * pointer = &packet;
		memory.oser & pointer;
		packets.emplace_back(typeid(packet).name(), memory.extractBuffer());
		delegate.apply(packet);
	}
public:
	std::vector<std::pair<std::string, std::vector<std::byte>>> packets;
	std::vector<uint32_t> recipients;
	explicit Recorder(ServerCallback & delegate) : delegate(delegate) {}
	std::vector<std::byte> rngState() const { return projectedHitTest::serialized(random); }
	void complain(const std::string & problem) override { throw std::runtime_error(problem); }
	bool describeChanges() const override { return true; }
	vstd::RNG * getRNG() override { return &random; }
	void recordResolvedSpellRecipient(uint32_t id) override
	{
		recipients.push_back(id);
		delegate.recordResolvedSpellRecipient(id);
	}
	bool rollCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor, int chance) override
	{ return delegate.rollCombatAbility(battle, actor, chance); }
	bool rollHostileCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor,
		const battle::Unit & recipient, int chance) override
	{ return delegate.rollHostileCombatAbility(battle, actor, recipient, chance); }
	bool resolveAdverseCombatRoll(const BattleID & id, BattleSide side, bool stochastic,
		bool adverse, const std::function<bool()> & draw) override
	{ return delegate.resolveAdverseCombatRoll(id, side, stochastic, adverse, draw); }
	void apply(CPackForClient & packet) override { record(packet); }
	void apply(BattleLogMessage & packet) override { record(packet); }
	void apply(BattleStackMoved & packet) override { record(packet); }
	void apply(BattleUnitsChanged & packet) override { record(packet); }
	void apply(SetStackEffect & packet) override { record(packet); }
	void apply(StacksInjured & packet) override { record(packet); }
	void apply(BattleObstaclesChanged & packet) override { record(packet); }
	void apply(CatapultAttack & packet) override { record(packet); }
};

inline std::vector<JsonNode> unitStates(const CBattleInfoCallback & battle)
{
	std::vector<JsonNode> result;
	for(const auto * unit : battle.battleGetAllUnits(false)) result.push_back(unit->acquireState()->save());
	return result;
}
inline std::vector<std::byte> obstacleState(const CBattleInfoCallback & battle)
{
	return projectedHitTest::serialized(battle.getBattle()->getAllObstacles());
}
}

class NativeRosterSpellLuaParity : public ProjectedTargetHitFixture
{
protected:
	using Aim = std::function<spells::Target(const HypotheticBattle &)>;
	void compare(const std::string & effectName, const std::string & spellName, JsonNode config, const Aim & aim,
		const std::vector<uint32_t> & receiptPrefix = std::vector<uint32_t>())
	{
		const SpellID id(SpellID::decode(spellName));
		ASSERT_NE(id, SpellID::NONE);
		const auto * spell = id.toSpell();
		ASSERT_NE(spell, nullptr);
		scripting::LuaModule host;
		ASSERT_TRUE(CResourceHandler::get("core")->existsResource(ScriptPath::builtinTODO("spells/" + effectName).addPrefix("SCRIPTS/")));
		scripting::LuaScriptInstance script(host, "core", ScriptPath::builtinTODO("spells/" + effectName).addPrefix("SCRIPTS/"), {});
		ScriptTypeDescription description;
		description.identifier = effectName;
		description.modScope = "core";
		description.scriptId = "core:" + effectName;
		description.sourcePath = "spells/" + effectName;
		description.kind = ScriptKind::SPELL_EFFECT;
		auto effect = nativeSpells::create(description);
		ASSERT_NE(effect, nullptr);
		effect->init(config);
		const auto world = gameState()->saveToMemory();
		const auto parentUnits = rosterSpellParity::unitStates(*child);
		const auto parentObstacles = rosterSpellParity::obstacleState(*child);
		const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
		using Result = std::tuple<decltype(rosterSpellParity::Recorder::packets),
			std::vector<JsonNode>, std::vector<std::byte>, std::vector<std::byte>, std::vector<uint32_t>>;
		std::optional<Result> expected;
		for(bool native : {false, true})
		{
			auto branch = std::make_shared<HypotheticBattle>(environment.get(), child);
			const auto * hero = branch->battleGetFightingHero(BattleSide::ATTACKER);
			ASSERT_NE(hero, nullptr);
			spells::BattleCast cast(branch.get(), hero, spells::Mode::HERO, spell);
			cast.setSpellLevel(3);
			cast.setEffectPower(50);
			cast.setEffectDuration(7);
			cast.setEffectValue(337);
			auto mechanics = spell->battleMechanics(&cast);
			ASSERT_NE(mechanics, nullptr);
			scripting::LuaContext context(&script, environment.get());
			context.initialize();
			ASSERT_TRUE(context.hasFunction("apply"));
			const auto beforeQueries = rosterSpellParity::unitStates(*branch);
			const auto obstaclesBeforeQueries = rosterSpellParity::obstacleState(*branch);
			const auto input = aim(*branch);
			const auto expectedTarget = context.callMethod<spells::Target>("transformTarget", config,
				static_cast<const spells::Mechanics *>(mechanics.get()), input, input);
			const auto actualTarget = effect->transformTarget(mechanics.get(), input, input);
			EXPECT_EQ(actualTarget, expectedTarget);
			const auto expectedFiltered = context.callMethod<spells::Target>("filterTarget", config,
				static_cast<const spells::Mechanics *>(mechanics.get()), expectedTarget);
			const auto actualFiltered = effect->filterTarget(mechanics.get(), actualTarget);
			EXPECT_EQ(actualFiltered, expectedFiltered);
			const auto & target = native ? actualFiltered : expectedFiltered;
			ASSERT_FALSE(target.empty());
			testing::NiceMock<spells::ProblemMock> problem;
			// NaturesWrath's raw predicate accepts one conductor. Its dedicated
			// BattleSpellMechanics validation bypasses generic expanded applicability.
			const auto & applicabilityTarget = effectName == "naturesWrath" ? input : target;
			const bool expectedApplicable = context.callMethod<bool>("applicableTarget", config,
				static_cast<const spells::Mechanics *>(mechanics.get()), static_cast<spells::Problem *>(&problem), applicabilityTarget);
			const bool actualApplicable = effect->applicableTarget(problem, mechanics.get(), applicabilityTarget);
			EXPECT_EQ(actualApplicable, expectedApplicable);
			ASSERT_TRUE(actualApplicable);
			if(effectName == "naturesWrath")
			{
				ASSERT_GT(target.size(), 1u);
				EXPECT_FALSE(context.callMethod<bool>("applicableTarget", config,
					static_cast<const spells::Mechanics *>(mechanics.get()), static_cast<spells::Problem *>(&problem), target));
				EXPECT_FALSE(effect->applicableTarget(problem, mechanics.get(), target));
			}
			const auto expectedHealth = context.callMethod<spells::effects::SpellEffectValue>("getHealthChange", config,
				static_cast<const spells::Mechanics *>(mechanics.get()), target);
			const auto actualHealth = effect->getHealthChange(mechanics.get(), target);
			EXPECT_EQ(actualHealth.hpDelta, expectedHealth.hpDelta);
			EXPECT_EQ(actualHealth.unitsDelta, expectedHealth.unitsDelta);
			EXPECT_EQ(actualHealth.unitType, expectedHealth.unitType);
			EXPECT_EQ(rosterSpellParity::unitStates(*branch), beforeQueries);
			EXPECT_EQ(rosterSpellParity::obstacleState(*branch), obstaclesBeforeQueries);
			rosterSpellParity::Recorder recorder(*branch->getServerCallback());
			if(native) effect->apply(&recorder, mechanics.get(), target);
			else context.callMethod<void>("apply", config, static_cast<const spells::Mechanics *>(mechanics.get()),
				static_cast<ServerCallback *>(&recorder), target);
			const Result result{recorder.packets, rosterSpellParity::unitStates(*branch),
				rosterSpellParity::obstacleState(*branch), recorder.rngState(), recorder.recipients};
			EXPECT_FALSE(recorder.packets.empty());
			if(receiptPrefix.empty()) EXPECT_TRUE(recorder.recipients.empty());
			else
			{
				ASSERT_GE(recorder.recipients.size(), receiptPrefix.size());
				EXPECT_TRUE(std::equal(receiptPrefix.begin(), receiptPrefix.end(), recorder.recipients.begin()));
			}
			if(!expected) expected = result;
			else EXPECT_EQ(result, *expected);
			EXPECT_EQ(rosterSpellParity::unitStates(*child), parentUnits);
			EXPECT_EQ(rosterSpellParity::obstacleState(*child), parentObstacles);
			EXPECT_EQ(gameState()->saveToMemory(), world);
			EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
			EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
		}
	}
	Aim source() const
	{
		const auto id = braced->unitId();
		return [id](const HypotheticBattle & battle) { return spells::Target{spells::Destination(battle.battleGetUnitByID(id))}; };
	}
	Aim destination() const
	{
		return [](const HypotheticBattle &) { return spells::Target{spells::Destination(BattleHex(77))}; };
	}
};
}
