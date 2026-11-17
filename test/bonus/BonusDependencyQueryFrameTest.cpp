/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/battle/OwnedUnitCreatureResolver.h"
namespace
{
using namespace bonusDependencies;
std::shared_ptr<Bonus> row(BonusType type, int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, type, BonusSource::OTHER, value, BonusSourceID());
}
OwnedRecipientBonusGraph::NodeInput input(int count = 21)
{
	OwnedRecipientBonusGraph::NodeInput result;
	result.facts.nodeType = BonusNodeType::STACK_BATTLE;
	result.facts.owner = PlayerColor(1);
	result.facts.stackCount = count;
	result.facts.stableStackLevel = 3;
	result.facts.acceptance.emplace();
	result.facts.creatureContext.emplace();
	result.facts.creatureContext->creature = OwnedBonusEvaluationFacts::CreatureContext::Creature{
		CreatureID(7),3,EAlignment::GOOD};
	result.facts.creatureContext->faction = FactionID(2);
	auto damage = row(BonusType::CREATURE_DAMAGE, 4);
	damage->subtype = BonusCustomSubtype::creatureDamageBoth;
	auto attack = row(BonusType::PRIMARY_SKILL, 0);
	attack->subtype = BonusSubtypeID(PrimarySkill::ATTACK);
	attack->updater = std::make_shared<TimesStackSizeUpdater>(0, 100, 2, 3);
	auto dormant = row(BonusType::PHYSICAL_DEFENSE, 37);
	dormant->limiter = std::make_shared<CreatureLevelLimiter>(99, 100);
	result.local = {damage, attack, damage, dormant};
	return result;
}
QueryFrameBinding binding()
{
	battle::OwnedUnitCreatureResolver::Creature creature;
	creature.id = CreatureID(7); creature.faction = FactionID(2); creature.level = 3;
	creature.key = "query-frame-native-fixture";
	QueryFrameBinding result;
	result.recipientId = 31; result.creature = creature.id; result.count = 21;
	result.owner = PlayerColor(1); result.side = BattleSide::ATTACKER; result.slot = SlotID(0);
	result.profileOwner = std::make_shared<const battle::OwnedUnitCreatureResolver>(std::vector{creature});
	return result;
}
recipientBonusOverlay::Layer emptyLayer()
{
	recipientBonusOverlay::Records added, updated;
	std::set<std::shared_ptr<Bonus>> removed;
	std::optional<recipientBonusOverlay::Values> stacked;
	std::optional<recipientBonusOverlay::Records> unstacked;
	recipientBonusOverlay::Groups groups;
	bool veteran = false;
	return recipientBonusOverlay::Layer({added, updated, removed, stacked, unstacked, groups, veteran});
}
void equalNative(const BonusList & actual, const BonusList & expected)
{
	ASSERT_EQ(actual.size(), expected.size());
	for(size_t i = 0; i < actual.size(); ++i)
	{
		const auto & a = *actual[i]; const auto & b = *expected[i];
		EXPECT_EQ(a.type,b.type); EXPECT_EQ(a.subtype,b.subtype); EXPECT_EQ(a.val,b.val);
		EXPECT_EQ(a.source,b.source); EXPECT_EQ(a.sid,b.sid); EXPECT_EQ(a.valType,b.valType);
		EXPECT_EQ(a.duration,b.duration); EXPECT_EQ(a.turnsRemain,b.turnsRemain);
		EXPECT_EQ(a.effectRange,b.effectRange); EXPECT_EQ(a.stacking,b.stacking); EXPECT_EQ(a.bonusOwner,b.bonusOwner);
		ASSERT_EQ(bool(a.updater),bool(b.updater));
		if(a.updater) EXPECT_EQ(typeid(*a.updater),typeid(*b.updater));
		ASSERT_EQ(bool(a.limiter),bool(b.limiter));
		if(a.limiter) EXPECT_EQ(typeid(*a.limiter),typeid(*b.limiter));
	}
}
}
TEST(BonusDependencyQueryFrameTest, BothNativeLanesRebindScratchCountBeforeOuterPublication)
{
	auto source = input(); auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source},0);
	auto current = binding();
	const auto program = captureNativeModifiers(*graph,graph->root()).bindQueries(current,{});
	ASSERT_TRUE(program);
	std::shared_ptr<const OwnedBonusQueryFrame> previous;
	for(int count : {21,8,0,15})
	{
		current.count = count; source.facts.stackCount = count;
		auto native = OwnedRecipientBonusGraph::fromOwnedInputs({source},0);
		const auto frame = program->refreshQueries(current,Change::Count,previous);
		ASSERT_TRUE(frame); ASSERT_TRUE(program->ownsQueryFrame(*frame));
		for(bool unstacked : {false,true})
			for(const auto & selector : {Selector::all,Selector::type()(BonusType::PRIMARY_SKILL)})
				equalNative(*frame->query(selector,unstacked),native->query(selector,unstacked));
		EXPECT_TRUE(frame->mayHaveBonusType(BonusType::PHYSICAL_DEFENSE)); // Dormant raw row.
		EXPECT_TRUE(frame->getAllBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE))->empty());
		EXPECT_EQ(program->refreshQueries(current,Change::None,frame),frame);
		previous = frame;
	}
}
TEST(BonusDependencyQueryFrameTest, PairDependentHateKeepsOwnedQueryCapability)
{
	auto source = input(); source.local.push_back(row(BonusType::HATE,50));
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source},0);
	const auto context = binding();
	const auto program = captureNativeModifiers(*graph,graph->root()).bindQueries(context,{});
	ASSERT_TRUE(program);
	const auto frame = program->refreshQueries(context,Change::None);
	ASSERT_TRUE(frame); EXPECT_TRUE(frame->hasBonusOfType(BonusType::HATE));
	std::array<nativeDamage::PreparedUnit,2> base;
	EXPECT_FALSE(program->refresh(21,Change::None,base,0));
	for(bool lane : {false,true}) equalNative(*frame->query(Selector::all,lane),graph->query(Selector::all,lane));
}
TEST(BonusDependencyQueryFrameTest, ForkedProgramsAndOldQueryLoansOutliveParentAndNativeGraph)
{
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({input()},0); const auto context = binding();
	auto parent = captureNativeModifiers(*graph,graph->root()).bindQueries(context,{});
	const auto child = parent;
	auto first = child->refreshQueries(context,Change::None); ASSERT_TRUE(first);
	const auto oldLoan = first->getAllBonuses(Selector::all);
	const int before = oldLoan->valOfBonuses(Selector::type()(BonusType::PRIMARY_SKILL));
	auto layer = emptyLayer(); layer.added.push_back(row(BonusType::PRIMARY_SKILL,90));
	parent = parent->appendRecipientLayer(layer,{},*first); ASSERT_TRUE(parent);
	graph->local(graph->root())[0]->val = 999;
	graph.reset(); parent.reset(); first.reset();
	EXPECT_EQ(oldLoan->valOfBonuses(Selector::type()(BonusType::PRIMARY_SKILL)),before);
	auto casualty = context; casualty.count = 8;
	const auto after = child->refreshQueries(casualty,Change::Count); ASSERT_TRUE(after);
	EXPECT_EQ(after->valOfBonuses(BonusType::CREATURE_DAMAGE),4);
	EXPECT_EQ(after->getAllBonuses(Selector::all)->size(),size_t(2));
}
TEST(BonusDependencyQueryFrameTest, FixedBaseReplacesWholeOwnLeafAndExpiryCannotReappearAfterCount)
{
	auto source = input(); auto spell = row(BonusType::PERCENTAGE_DAMAGE_BOOST,17);
	spell->source = BonusSource::SPELL_EFFECT; spell->sid = BonusSourceID(SpellID(SpellID::HASTE));
	spell->duration = BonusDuration::N_TURNS; spell->turnsRemain = 2;
	source.local.push_back(spell); source.local.push_back(spell);
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source},0); const auto context = binding();
	const std::array local{*spell,*spell}; // Exact LOCAL multiset preserves duplicate source clocks.
	const auto base = captureNativeModifiers(*graph,graph->root()).bindQueries(context,local); ASSERT_TRUE(base);
	const auto before = base->refreshQueries(context,Change::None); ASSERT_TRUE(before);
	EXPECT_EQ(before->sourceLocalSpellEffects().size(),size_t(2));
	auto applied = emptyLayer(); applied.stackedSnapshot = recipientBonusOverlay::Values{*spell};
	applied.unstackedSnapshot = recipientBonusOverlay::Records{spell};
	applied.added.push_back(row(BonusType::PHYSICAL_DEFENSE,9));
	const auto first = base->appendRecipientLayer(applied,local,*before); ASSERT_TRUE(first);
	const auto firstFrame = first->refreshQueries(context,Change::None); ASSERT_TRUE(firstFrame);
	auto expired = emptyLayer(); expired.stackedSnapshot.emplace(); expired.unstackedSnapshot.emplace();
	expired.added.push_back(row(BonusType::PHYSICAL_DEFENSE,9));
	const auto current = base->appendRecipientLayer(expired,{},*firstFrame); ASSERT_TRUE(current);
	const auto expiredFrame = current->refreshQueries(context,Change::None); ASSERT_TRUE(expiredFrame);
	EXPECT_EQ(expiredFrame->valOfBonuses(BonusType::PHYSICAL_DEFENSE),9); // No repeated leaf.
	EXPECT_FALSE(expiredFrame->hasBonusOfType(BonusType::PERCENTAGE_DAMAGE_BOOST));
	EXPECT_TRUE(expiredFrame->sourceLocalSpellEffects().empty());
	auto casualty = context; casualty.count = 8;
	const auto after = current->refreshQueries(casualty,Change::Count,expiredFrame); ASSERT_TRUE(after);
	EXPECT_FALSE(after->hasBonusOfType(BonusType::PERCENTAGE_DAMAGE_BOOST));
	EXPECT_EQ(after->valOfBonuses(BonusType::PHYSICAL_DEFENSE),9);
	EXPECT_TRUE(before->hasBonusOfType(BonusType::PERCENTAGE_DAMAGE_BOOST));
}
TEST(BonusDependencyQueryFrameTest, FinalNativeUpdaterMetadataAndRawRemovalAliasArePreserved)
{
	auto source = input(); source.local.resize(1);
	auto producer = std::make_shared<Bonus>(*source.local[0]);
	producer->updater = std::make_shared<TimesStackLevelUpdater>();
	source.local[0] = producer; // This exact configured producer feeds native graph capture.
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source},0); const auto context = binding();
	const auto base = captureNativeModifiers(*graph,graph->root()).bindQueries(context,{}); ASSERT_TRUE(base);
	const auto before = base->refreshQueries(context,Change::None); ASSERT_TRUE(before);
	equalNative(*before->getAllBonuses(Selector::all),graph->query(Selector::all));
	ASSERT_FALSE((*before->getAllBonuses(Selector::all))[0]->updater);
	auto layer = emptyLayer(); layer.removed.insert((*before->getAllBonuses(Selector::all))[0]);
	const auto removed = base->appendRecipientLayer(layer,{},*before); ASSERT_TRUE(removed);
	const auto after = removed->refreshQueries(context,Change::None); ASSERT_TRUE(after);
	EXPECT_TRUE(after->getAllBonuses(Selector::all)->empty());
}
TEST(BonusDependencyQueryFrameTest, ForeignBindingProfileUnknownRemovalAndMissingCountDirtyFailClosed)
{
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({input()},0); const auto context = binding();
	const auto base = captureNativeModifiers(*graph,graph->root()).bindQueries(context,{}); ASSERT_TRUE(base);
	const auto frame = base->refreshQueries(context,Change::None); ASSERT_TRUE(frame);
	auto other = context; other.recipientId++;
	EXPECT_FALSE(base->refreshQueries(other,Change::None,frame));
	other = context; other.ghost = true; EXPECT_FALSE(base->refreshQueries(other,Change::Profile,frame));
	other = context; other.count = 8; EXPECT_FALSE(base->refreshQueries(other,Change::None,frame));
	const auto foreign = captureNativeModifiers(*graph,graph->root()).bindQueries(context,{}); ASSERT_TRUE(foreign);
	EXPECT_FALSE(foreign->refreshQueries(context,Change::None,frame));
	auto layer = emptyLayer(); layer.removed.insert(row(BonusType::CREATURE_DAMAGE,99));
	EXPECT_FALSE(base->appendRecipientLayer(layer,{},*frame));
	EXPECT_EQ(frame->valOfBonuses(BonusType::CREATURE_DAMAGE),4);
}

