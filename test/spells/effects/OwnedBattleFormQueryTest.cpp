/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../../lib/battle/OwnedProspectiveCreatureDomain.h"
#include "../../../lib/battle/OwnedUnitBonusFrame.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/bonuses/ProjectedBonusView.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../../lib/spells/effects/BattleForm.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../mock/BattleFake.h"
#include "../../mock/mock_spells_Mechanics.h"
#include "../../mock/mock_spells_Spell.h"
#include "../../mock/mock_ServerCallback.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../server/battles/BattleTestFixture.h"
#include <stdexcept>
using namespace ::testing;
namespace
{
using Domain = battle::OwnedProspectiveCreatureDomain;
using Frame = battle::OwnedUnitBonusFrame;
using Graph = OwnedRecipientBonusGraph;
std::shared_ptr<Domain> domain(CreatureID first = CreatureID(1), CreatureID second = CreatureID(2))
{
	std::vector<Domain::Species> inputs;
	for(const auto id : {first, second})
	{
		Domain::Species value;
		value.descriptor.id = id; value.descriptor.faction = FactionID(0); value.descriptor.level = 1;
		value.descriptor.key = "owned:form" + std::to_string(id.getNum());
		value.descriptor.baseHitPoints = id == first ? 10 : 1;
		value.descriptor.aiValue = id == first ? 1000 : 1;
		value.descriptor.doubleWide = id != first;
		value.category.emplace(battle::BattleFormCategory::CORE); inputs.push_back(std::move(value));
	}
	return std::make_shared<Domain>(std::move(inputs));
}
class SeedInfo final : public battle::IUnitInfo
{
	const Domain & values; CreatureID first;
public:
	SeedInfo(const Domain & values, CreatureID first) : values(values), first(first) {}
	int32_t unitBaseAmount() const override { return 3; }
	uint32_t unitId() const override { return 17; }
	BattleSide unitSide() const override { return BattleSide::ATTACKER; }
	PlayerColor unitOwner() const override { return PlayerColor(0); }
	SlotID unitSlot() const override { return SlotID(0); }
	const CCreature * unitType() const override { return values.creatures()->creature(first); }
	const battle::UnitCreatureResolver * unitCreatureResolver() const override { return values.creatures().get(); }
	std::optional<battle::UnitPreparationHealthBasis> unitPreparationHealthBasis() const override { return battle::UnitPreparationHealthBasis{30, 30}; }
};
class SeedView final : public ProjectedBonusView
{
public:
	const battle::Unit * subject = nullptr;
	const battle::Unit * evaluatedUnit() const override { return subject; }
	void onBattleFormChanged(const battle::CUnitState & unit) const override
	{
		if(&unit != subject) throw std::logic_error("Seed form notification requires its exact subject");
	}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string &) const override
	{
		BonusList raw; raw.push_back(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACK_HEALTH, BonusSource::OTHER, 10, BonusSourceID()));
		auto result = std::make_shared<BonusList>(); raw.getBonuses(*result, selector); return result;
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override { return getAllBonuses(selector, {}); }
	int32_t getTreeVersion() const override { return 0; }
};
std::shared_ptr<Frame> frame(const Domain & types, CreatureID first = CreatureID(1), CreatureID second = CreatureID(2))
{
	Frame::Inputs input;
	input.identity = {3, 17, BattleSide::ATTACKER, PlayerColor(0), SlotID(0), first, {30, 30}};
	input.creatures = types.creatures();
	Graph::NodeInput root, original, alternate;
	root.facts.nodeType = BonusNodeType::STACK_BATTLE; root.facts.stackCount = 3; root.facts.stableStackLevel = 1;
	root.facts.acceptance.emplace(); root.parents = {1, 2};
	original.facts.nodeType = alternate.facts.nodeType = BonusNodeType::CREATURE;
	original.local = {std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::CREATURE_ABILITY, 10, BonusSourceID(first))};
	alternate.local = {std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(second))};
	input.nodes = {root, original, alternate};
	for(const auto id : {first, second})
	{
		Frame::FormSources source; source.full.facts = root.facts;
		source.full.parents = id == first ? std::vector<size_t>{1} : std::vector<size_t>{1, 2};
		source.full.facts.creatureContext.emplace();
		source.full.facts.creatureContext->creature = OwnedBonusEvaluationFacts::CreatureContext::Creature{id, 1, EAlignment::NEUTRAL};
		source.full.facts.creatureContext->faction = FactionID(0);
		source.full.facts.acceptance->metadata.slot = SlotID(0); source.native = source.full;
		input.forms.emplace(id, source);
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		input.environment.colors.at(side) = PlayerColor(side == BattleSide::ATTACKER ? 0 : 1);
		input.environment.inputs.at(side).emplace(); input.environment.inputs.at(side)->color = *input.environment.colors.at(side);
		input.environment.states.at(side).emplace();
	}
	input.environment.magicRules.emplace(); input.environment.skeletonUpgrades.emplace();
	input.environmentUnits = {nullptr}; input.recipientPosition = 0;
	SeedInfo info(types, first); SeedView view; battle::CUnitStateDetached seed(&info, &view); view.subject = &seed;
	seed.localInit(nullptr); seed.setPosition(BattleHex(8, 5)); input.state = seed.save();
	return Frame::fromOwnedInputs(std::move(input));
}
class BoundBattle : public test::battle::BattleFake
{
public:
	std::shared_ptr<const Domain> retained;
	std::shared_ptr<const Frame> current;
	std::optional<std::shared_ptr<battle::CUnitState>> battleAcquireOwnedUnitState(const battle::Unit & unit) const override
	{
		if(!current) return std::shared_ptr<battle::CUnitState>();
		if(&unit != &current->unit() || unit.unitCreatureResolver() != current->unit().unitCreatureResolver())
			throw std::logic_error("Fixture loan requires the exact retained Frame subject");
		auto scratch = std::make_shared<Frame::Scratch>(current->scratch());
		return std::shared_ptr<battle::CUnitState>(scratch, &scratch->unit());
	}
	std::optional<const CreatureService *> battleOwnedCreatureService() const override
	{
		return std::optional<const CreatureService *>(retained.get());
	}
	void geometry()
	{
		ON_CALL(*this, getDefendedTown()).WillByDefault(Return(nullptr));
		ON_CALL(*this, getAllObstacles()).WillByDefault(Return(IBattleInfo::ObstacleCList{}));
		ON_CALL(*this, getBattlefieldType()).WillByDefault(Return(BattleField(BattleField::NONE)));
		ON_CALL(*this, getGateState()).WillByDefault(Return(EGateState::OPENED));
		ON_CALL(*this, getUnitsIf(_)).WillByDefault(Return(battle::Units{}));
	}
};
void mechanicsFor(spells::MechanicsMock & mechanics, BoundBattle & callback)
{
	ON_CALL(mechanics, battle()).WillByDefault(Return(&callback));
	ON_CALL(mechanics, isSmart()).WillByDefault(Return(false));
	ON_CALL(mechanics, isNegativeSpell()).WillByDefault(Return(false));
	ON_CALL(mechanics, isReceptive(_)).WillByDefault(Return(true));
	EXPECT_CALL(mechanics, creatures()).Times(0); // Owned getForms must not ask the legacy service.
}
struct WithoutLibrary
{
	GameLibrary * saved = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = saved; }
};
template<typename T> auto wire(const T & value) { CMemorySerializer memory; memory.oser & value; return memory.extractBuffer(); }
}
TEST(OwnedBattleFormQueryTest, ActualCandidateQueryRetainsBothFootprintsAndOwnedStateWithoutLibrary)
{
	auto types = domain(); const auto captured = frame(*types); const auto before = captured->save();
	NiceMock<BoundBattle> callback; callback.retained = types; callback.geometry();
	NiceMock<spells::MechanicsMock> mechanics; mechanicsFor(mechanics, callback);
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	WithoutLibrary absent; spells::effects::BattleFormEffect effect;
	const auto forms = effect.formsForTarget(&mechanics, &captured->unit());
	ASSERT_EQ(forms.size(), 2u); EXPECT_EQ(forms[0].creature, CreatureID(1)); EXPECT_EQ(forms[1].creature, CreatureID(2));
	EXPECT_TRUE(forms[0].landing.isValid()); EXPECT_TRUE(forms[1].landing.isValid());
	EXPECT_EQ(captured->save(), before); EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
}
TEST(OwnedBattleFormQueryTest, MissingAndForeignOwnedDomainsRemainUnknownInsteadOfUsingNativePool)
{
	auto types = domain(); const auto foreign = domain(); const auto captured = frame(*types); const auto before = captured->save();
	ASSERT_NE(foreign->creatures().get(), types->creatures().get());
	NiceMock<BoundBattle> callback; callback.geometry();
	NiceMock<spells::MechanicsMock> mechanics; mechanicsFor(mechanics, callback);
	WithoutLibrary absent; spells::effects::BattleFormEffect effect;
	EXPECT_THROW(effect.formsForTarget(&mechanics, &captured->unit()), std::logic_error);
	callback.retained = foreign; // Captured before retirement; same values, different descriptor source.
	EXPECT_THROW(effect.formsForTarget(&mechanics, &captured->unit()), std::logic_error);
	EXPECT_EQ(captured->save(), before);
}
class OwnedShapeshifterQueryTest : public BattleTestFixture {};
TEST_F(OwnedShapeshifterQueryTest, ActualWeightsUseCapturedAiValuesAndHpConvertedCountsWithoutRngOrStateChanges)
{
	startGame();
	const SecondarySkill chaos(SecondarySkill::decode("new-horizons:chaosMagic")); ASSERT_GE(chaos.getNum(), 0);
	attackerSideHero->setSecSkillLevel(chaos, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.misfortuneWeaver"});
	attackerSideHero->applyPerkSelection({"new-horizons:chaosMagic", "new-horizons:chaosMagic.shapeshifter"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.shapeshifter"));
	const auto first = creatureByName("core:pikeman"), second = creatureByName("core:peasant");
	auto types = domain(first, second); const auto captured = frame(*types, first, second); const auto before = captured->save();
	NiceMock<BoundBattle> callback; callback.retained = types; callback.geometry();
	NiceMock<spells::MechanicsMock> mechanics; mechanicsFor(mechanics, callback);
	NiceMock<spells::SpellMock> spell;
	ON_CALL(spell, getJsonKey()).WillByDefault(Return("new-horizons:polymorph"));
	ON_CALL(mechanics, getSpell()).WillByDefault(Return(&spell));
	ON_CALL(mechanics, getHeroCaster()).WillByDefault(Return(attackerSideHero));
	ON_CALL(mechanics, usesNewHorizonsMagicV3()).WillByDefault(Return(true));
	auto * nativeRng = dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator());
	auto * fixtureRng = dynamic_cast<CRandomGenerator *>(getRNG()); ASSERT_NE(nativeRng, nullptr); ASSERT_NE(fixtureRng, nullptr);
	const auto nativeBefore = wire(*nativeRng), fixtureBefore = wire(*fixtureRng);
	spells::effects::BattleFormEffect effect;
	EXPECT_THROW(effect.weightedFormsForTarget(&mechanics, &captured->unit()), std::logic_error);
	callback.current = captured;
	const auto foreign = captured->fork(); // Same ID/resolver/scalars are not the current occurrence.
	EXPECT_THROW(effect.weightedFormsForTarget(&mechanics, &foreign->unit()), std::logic_error);
	const auto weighted = effect.weightedFormsForTarget(&mechanics, &captured->unit());
	ASSERT_EQ(weighted.size(), 2u);
	EXPECT_EQ(weighted[0].form.creature, first); EXPECT_EQ(weighted[1].form.creature, second);
	// Owned original:3*1000=3000; alternative after exact HP conversion:30*1=30.
	EXPECT_EQ(weighted[0].weight, 1u); EXPECT_EQ(weighted[1].weight, 3u);
	EXPECT_EQ(weighted[0].totalWeight, 4u); EXPECT_EQ(weighted[1].totalWeight, 4u);
	EXPECT_EQ(captured->save(), before);
	EXPECT_EQ(wire(*nativeRng), nativeBefore); EXPECT_EQ(wire(*fixtureRng), fixtureBefore);
}

TEST(OwnedBattleFormQueryTest, RetainedStateLoanKeepsSourceAndSiblingIndependentAcrossLibraryRetirement)
{
	auto types = domain(); auto source = frame(*types); const auto sibling = source->fork();
	const auto before = source->save(), siblingBefore = sibling->save();
	const auto generation = PreparedBonusGraphStorage::currentGeneration();
	const auto randomBefore = wire(CRandomGenerator::getDefault());
	NiceMock<BoundBattle> callback; callback.retained = types; callback.current = source;
	const auto loan = callback.battleAcquireOwnedUnitState(source->unit());
	ASSERT_TRUE(loan.has_value()); ASSERT_NE(*loan, nullptr);
	const auto foreign = source->fork();
	EXPECT_THROW(callback.battleAcquireOwnedUnitState(foreign->unit()), std::logic_error);
	EXPECT_EQ(source->save(), before); EXPECT_EQ(sibling->save(), siblingBefore);
	// Source scalar changes after loan creation cannot alter its frozen count/HP basis.
	auto changed = source->scratch(); int64_t damage = 7; changed.unit().damage(damage); source->load(changed.unit().save());
	EXPECT_EQ(source->unit().getAvailableHealth(), 23); EXPECT_EQ((*loan)->getAvailableHealth(), 30);
	std::weak_ptr<const Frame> retiredSource = source;
	callback.current.reset(); callback.retained.reset(); source.reset(); types.reset();
	EXPECT_TRUE(retiredSource.expired());
	WithoutLibrary absent;
	(*loan)->beginBattleForm(CreatureID(2), 1);
	EXPECT_EQ((*loan)->getCount(), 30); EXPECT_EQ((*loan)->getAvailableHealth(), 30);
	EXPECT_EQ((*loan)->unitType()->getId(), CreatureID(2));
	EXPECT_EQ(sibling->save(), siblingBefore);
	EXPECT_EQ(PreparedBonusGraphStorage::currentGeneration(), generation);
	EXPECT_EQ(wire(CRandomGenerator::getDefault()), randomBefore);
}

TEST(OwnedBattleFormQueryTest, ActualOwnedApplyUsesAdmittedStateAndCanonicalPacketsWithExactUniformDraw)
{
	auto types = domain(); const auto captured = frame(*types); const auto receiver = captured->fork();
	const auto before = captured->save(); NiceMock<BoundBattle> callback;
	callback.retained = types; callback.current = captured; callback.geometry();
	NiceMock<spells::MechanicsMock> mechanics; mechanicsFor(mechanics, callback);
	NiceMock<spells::SpellMock> spell;
	ON_CALL(spell, getJsonKey()).WillByDefault(Return("new-horizons:polymorph"));
	ON_CALL(mechanics, getSpell()).WillByDefault(Return(&spell));
	ON_CALL(mechanics, getSpellId()).WillByDefault(Return(SpellID(42)));
	ON_CALL(mechanics, getCasterColor()).WillByDefault(Return(PlayerColor(0)));
	ON_CALL(mechanics, getBattleID()).WillByDefault(Return(BattleID(0)));
	CRandomGenerator random(1337), expectedRandom(1337); NiceMock<ServerCallbackMock> server;
	ON_CALL(server, getRNG()).WillByDefault(Return(&random)); EXPECT_CALL(server, complain(_)).Times(0);
	spells::effects::BattleFormEffect effect;
	const auto candidates = effect.weightedFormsForTarget(&mechanics, &captured->unit());
	ASSERT_EQ(candidates.size(), 2u);
	const auto expected = RandomGeneratorUtil::nextItem(candidates, expectedRandom)->form;
	InSequence packetOrder;
	EXPECT_CALL(server, apply(An<BattleUnitsChanged &>())).WillOnce([&](BattleUnitsChanged & packet)
	{
		EXPECT_EQ(packet.battleID, BattleID(0)); ASSERT_EQ(packet.changedStacks.size(), 1u);
		const auto & update = packet.changedStacks.front(); EXPECT_EQ(update.id, captured->unit().unitId());
		EXPECT_EQ(update.operation, UnitChanges::EOperation::UPDATE); receiver->load(update.data);
	});
	EXPECT_CALL(server, apply(An<SetStackEffect &>())).WillOnce([&](SetStackEffect & packet)
	{
		ASSERT_EQ(packet.toUpdate.size(), 1u); EXPECT_EQ(packet.toUpdate.front().first, captured->unit().unitId());
		ASSERT_EQ(packet.toUpdate.front().second.size(), 1u);
		EXPECT_EQ(packet.toUpdate.front().second.front().statusIdentity, "polymorph");
		EXPECT_EQ(packet.toUpdate.front().second.front().spellCasterOwner, PlayerColor(0));
	});
	const auto globalBefore = wire(CRandomGenerator::getDefault());
	WithoutLibrary absent;
	effect.apply(&server, &mechanics, spells::Target{spells::Destination(&captured->unit())});
	EXPECT_EQ(receiver->unit().creatureId(), expected.creature);
	EXPECT_EQ(receiver->unit().getPosition(), expected.landing);
	EXPECT_EQ(receiver->unit().getAvailableHealth(), 30);
	EXPECT_EQ(receiver->unit().getCount(), expected.creature == CreatureID(1) ? 3 : 30);
	EXPECT_TRUE(receiver->unit().hasBattleForm()); EXPECT_EQ(captured->save(), before);
	EXPECT_EQ(wire(random), wire(expectedRandom)); EXPECT_EQ(wire(CRandomGenerator::getDefault()), globalBefore);
}
TEST(OwnedBattleFormQueryTest, ActualOwnedApplyRejectsMissingAndForeignLoansBeforeDrawingOrPublishing)
{
	auto types = domain(); const auto captured = frame(*types); const auto foreign = captured->fork();
	const auto before = captured->save(), foreignBefore = foreign->save();
	NiceMock<BoundBattle> callback; callback.retained = types; callback.geometry();
	NiceMock<spells::MechanicsMock> mechanics; mechanicsFor(mechanics, callback);
	CRandomGenerator random(1337); NiceMock<ServerCallbackMock> server;
	ON_CALL(server, getRNG()).WillByDefault(Return(&random));
	EXPECT_CALL(server, apply(An<BattleUnitsChanged &>())).Times(0);
	EXPECT_CALL(server, apply(An<SetStackEffect &>())).Times(0);
	const auto randomBefore = wire(random), globalBefore = wire(CRandomGenerator::getDefault());
	spells::effects::BattleFormEffect effect; WithoutLibrary absent;
	EXPECT_THROW(effect.apply(&server, &mechanics, spells::Target{spells::Destination(&captured->unit())}), std::logic_error);
	callback.current = foreign; // Equal identity/scalars/resolver do not confer occurrence authority.
	EXPECT_THROW(effect.apply(&server, &mechanics, spells::Target{spells::Destination(&captured->unit())}), std::logic_error);
	EXPECT_EQ(captured->save(), before); EXPECT_EQ(foreign->save(), foreignBefore);
	EXPECT_EQ(wire(random), randomBefore); EXPECT_EQ(wire(CRandomGenerator::getDefault()), globalBefore);
}
