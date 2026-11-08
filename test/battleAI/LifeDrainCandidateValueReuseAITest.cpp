/*
 * LifeDrainCandidateValueReuseAITest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroActionSelectionState.h"
#include "../server/battles/BattleStartSnapshotFixture.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleEvaluatorSpellProjection.h"
#include "../../AI/BattleAI/HeroActionSelection.h"
#include "../../AI/BattleAI/LifeDrainCandidateValueReuse.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/networkPacks/SetMetamagicPointState.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsShadowMagnitudes.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/spells/NewHorizonsSpellcraft.h"
#include "../../lib/battle/NewHorizonsElementalAttunement.h"
#include <bit>
#include <tuple>
#include <type_traits>
#include <set>

namespace
{
SpellID lifeDrainId()
{
	return SpellID(SpellID::decode(std::string(newHorizonsLifeDrain::SPELL_KEY)));
}
// Native known-policy projection only; the AI action callback remains player-scoped.
class KnownLifeDrainBattleView final : public CPlayerBattleCallback
{
public:
	explicit KnownLifeDrainBattleView(const IBattleInfo * battle)
		: CPlayerBattleCallback(battle, PlayerColor(0)) {}
	std::optional<PlayerColor> getPlayerID() const override { return std::nullopt; }
};
class LifeDrainCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> submitted;
	LifeDrainCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override
	{
		submitted.push_back(action);
	}
};
class LifeDrainScores final : public heroActionSelection::Observer
{
public:
	using Row = std::tuple<size_t, HeroCommand, SpellID, int32_t, uint32_t,
		heroActionSelection::CandidateIdentity>;
	std::vector<Row> rows;
	std::optional<heroActionSelection::SelectionValue> selection;
	bool captureFailed = false;
	size_t identityCount = 0;
	LifeDrainScores() { rows.reserve(256); }
	void candidate(heroActionSelection::CandidateValue value) noexcept override
	{
		try
		{
			rows.emplace_back(value.ordinal, value.command, value.spell, value.overcharge,
				std::bit_cast<uint32_t>(value.value), heroActionSelection::CandidateIdentity{});
		}
		catch(...) { captureFailed = true; }
	}
	bool observeIdentity() const noexcept override { return true; }
	void candidateIdentity(size_t ordinal, heroActionSelection::CandidateIdentity identity) noexcept override
	{
		static_assert(std::is_nothrow_move_assignable_v<heroActionSelection::CandidateIdentity>);
		if(captureFailed || rows.empty() || std::get<0>(rows.back()) != ordinal || ordinal != identityCount)
			captureFailed = true;
		else
		{
			std::get<5>(rows.back()) = std::move(identity);
			++identityCount;
		}
	}
	void selected(heroActionSelection::SelectionValue value) noexcept override { selection = value; }
};
}

class LifeDrainCandidateValueReuseAITest : public HeroCommandFixture
{
protected:
	bool addLightSchool = false;
	bool completedLedgers = false;
	bool currentPointFixture = false;
	CStack * active = nullptr;
	CStack * fullFriend = nullptr;
	CStack * woundedFriend = nullptr;
	CStack * enemy = nullptr;
	std::shared_ptr<LifeDrainCallback> callback;
	std::shared_ptr<CPlayerBattleCallback> knownConfluxView;
	std::shared_ptr<projectedHitTest::Environment> environment;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}
	void mapLoaded(CMap * loaded) override
	{
		useRevisedOrders = true;
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		auto magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		if(addLightSchool)
			magic["spells"][std::string(newHorizonsLifeDrain::SPELL_KEY)]["schools"].Vector().push_back(JsonNode("new-horizons:light"));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare(bool withPerkAndFrozen)
	{
		startGame();
		if(currentPointFixture)
		{
			attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:aine")));
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
				MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		}
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto known : attackerSideHero->getSpellsInSpellbook())
			attackerSideHero->removeSpellFromSpellbook(known);
		attackerSideHero->addSpellToSpellbook(lifeDrainId());
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 30, ChangeValueMode::ABSOLUTE);
		const int shadow = SecondarySkill::decode("new-horizons:shadowMagic");
		ASSERT_GE(shadow, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(shadow), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		if(completedLedgers)
		{
			ASSERT_NO_FATAL_FAILURE(configureCompletedLedgers());
		}
		if(withPerkAndFrozen)
		{
			const std::string skill(newHorizonsMagic::SPELLCRAFT_SKILL);
			const std::string perk(newHorizonsSpellcraft::CONCENTRATION);
			const int id = SecondarySkill::decode(skill);
			ASSERT_GE(id, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(id), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(skill, perk,
				attackerSideHero->getPerkSkillRank(skill)));
			attackerSideHero->applyPerkSelection({skill, perk});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
		}
		startBattle();
		beginCombat();
		BattleUnitsChanged removed;
		removed.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			removed.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(removed);
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 100);
		fullFriend = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 3), 20);
		woundedFriend = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(4, 7), 20);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(12, 3), 80);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(fullFriend, nullptr);
		ASSERT_NE(woundedFriend, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 7), 1), nullptr);
		auto injured = woundedFriend->acquireState();
		int64_t wound = 1;
		injured->damage(wound);
		UnitChanges changed(woundedFriend->unitId(), UnitChanges::EOperation::UPDATE);
		changed.data = injured->save();
		changed.healthDelta = -wound;
		BattleUnitsChanged injury;
		injury.battleID = BattleID(0);
		injury.changedStacks.push_back(std::move(changed));
		gameHandler->sendAndApply(injury);
		if(withPerkAndFrozen)
		{
			SetStackEffect effect;
			effect.battleID = BattleID(0);
			effect.toAdd.emplace_back(enemy->unitId(), std::vector<Bonus>{newHorizonsFrozen::makeFrozenMarker(
				BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound())});
			gameHandler->sendAndApply(effect);
			ASSERT_TRUE(newHorizonsFrozen::isFrozen(*enemy));
		}
		finishSetup();
	}
	void setMarks(CStack * stack, int own, int opposing)
	{
		auto state = stack->acquireState();
		state->arcaneMarks.set(BattleSide::ATTACKER, own);
		state->arcaneMarks.set(BattleSide::DEFENDER, opposing);
		UnitChanges change(stack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		BattleUnitsChanged packet;
		packet.battleID = BattleID(0);
		packet.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(packet);
	}
	void enableCurrentPoints(int points, int first, int second)
	{
		const auto profile = newHorizonsMetamagic::profile(attackerSideHero);
		ASSERT_TRUE(newHorizonsMetamagic::usesPoints(attackerSideHero->getPerkState().rules));
		ASSERT_FALSE(newHorizonsMetamagic::usesArcaneMarks(attackerSideHero->getPerkState().rules));
		ASSERT_TRUE(profile.pointPolicy);
		ASSERT_EQ(profile.rank, 3);
		ASSERT_FALSE(profile.arcaneCatalyst);
		ASSERT_FALSE(profile.arcaneAttunement);
		ASSERT_FALSE(profile.arcaneShield);
		ASSERT_EQ(profile.adeptAdditionalPoints, 0);
		SetMetamagicPointState update;
		update.battleID = BattleID(0); update.side = BattleSide::ATTACKER;
		update.round = battle()->getRound();
		update.state = battle()->getMetamagicPointState(BattleSide::ATTACKER);
		ASSERT_LE(update.state.points, points);
		update.state.points = points;
		gameHandler->sendAndApply(update);
		ASSERT_EQ(battle()->getMetamagicPointState(BattleSide::ATTACKER).points, points);
		// Serialized unit counters are inert under the current hero-point policy.
		ASSERT_NO_FATAL_FAILURE(setMarks(active, first, 4));
		ASSERT_NO_FATAL_FAILURE(setMarks(fullFriend, second, 0));
		ASSERT_NO_FATAL_FAILURE(setMarks(woundedFriend, 5, 3));
		ASSERT_NO_FATAL_FAILURE(setMarks(enemy, 0, 5));
	}
	void finishSetup()
	{
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = active->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		callback = std::make_shared<LifeDrainCallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<projectedHitTest::Environment>(gameState());
		// Warm lazy read-only resource caches before taking exact live snapshots.
		for(const auto * unit : battle()->battleGetAllStacks(false))
		{
			(void)unit->counterAttacks.total();
			(void)unit->casts.total();
			(void)unit->shots.total();
		}
	}
	void select(CGHeroInstance * hero, const std::string & skill, const std::string & perk,
		MasteryLevel::Type rank)
	{
		const int id = SecondarySkill::decode(skill);
		ASSERT_GE(id, 0);
		hero->setSecSkillLevel(SecondarySkill(id), rank, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(hero->getPerkState().canAcquirePerk(skill, perk, hero->getPerkSkillRank(skill)));
		hero->applyPerkSelection({skill, perk});
		ASSERT_TRUE(hero->hasActivePerk(skill, perk));
	}
	void configureCompletedLedgers()
	{
		ASSERT_NO_FATAL_FAILURE(select(attackerSideHero, "new-horizons:spellcraft",
			"new-horizons:spellcraft.crossSchoolFormula", MasteryLevel::BASIC));
		ASSERT_NO_FATAL_FAILURE(select(attackerSideHero, "new-horizons:warcasting",
			"new-horizons:warcasting.arcaneValor", MasteryLevel::BASIC));
		ASSERT_NO_FATAL_FAILURE(select(defenderSideHero, "new-horizons:spellcraft",
			"new-horizons:spellcraft.spellPenetration", MasteryLevel::BASIC));
		ASSERT_NO_FATAL_FAILURE(select(defenderSideHero, "new-horizons:spellcraft",
			"new-horizons:spellcraft.counterpressure", MasteryLevel::ADVANCED));
		ASSERT_NO_FATAL_FAILURE(select(defenderSideHero, "new-horizons:warcasting",
			"new-horizons:warcasting.arcaneValor", MasteryLevel::BASIC));
		ASSERT_NO_FATAL_FAILURE(select(defenderSideHero, "new-horizons:warcasting",
			"new-horizons:warcasting.lingeringInspiration", MasteryLevel::ADVANCED));
		const int light = SecondarySkill::decode("new-horizons:lightMagic");
		ASSERT_GE(light, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(light), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->addSpellToSpellbook(SpellID::BLESS);
	}
	void seedCompletedFormula()
	{
		ASSERT_TRUE(castOn(attackerSideHero, SpellID::BLESS, active));
		ASSERT_EQ(battle()->getCrossSchoolFormulaState(BattleSide::ATTACKER).spell, SpellID::BLESS);
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
		finishSetup();
	}
	void prepareConfluxCasualty()
	{
		startGame();
		attackerSideHero->clearSlots();
		defenderSideHero->clearSlots();
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		ASSERT_NO_FATAL_FAILURE(select(defenderSideHero, "new-horizons:elementalRebirth",
			"new-horizons:elementalRebirth.primalBurst", MasteryLevel::BASIC));
		ASSERT_GT(newHorizonsElementalAttunement::captureProfile(defenderSideHero).rank, 0);
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 1000));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(1), creatureByName("core:archer"), 1000));
		ASSERT_TRUE(attackerSideHero->setCreature(SlotID(2), creatureByName("core:archer"), 1000));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), creatureByName("core:peasant"), 1000));
		ASSERT_TRUE(defenderSideHero->setCreature(SlotID(1), creatureByName("core:ogre"), 1000));
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		attackerSideHero->addSpellToSpellbook(lifeDrainId());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
		const int shadow = SecondarySkill::decode("new-horizons:shadowMagic");
		ASSERT_GE(shadow, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(shadow), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		std::vector<std::pair<CStack *, BattleHex>> positions;
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			auto * stack = battle()->getStack(unit->unitId(), false);
			if(unit->unitSide() == BattleSide::ATTACKER)
			{
				if(unit->unitSlot() == SlotID(0)) { active = stack; positions.emplace_back(stack, BattleHex(7, 5)); }
				if(unit->unitSlot() == SlotID(1)) { fullFriend = stack; positions.emplace_back(stack, BattleHex(3, 3)); }
				if(unit->unitSlot() == SlotID(2)) { woundedFriend = stack; positions.emplace_back(stack, BattleHex(4, 7)); }
			}
			else if(unit->unitSlot() == SlotID(0)) { enemy = stack; positions.emplace_back(stack, BattleHex(8, 5)); }
			else if(unit->unitSlot() == SlotID(1)) positions.emplace_back(stack, BattleHex(13, 3));
		}
		ASSERT_NE(active, nullptr);
		ASSERT_NE(fullFriend, nullptr);
		ASSERT_NE(woundedFriend, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_GT(enemy->getAvailableHealth(), 0);
		ASSERT_GT(enemy->getMaxHealth(), 0);
		for(const auto & [stack, position] : positions)
		{
			BattleStackMoved moved;
			moved.battleID = BattleID(0);
			moved.stack = stack->unitId();
			moved.tilesToMove.insert(position);
			gameHandler->sendAndApply(moved);
		}
		beginCombat();
		ASSERT_NO_FATAL_FAILURE(woundConfluxSource());
		finishSetup();
		knownConfluxView = std::make_shared<KnownLifeDrainBattleView>(battle());
		ASSERT_EQ(callback->getPlayerID(), std::make_optional(PlayerColor(0)));
		ASSERT_EQ(callback->getBattle(BattleID(0))->battleGetMySide(), BattleSide::ATTACKER);
		ASSERT_EQ(callback->getBattle(BattleID(0))->battleGetMyHero(), attackerSideHero);
		ASSERT_EQ(callback->getBattle(BattleID(0))->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
		ASSERT_EQ(knownConfluxView->battleGetMySide(), BattleSide::ALL_KNOWING);
		ASSERT_EQ(knownConfluxView->battleGetFightingHero(BattleSide::ATTACKER), attackerSideHero);
		ASSERT_EQ(knownConfluxView->battleGetFightingHero(BattleSide::DEFENDER), defenderSideHero);
		ASSERT_EQ(knownConfluxView->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(),
			VisibleHeroPolicy::Knowledge::VISIBLE_HERO);
		ASSERT_EQ(knownConfluxView->getBattle()->getSidePlayer(BattleSide::DEFENDER), defenderSideHero->getOwner());
		ASSERT_EQ(knownConfluxView->battleGetActionController(active), PlayerColor(0));
		const auto knownProfile = newHorizonsElementalAttunement::captureProfile(knownConfluxView->battleGetFightingHero(BattleSide::DEFENDER));
		ASSERT_EQ(knownProfile.rank, MasteryLevel::BASIC);
	}
	void woundConfluxSource()
	{
		const auto originalMaximum = enemy->getMaxHealth();
		const auto originalCount = enemy->getCount();
		int64_t wound = enemy->getAvailableHealth() - 10;
		ASSERT_GT(wound, 0);
		auto injured = enemy->acquireState();
		injured->damage(wound);
		UnitChanges change(enemy->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = injured->save();
		change.healthDelta = -wound;
		BattleUnitsChanged injury;
		injury.battleID = BattleID(0);
		injury.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(injury);
		ASSERT_EQ(enemy->getAvailableHealth(), 10);
		ASSERT_EQ(enemy->getMaxHealth(), originalMaximum);
		ASSERT_LT(enemy->getCount(), originalCount);
	}

	std::vector<std::byte> snapshot()
	{
		const auto live = callback->getBattle(BattleID(0));
		auto bytes = heroSelectionTest::snapshot(*live, *gameHandler->randomizer,
			dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto append = [&](const auto & value)
		{
			const auto more = projectedHitTest::serialized(value);
			bytes.insert(bytes.end(), more.begin(), more.end());
		};
		// The maintained world-save route omits battles and writes an empty replay log.
		// Retain the actual log separately so no previously compared world field is lost.
		append(gameState()->replayLog);
		const auto worldBytes = gameState()->saveToMemory();
		bytes.insert(bytes.end(), worldBytes.begin(), worldBytes.end());
		CMemorySerializer descriptors;
		battleStartFixture::writeDescriptors(*battle(), descriptors);
		const auto battleBytes = descriptors.extractBuffer();
		bytes.insert(bytes.end(), battleBytes.begin(), battleBytes.end());
		// Detached descriptors omit these live CStack provenance values.
		// Seal every actual roster entry, including dead or ghost stacks.
		for(const auto & unit : battle()->stacks)
		{
			append(unit->unitId());
			append(unit->getBattleStartMaximumAggregateHP());
			append(unit->getMaxHealth());
			append(unit->elementalAttunementPassives().attuned);
		}
		return bytes;
	}
	battleEvaluatorProjection::SpellProjection project(const CStack * friendly, HypotheticBattle & child)
	{
		PossibleSpellcast cast;
		cast.spell = lifeDrainId().toSpell();
		cast.dest = {spells::Destination(enemy), spells::Destination(friendly)};
		const auto admission = heroActionProjection::admitCandidate(child, BattleSide::ATTACKER, cast);
		EXPECT_TRUE(admission.admitted);
		if(!admission.admitted)
			return {};
		uint32_t target = std::numeric_limits<uint32_t>::max();
		return battleEvaluatorProjection::projectSpell(child, *callback->getBattle(BattleID(0)),
			attackerSideHero, BattleSide::ATTACKER, cast, admission, target, {});
	}
	void expectOriginalPairs(const LifeDrainScores & scores)
	{
		std::set<uint32_t> fullFriends;
		for(const auto & row : scores.rows)
		{
			if(std::get<2>(row) != lifeDrainId())
				continue;
			const auto & targets = std::get<5>(row).targets;
			ASSERT_EQ(targets.size(), 2u);
			ASSERT_TRUE(targets[0].unitId);
			ASSERT_TRUE(targets[1].unitId);
			const auto * originalEnemy = battle()->battleGetUnitByID(*targets[0].unitId);
			const auto * originalFriend = battle()->battleGetUnitByID(*targets[1].unitId);
			ASSERT_NE(originalEnemy, nullptr);
			ASSERT_NE(originalFriend, nullptr);
			EXPECT_EQ(targets[0].hex, originalEnemy->getPosition());
			EXPECT_EQ(targets[1].hex, originalFriend->getPosition());
			if(*targets[0].unitId == enemy->unitId() && std::get<3>(row) == 0
				&& (*targets[1].unitId == active->unitId() || *targets[1].unitId == fullFriend->unitId()))
				fullFriends.insert(*targets[1].unitId);
		}
		EXPECT_EQ(fullFriends, (std::set<uint32_t>{active->unitId(), fullFriend->unitId()}));
	}
	void compare(bool expectHits = true)
	{
		const auto before = snapshot();
		std::vector<LifeDrainScores::Row> expected;
		std::vector<std::byte> expectedHero, expectedUnit;
		std::optional<heroActionSelection::SelectionValue> expectedSelection;
		bool expectedAccepted = false;
		size_t expectedReceipts = 0;
		for(const bool enabled : {false, true})
		{
			SCOPED_TRACE(enabled);
			callback->submitted.clear();
			BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
				BattleSide::ATTACKER, 1.0f, 2);
			const auto unit = evaluator.selectStackAction(active);
			EXPECT_EQ(snapshot(), before);
			LifeDrainScores scores;
			LifeDrainCandidateValueReuse::TestControl control{enabled};
			const bool accepted = evaluator.attemptCastingSpell(active, true, &scores, nullptr, &control);
			EXPECT_EQ(snapshot(), before);
			ASSERT_EQ(callback->submitted.size(), accepted ? 1u : 0u);
			ASSERT_TRUE(scores.selection);
			ASSERT_FALSE(scores.rows.empty());
			ASSERT_FALSE(scores.captureFailed);
			ASSERT_EQ(scores.identityCount, scores.rows.size());
			ASSERT_NO_FATAL_FAILURE(expectOriginalPairs(scores));
			const auto hero = projectedHitTest::serialized(callback->submitted);
			if(!enabled)
			{
				expected = scores.rows;
				expectedHero = hero;
				expectedUnit = projectedHitTest::serialized(unit);
				expectedSelection = scores.selection;
				expectedAccepted = accepted;
				expectedReceipts = control.completedReceipts;
				EXPECT_EQ(control.hits, 0u);
			}
			else
			{
				if(expectHits)
				{
					EXPECT_GT(control.hits, 0u); // No silent all-fallback pass.
					EXPECT_GT(control.completedReceipts, 0u);
				}
				else
				{
					EXPECT_EQ(control.hits, 0u);
					EXPECT_EQ(control.completedReceipts, 0u);
				}
				EXPECT_EQ(control.completedReceipts, expectedReceipts);
				EXPECT_EQ(accepted, expectedAccepted);
				EXPECT_EQ(scores.rows, expected);
				EXPECT_EQ(hero, expectedHero); // Includes the unchanged original enemy/friendly pair.
				EXPECT_EQ(projectedHitTest::serialized(unit), expectedUnit);
				EXPECT_EQ(scores.selection->ordinal, expectedSelection->ordinal);
				EXPECT_EQ(scores.selection->disposition, expectedSelection->disposition);
				EXPECT_EQ(std::bit_cast<uint32_t>(scores.selection->baseline),
					std::bit_cast<uint32_t>(expectedSelection->baseline));
			}
		}
	}
};

TEST_F(LifeDrainCandidateValueReuseAITest, CompletedActualCastRequiresNoOpFriendAndPreservesLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto before = snapshot();
	const auto live = callback->getBattle(BattleID(0));
	HypotheticBattle first(environment.get(), live), second(environment.get(), live), wounded(environment.get(), live);
	const auto a = project(active, first);
	const auto b = project(fullFriend, second);
	ASSERT_TRUE(a.accepted);
	ASSERT_TRUE(b.accepted);
	ASSERT_TRUE(a.lifeDrain);
	ASSERT_TRUE(b.lifeDrain);
	EXPECT_EQ(a.lifeDrain, b.lifeDrain);
	EXPECT_LT(first.battleGetUnitByID(enemy->unitId())->getAvailableHealth(), enemy->getAvailableHealth());
	const auto c = project(woundedFriend, wounded);
	ASSERT_TRUE(c.accepted);
	EXPECT_FALSE(c.lifeDrain);
	EXPECT_GT(wounded.battleGetUnitByID(woundedFriend->unitId())->getAvailableHealth(), woundedFriend->getAvailableHealth());
	EXPECT_EQ(snapshot(), before);
}

TEST_F(LifeDrainCandidateValueReuseAITest, AllOrderedCandidateBitsOriginalPairsFullWorldBattleAndBothRngMatch)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(compare());
}

TEST_F(LifeDrainCandidateValueReuseAITest, ConcentrationAndFrozenRemovalKeepExactPositiveHitParity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(compare());
}

TEST_F(LifeDrainCandidateValueReuseAITest, AdditionalLightMembershipRetainsEveryOrdinaryEvaluation)
{
	addLightSchool = true;
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto before = snapshot();
	HypotheticBattle child(environment.get(), callback->getBattle(BattleID(0)));
	const auto result = project(active, child);
	ASSERT_TRUE(result.accepted);
	EXPECT_FALSE(result.lifeDrain);
	EXPECT_EQ(snapshot(), before);
	ASSERT_NO_FATAL_FAILURE(compare(false));
}

TEST_F(LifeDrainCandidateValueReuseAITest, LethalConfluxDrainKeepsUntouchedFriendReceiptAndCreatesNoReplacement)
{
	ASSERT_NO_FATAL_FAILURE(prepareConfluxCasualty());
	const auto before = snapshot();
	HypotheticBattle child(environment.get(), knownConfluxView);
	const auto count = child.battleGetAllUnits(false).size();
	const auto friendHealth = child.battleGetUnitByID(active->unitId())->getAvailableHealth();
	const auto result = project(active, child);
	ASSERT_TRUE(result.accepted);
	ASSERT_TRUE(result.lifeDrain);
	EXPECT_FALSE(child.battleGetUnitByID(enemy->unitId())->alive());
	EXPECT_EQ(child.battleGetAllUnits(false).size(), count);
	EXPECT_EQ(child.battleGetUnitByID(active->unitId())->getAvailableHealth(), friendHealth);
	EXPECT_EQ(child.getElementalAttunementMask(BattleSide::DEFENDER), 0);
	HypotheticBattle outside(environment.get(), knownConfluxView);
	const auto unaffected = project(fullFriend, outside);
	ASSERT_TRUE(unaffected.accepted);
	ASSERT_TRUE(unaffected.lifeDrain);
	EXPECT_EQ(child.battleGetFightingHero(BattleSide::DEFENDER), defenderSideHero);
	EXPECT_EQ(snapshot(), before);
	ASSERT_NO_FATAL_FAILURE(compare());
}

TEST_F(LifeDrainCandidateValueReuseAITest, CompletedFormulaResponseAndInspirationEffectsMatchForFullFriends)
{
	completedLedgers = true;
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(seedCompletedFormula());
	const auto before = snapshot();
	HypotheticBattle first(environment.get(), callback->getBattle(BattleID(0)));
	HypotheticBattle second(environment.get(), callback->getBattle(BattleID(0)));
	ASSERT_EQ(first.battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	ASSERT_EQ(first.getSideHero(BattleSide::DEFENDER), defenderSideHero);
	ASSERT_TRUE(defenderSideHero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
		std::string(newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE)));
	spells::BattleCast beforeCast(&first, attackerSideHero, spells::Mode::HERO, lifeDrainId().toSpell());
	const auto beforeMechanics = beforeCast.mechanicsForTarget({spells::Destination(first.battleGetUnitByID(enemy->unitId())),
		spells::Destination(first.battleGetUnitByID(active->unitId()))});
	ASSERT_EQ(beforeMechanics->getCastSpellPowerComponentBonusPercent(), 10);
	const auto a = project(active, first);
	const auto b = project(fullFriend, second);
	ASSERT_TRUE(a.accepted);
	ASSERT_TRUE(b.accepted);
	// Completed Arcane Inspiration refreshes the friendly army even when the
	// Life Drain healing endpoint was full. That is not a reusable no-op.
	EXPECT_FALSE(a.lifeDrain);
	EXPECT_FALSE(b.lifeDrain);
	const auto inspirationWire = [](const battle::Unit & unit)
	{
		std::vector<Bonus> records;
		for(const auto & bonus : *unit.getBonuses(CSelector(arcaneInspiration::isEffect))) records.push_back(*bonus);
		return projectedHitTest::serialized(records);
	};
	for(const auto * source : {active, fullFriend})
	{
		const auto * after = first.battleGetUnitByID(source->unitId());
		ASSERT_NE(after, nullptr);
		EXPECT_EQ(after->getAvailableHealth(), source->getAvailableHealth());
		EXPECT_NE(inspirationWire(*after), inspirationWire(*source));
		EXPECT_GT(arcaneInspiration::activeProfile(*after, PlayerColor(0)).corePercent, 0);
	}
	for(const auto * child : {&first, &second})
	{
		EXPECT_EQ(child->getCrossSchoolFormulaState(BattleSide::ATTACKER).spell, lifeDrainId());
		EXPECT_TRUE(child->hasCompletedHeroSpellCast(BattleSide::ATTACKER));
		EXPECT_TRUE(child->getSpellResponseState(BattleSide::DEFENDER).isReadyAt(child->getRound()));
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		EXPECT_EQ(first.getCrossSchoolFormulaState(side), second.getCrossSchoolFormulaState(side));
		EXPECT_EQ(projectedHitTest::serialized(first.getWisdomCombatState(side)),
			projectedHitTest::serialized(second.getWisdomCombatState(side)));
		EXPECT_EQ(projectedHitTest::serialized(first.getSpellResponseState(side)),
			projectedHitTest::serialized(second.getSpellResponseState(side)));
		EXPECT_EQ(first.getHeroActionAllowances(side), second.getHeroActionAllowances(side));
		EXPECT_EQ(first.hasCompletedHeroSpellCast(side), second.hasCompletedHeroSpellCast(side));
	}
	EXPECT_EQ(heroSelectionTest::snapshot(first, *gameHandler->randomizer, dynamic_cast<CRandomGenerator &>(*getRNG())),
		heroSelectionTest::snapshot(second, *gameHandler->randomizer, dynamic_cast<CRandomGenerator &>(*getRNG())));
	EXPECT_EQ(first.battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	EXPECT_EQ(snapshot(), before);
	ASSERT_NO_FATAL_FAILURE(compare(false)); // Every full friendly endpoint receives completion effects.
}

TEST_F(LifeDrainCandidateValueReuseAITest, CurrentHeroPointsPreserveFullFriendReceiptsDespiteDormantUnitMarks)
{
	currentPointFixture = true;
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(enableCurrentPoints(4, 0, 4));
	const auto before = snapshot();
	for(const auto * friendly : {active, fullFriend})
	{
		HypotheticBattle child(environment.get(), callback->getBattle(BattleID(0)));
		const auto result = project(friendly, child);
		ASSERT_TRUE(result.accepted);
		ASSERT_TRUE(result.lifeDrain);
		const auto * after = child.battleGetUnitByID(friendly->unitId());
		ASSERT_NE(after, nullptr);
		EXPECT_EQ(after->getAvailableHealth(), friendly->getAvailableHealth());
		EXPECT_EQ(after->metamagicMarks(BattleSide::ATTACKER), friendly->metamagicMarks(BattleSide::ATTACKER));
		EXPECT_EQ(after->metamagicMarks(BattleSide::DEFENDER), friendly->metamagicMarks(BattleSide::DEFENDER));
		EXPECT_EQ(after->acquireState()->save(), friendly->acquireState()->save());
		EXPECT_EQ(child.getMetamagicPointState(BattleSide::ATTACKER).points, 4);
	}
	EXPECT_EQ(snapshot(), before);
	ASSERT_NO_FATAL_FAILURE(compare()); // Full recipients remain equivalent; wounded recipient still heals.
}

TEST_F(LifeDrainCandidateValueReuseAITest, CurrentMaximumHeroPointsKeepFullStateAndPositiveHitParity)
{
	currentPointFixture = true;
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(enableCurrentPoints(12, 5, 5));
	const auto before = snapshot();
	for(const auto * friendly : {active, fullFriend})
	{
		HypotheticBattle child(environment.get(), callback->getBattle(BattleID(0)));
		const auto result = project(friendly, child);
		ASSERT_TRUE(result.accepted);
		ASSERT_TRUE(result.lifeDrain);
		const auto * after = child.battleGetUnitByID(friendly->unitId());
		ASSERT_NE(after, nullptr);
		EXPECT_EQ(after->acquireState()->save(), friendly->acquireState()->save());
		EXPECT_EQ(after->metamagicMarks(BattleSide::ATTACKER), 5);
		EXPECT_EQ(child.getMetamagicPointState(BattleSide::ATTACKER).points, 12);
		EXPECT_EQ(after->metamagicMarks(BattleSide::DEFENDER), friendly->metamagicMarks(BattleSide::DEFENDER));
	}
	EXPECT_EQ(snapshot(), before);
	ASSERT_NO_FATAL_FAILURE(compare()); // Every score bit, original pair, action, state and both RNGs; positive hits.
}

TEST_F(LifeDrainCandidateValueReuseAITest, CurrentMaximumHeroPointsScaleActualHealingWithoutActivatingDormantMarks)
{
	currentPointFixture = true;
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	BattleUnitsChanged removed;
	removed.battleID = BattleID(0);
	removed.changedStacks.emplace_back(woundedFriend->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(removed);
	woundedFriend = addStack(BattleSide::ATTACKER, creatureByName("core:azureDragon"), BattleHex(4, 7), 1);
	ASSERT_NE(woundedFriend, nullptr);
	ASSERT_NO_FATAL_FAILURE(enableCurrentPoints(12, 5, 5));
	int64_t wound = 800;
	auto injured = woundedFriend->acquireState();
	injured->damage(wound);
	UnitChanges change(woundedFriend->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = injured->save();
	change.healthDelta = -wound;
	BattleUnitsChanged injury;
	injury.battleID = BattleID(0);
	injury.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(injury);
	const auto before = snapshot();
	HypotheticBattle child(environment.get(), callback->getBattle(BattleID(0)));
	const auto result = project(woundedFriend, child);
	ASSERT_TRUE(result.accepted);
	EXPECT_FALSE(result.lifeDrain);
	const auto * target = child.battleGetUnitByID(woundedFriend->unitId());
	ASSERT_NE(target, nullptr);
	const auto damage = enemy->getAvailableHealth() - child.battleGetUnitByID(enemy->unitId())->getAvailableHealth();
	const auto ordinary = damage * newHorizonsMagic::lifeDrainHealingPercent(attackerSideHero) / 100;
	const auto expected = ordinary * 136 / 100; // Expert:3% per hero point,12points; recipient marks are inert.
	ASSERT_GT(ordinary, 0);
	ASSERT_GT(expected, ordinary);
	ASSERT_LT(expected, 800); // No capacity clamp can conceal the recipient multiplier.
	EXPECT_EQ(target->getAvailableHealth() - woundedFriend->getAvailableHealth(), expected);
	EXPECT_EQ(target->metamagicMarks(BattleSide::ATTACKER), 5);
	EXPECT_EQ(target->metamagicMarks(BattleSide::DEFENDER), 3);
	EXPECT_EQ(child.getMetamagicPointState(BattleSide::ATTACKER).points, 12);
	EXPECT_EQ(snapshot(), before);
	ASSERT_NO_FATAL_FAILURE(compare());
}