TEST(BonusDependencyQueryFrameTest, UnknownNativeModifierSubclassRefusesBeforeOwnedQueryPublication)
{
	class UnknownCount final : public TimesStackSizeUpdater {};
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({input()},0);
	graph->local(graph->root())[1]->updater = std::make_shared<UnknownCount>();
	EXPECT_THROW(captureNativeModifiers(*graph,graph->root()),std::invalid_argument);
}

TEST(BonusDependencyQueryFrameTest, CurrentAncestorRecipientRowRemovalAndRepeatedOwnLeafKeepOrigin)
{
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({input()},0); auto context = binding();
	const auto root = captureNativeModifiers(*graph,graph->root()).bindQueries(context,{}); ASSERT_TRUE(root);
	const auto original = root->refreshQueries(context,Change::None); ASSERT_TRUE(original);
	auto added = emptyLayer(); auto other = row(BonusType::PHYSICAL_DEFENSE,9);
	other->duration = BonusDuration::STACK_GETS_TURN; added.added.push_back(other);
	const auto base = root->appendRecipientLayer(added,{},*original); ASSERT_TRUE(base);
	const auto before = base->refreshQueries(context,Change::None); ASSERT_TRUE(before);
	const auto sibling = before;
	auto leaf = emptyLayer(); const auto current = before->getAllBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE));
	ASSERT_EQ(current->size(),size_t(1)); leaf.removed.insert((*current)[0]);
	const auto removed = base->appendRecipientLayer(leaf,{},*before); ASSERT_TRUE(removed);
	const auto after = removed->refreshQueries(context,Change::None); ASSERT_TRUE(after);
	EXPECT_FALSE(after->hasBonusOfType(BonusType::PHYSICAL_DEFENSE));
	// Full SAME-ownleaf keeps its admitted source removal even after it is no
	// longer visible in current answers; arbitrary stale query rows do not.
	const auto repeated = base->appendRecipientLayer(leaf,{},*after); ASSERT_TRUE(repeated);
	context.count = 8; const auto casualty = repeated->refreshQueries(context,Change::Count); ASSERT_TRUE(casualty);
	EXPECT_FALSE(casualty->hasBonusOfType(BonusType::PHYSICAL_DEFENSE));
	EXPECT_EQ(sibling->valOfBonuses(BonusType::PHYSICAL_DEFENSE),9);
}
TEST(BonusDependencyQueryFrameTest, NativeDurationReplacementKeepsOriginInBothLanes)
{
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({input()},0); const auto context = binding();
	const auto root = captureNativeModifiers(*graph,graph->root()).bindQueries(context,{}); ASSERT_TRUE(root);
	const auto original = root->refreshQueries(context,Change::None); ASSERT_TRUE(original);
	auto value = row(BonusType::PHYSICAL_DEFENSE,9); value->duration=BonusDuration::N_TURNS; value->turnsRemain=1;
	auto snapshot = emptyLayer(); snapshot.added.push_back(value);
	const auto first = root->appendRecipientLayer(snapshot,{},*original); ASSERT_TRUE(first);
	const auto before = first->refreshQueries(context,Change::None); ASSERT_TRUE(before);
	auto update = emptyLayer(); auto refreshed=std::make_shared<Bonus>(*value); refreshed->turnsRemain=4;
	update.updated.push_back(refreshed); update.capturedGroups.insert({value->source,value->sid});
	const auto changed = first->appendRecipientLayer(update,{},*before); ASSERT_TRUE(changed);
	const auto frame = changed->refreshQueries(context,Change::None); ASSERT_TRUE(frame);
	for(bool lane : {false,true})
	{
		const auto rows=frame->query(Selector::type()(BonusType::PHYSICAL_DEFENSE),lane);
		ASSERT_EQ(rows->size(),size_t(1)); EXPECT_EQ((*rows)[0]->turnsRemain,4);
		auto remove=emptyLayer(); remove.removed.insert((*rows)[0]);
		const auto removed=changed->appendRecipientLayer(remove,{},*frame); ASSERT_TRUE(removed);
		const auto result=removed->refreshQueries(context,Change::None); ASSERT_TRUE(result);
		EXPECT_FALSE(result->hasBonusOfType(BonusType::PHYSICAL_DEFENSE));
		EXPECT_TRUE(result->getUnstackedBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE))->empty());
	}
}
TEST(BonusDependencyQueryFrameTest, ForeignStaleLookalikeAndAliasedOwnershipRowsAreRefused)
{
	auto graph=OwnedRecipientBonusGraph::fromOwnedInputs({input()},0); auto context=binding();
	const auto root=captureNativeModifiers(*graph,graph->root()).bindQueries(context,{}); ASSERT_TRUE(root);
	const auto initial=root->refreshQueries(context,Change::None); ASSERT_TRUE(initial);
	auto add=emptyLayer(); add.added.push_back(row(BonusType::PHYSICAL_DEFENSE,9));
	const auto base=root->appendRecipientLayer(add,{},*initial); ASSERT_TRUE(base);
	const auto old=base->refreshQueries(context,Change::None); ASSERT_TRUE(old);
	const auto stale=old->getAllBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE)); ASSERT_EQ(stale->size(),size_t(1));
	context.count=8; const auto current=base->refreshQueries(context,Change::Count,old); ASSERT_TRUE(current);
	auto remove=emptyLayer(); remove.removed.insert((*stale)[0]);
	EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current));
	remove.removed.clear(); remove.removed.insert(std::make_shared<Bonus>(*(*stale)[0]));
	EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current));
	const auto foreign=base->refreshQueries(context,Change::Count); ASSERT_TRUE(foreign);
	remove.removed.clear(); remove.removed.insert((*foreign->getAllBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE)))[0]);
	EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current));
	const auto rows=current->getAllBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE));
	const auto wrongOwner=std::shared_ptr<Bonus>(std::make_shared<int>(3),(*rows)[0].get());
	remove.removed.clear(); remove.removed.insert(wrongOwner);
	EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current));
	// Paired spell snapshots do not grant permission for an unknown input row.
	remove=emptyLayer(); remove.stackedSnapshot.emplace(); remove.unstackedSnapshot.emplace();
	auto spell=row(BonusType::PHYSICAL_DEFENSE,9); spell->source=BonusSource::SPELL_EFFECT;
	remove.removed.insert(spell); EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current));
}

TEST(BonusDependencyQueryFrameTest, StackedSnapshotValueCloneIsMappedWithoutInventedCrossLaneAlias)
{
	auto graph=OwnedRecipientBonusGraph::fromOwnedInputs({input()},0); const auto context=binding();
	const auto root=captureNativeModifiers(*graph,graph->root()).bindQueries(context,{}); ASSERT_TRUE(root);
	const auto start=root->refreshQueries(context,Change::None); ASSERT_TRUE(start);
	auto value=row(BonusType::PHYSICAL_DEFENSE,9); auto snapshot=emptyLayer();
	snapshot.stackedSnapshot=recipientBonusOverlay::Values{*value};
	snapshot.unstackedSnapshot=recipientBonusOverlay::Records{value};
	const auto base=root->appendRecipientLayer(snapshot,{},*start); ASSERT_TRUE(base);
	const auto before=base->refreshQueries(context,Change::None); ASSERT_TRUE(before);
	for(bool lane:{false,true})
	{
		const auto rows=before->query(Selector::type()(BonusType::PHYSICAL_DEFENSE),lane); ASSERT_EQ(rows->size(),size_t(1));
		auto leaf=emptyLayer(); leaf.removed.insert((*rows)[0]);
		const auto removed=base->appendRecipientLayer(leaf,{},*before); ASSERT_TRUE(removed);
		const auto after=removed->refreshQueries(context,Change::None); ASSERT_TRUE(after);
		EXPECT_TRUE(after->query(Selector::type()(BonusType::PHYSICAL_DEFENSE),lane)->empty());
		// Native snapshots have independent value/record identities per lane.
		// Equal metadata grants no cross-lane alias or source capability.
		EXPECT_EQ(after->query(Selector::type()(BonusType::PHYSICAL_DEFENSE),!lane)->size(),size_t(1));
	}
}

TEST(BonusDependencyQueryFrameTest, ExplicitFixedSourceLeaseSupportsHeldThreeRowsAcrossEveryPrefixRebind)
{
	auto graph=OwnedRecipientBonusGraph::fromOwnedInputs({input()},0); auto context=binding();
	const auto root=captureNativeModifiers(*graph,graph->root()).bindQueries(context,{}); ASSERT_TRUE(root);
	const auto initial=root->refreshQueries(context,Change::None); ASSERT_TRUE(initial);
	auto added=emptyLayer();
	for(int value:{7,9,11})
	{
		auto record=row(BonusType::PHYSICAL_DEFENSE,value); record->duration=BonusDuration::STACK_GETS_TURN;
		added.added.push_back(record);
		if(value==7) added.added.push_back(record); // One native originating alias appears twice.
	}
	const auto base=root->appendRecipientLayer(added,{},*initial); ASSERT_TRUE(base);
	auto current=base->refreshQueries(context,Change::None); ASSERT_TRUE(current);
	const auto sibling=current;
	const auto held=current->getAllBonuses(CSelector(Bonus::UntilGetsTurn)); ASSERT_EQ(held->size(),size_t(4));
	const auto source=base->bindRecipientLayerSource(current); ASSERT_TRUE(source);
	EXPECT_EQ(source->sourceFrame(),current); EXPECT_TRUE(base->ownsRecipientLayerSource(*source));
	auto fullLeaf=emptyLayer(); std::shared_ptr<const CapturedModifiers> program=base;
	for(const auto & record:*held)
	{
		fullLeaf.removed.insert(record);
		program=base->appendRecipientLayer(fullLeaf,{},*current,source.get()); ASSERT_TRUE(program);
		current=program->refreshQueries(context,Change::None); ASSERT_TRUE(current);
		EXPECT_FALSE(current->hasBonus(CSelector([&](const Bonus * value) { return value->type==record->type && value->val==record->val; })));
	}
	EXPECT_TRUE(current->getAllBonuses(CSelector(Bonus::UntilGetsTurn))->empty());
	EXPECT_TRUE(current->getUnstackedBonuses(CSelector(Bonus::UntilGetsTurn))->empty());
	context.count=8; const auto casualty=program->refreshQueries(context,Change::Count,current); ASSERT_TRUE(casualty);
	EXPECT_FALSE(casualty->hasBonus(CSelector(Bonus::UntilGetsTurn)));
	EXPECT_EQ(sibling->getAllBonuses(CSelector(Bonus::UntilGetsTurn))->size(),size_t(4));
}
TEST(BonusDependencyQueryFrameTest, FixedSourceLeaseDoesNotAdmitOtherFramesOrForeignBasePrograms)
{
	auto graph=OwnedRecipientBonusGraph::fromOwnedInputs({input()},0); auto context=binding();
	const auto root=captureNativeModifiers(*graph,graph->root()).bindQueries(context,{}); ASSERT_TRUE(root);
	const auto initial=root->refreshQueries(context,Change::None); ASSERT_TRUE(initial);
	auto added=emptyLayer(); added.added.push_back(row(BonusType::PHYSICAL_DEFENSE,9));
	const auto base=root->appendRecipientLayer(added,{},*initial); ASSERT_TRUE(base);
	const auto fixed=base->refreshQueries(context,Change::None); ASSERT_TRUE(fixed);
	const auto lease=base->bindRecipientLayerSource(fixed); ASSERT_TRUE(lease);
	const auto badFrameOwner=std::shared_ptr<const OwnedBonusQueryFrame>(std::make_shared<int>(4),fixed.get());
	EXPECT_FALSE(base->bindRecipientLayerSource(badFrameOwner));
	EXPECT_FALSE(root->bindRecipientLayerSource(fixed)); EXPECT_FALSE(root->ownsRecipientLayerSource(*lease));
	context.count=8; const auto current=base->refreshQueries(context,Change::Count,fixed); ASSERT_TRUE(current);
	const auto other=base->refreshQueries(context,Change::Count); ASSERT_TRUE(other);
	auto remove=emptyLayer(); remove.removed.insert((*other->getAllBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE)))[0]);
	EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current,lease.get()));
	auto oldCount=context; oldCount.count=13;
	const auto unleasedOld=base->refreshQueries(oldCount,Change::Count); ASSERT_TRUE(unleasedOld);
	remove.removed.clear(); remove.removed.insert((*unleasedOld->getAllBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE)))[0]);
	EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current,lease.get()));
	const auto fixedRows=fixed->getAllBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE));
	remove.removed.clear(); remove.removed.insert(std::make_shared<Bonus>(*(*fixedRows)[0]));
	EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current,lease.get()));
	remove.removed.clear(); remove.removed.insert(std::shared_ptr<Bonus>(std::make_shared<int>(3),(*fixedRows)[0].get()));
	EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current,lease.get()));
	remove.removed.clear(); remove.removed.insert((*fixedRows)[0]);
	EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current)); // Explicit receipt is necessary.
	EXPECT_TRUE(base->appendRecipientLayer(remove,{},*current,lease.get()));
	const auto foreignLease=root->bindRecipientLayerSource(initial); ASSERT_TRUE(foreignLease);
	EXPECT_FALSE(base->appendRecipientLayer(remove,{},*current,foreignLease.get()));
	EXPECT_EQ(current->valOfBonuses(BonusType::PHYSICAL_DEFENSE),9);
}


TEST(BonusDependencyQueryFrameTest, StaticCountRefreshKeepsReductionAndFreshRemovalEntitlements)
{
	auto source = input();
	auto staticAttack = std::make_shared<Bonus>(*source.local[1]);
	staticAttack->updater.reset();
	staticAttack->val = 14;
	source.local[1] = staticAttack;
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({source}, 0);
	auto context = binding();
	const auto root = captureNativeModifiers(*graph, graph->root()).bindQueries(context, {});
	ASSERT_TRUE(root);
	const auto initial = root->refreshQueries(context, Change::None);
	ASSERT_TRUE(initial);
	auto added = emptyLayer();
	added.added.push_back(row(BonusType::PHYSICAL_DEFENSE, 9));
	const auto program = root->appendRecipientLayer(added, {}, *initial);
	ASSERT_TRUE(program);
	auto previous = program->refreshQueries(context, Change::None);
	ASSERT_TRUE(previous);
	const auto original = previous;
	const auto held = previous->getAllBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE));
	ASSERT_EQ(held->size(), 1u);
	for(const int count : {8, 0, 15})
	{
		context.count = count;
		const auto current = program->refreshQueries(context, Change::Count, previous);
		ASSERT_TRUE(current);
		EXPECT_NE(current, previous);
		EXPECT_TRUE(program->ownsQueryFrame(*current));
		EXPECT_TRUE(current->matches(context.recipientId, count, context.creature));
		EXPECT_EQ(current->valOfBonuses(BonusType::PRIMARY_SKILL), 14);
		EXPECT_TRUE(current->mayHaveBonusType(BonusType::PHYSICAL_DEFENSE));
		const auto now = current->getAllBonuses(Selector::type()(BonusType::PHYSICAL_DEFENSE));
		ASSERT_EQ(now->size(), 1u);
		EXPECT_NE((*now)[0].get(), (*held)[0].get());
		EXPECT_EQ((*now)[0]->val, 9);
		auto removed = emptyLayer();
		removed.removed.insert((*held)[0]);
		EXPECT_FALSE(program->appendRecipientLayer(removed, {}, *current));
		removed.removed.clear();
		removed.removed.insert((*now)[0]);
		const auto replacement = program->appendRecipientLayer(removed, {}, *current);
		ASSERT_TRUE(replacement);
		const auto after = replacement->refreshQueries(context, Change::None);
		ASSERT_TRUE(after);
		EXPECT_FALSE(after->hasBonusOfType(BonusType::PHYSICAL_DEFENSE));
		EXPECT_EQ(program->refreshQueries(context, Change::None, current), current);
		EXPECT_FALSE(program->refreshQueries(context, Change::ActiveEffects, current));
		previous = current;
	}
	graph.reset();
	EXPECT_EQ((*held)[0]->val, 9);
	EXPECT_EQ(original->binding().count, 21);
	EXPECT_EQ(original->valOfBonuses(BonusType::PRIMARY_SKILL), 14);
}

TEST(BonusDependencyQueryFrameTest, CountDependentRefreshStillRebuildsCurrentScalarPayloads)
{
	auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({input()}, 0);
	auto context = binding();
	const auto program = captureNativeModifiers(*graph, graph->root()).bindQueries(context, {});
	ASSERT_TRUE(program);
	const auto first = program->refreshQueries(context, Change::None);
	ASSERT_TRUE(first);
	const int before = first->valOfBonuses(BonusType::PRIMARY_SKILL);
	context.count = 8;
	const auto casualty = program->refreshQueries(context, Change::Count, first);
	ASSERT_TRUE(casualty);
	EXPECT_LT(casualty->valOfBonuses(BonusType::PRIMARY_SKILL), before);
	EXPECT_EQ(first->valOfBonuses(BonusType::PRIMARY_SKILL), before);
	const auto oldRows = first->getAllBonuses(Selector::type()(BonusType::PRIMARY_SKILL));
	const auto newRows = casualty->getAllBonuses(Selector::type()(BonusType::PRIMARY_SKILL));
	ASSERT_EQ(oldRows->size(), 1u);
	ASSERT_EQ(newRows->size(), 1u);
	EXPECT_NE((*oldRows)[0].get(), (*newRows)[0].get());
}
