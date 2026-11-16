/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "RevisedEstatesFixture.h"
#include "../../lib/mapObjects/EstatesArtifactQuote.h"
#include "../../lib/mapObjects/CGMarket.h"
#include "../../lib/mapObjects/EstatesRecruitmentQuote.h"
#include "../../lib/mapObjects/EstatesStewardship.h"
#include "../../lib/mapObjects/RecruitmentBasketAdmission.h"
#include "../../lib/networkPacks/EstatesPacks.h"

namespace
{
using namespace estatesTest;
class EstatesTransactionsTest : public RevisedFixture {};
}

TEST(EstatesHistoricalCapture, OverridesCurrentDefaultsWithoutInheritingNewPolicyMarkers)
{
	JsonNode current;
	current["heroes"]["newHorizonsPerks"] = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	GameSettings settings;
	ASSERT_NO_THROW(settings.loadBase(current));
	const auto historical = legacyEstatesPerks();
	ASSERT_TRUE(historical.getOverrideFlag());
	ASSERT_NO_THROW(settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, historical));
	const auto & effective = settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS);
	EXPECT_EQ(effective, historical);
	EXPECT_FALSE(effective.Struct().contains("estatesPolicyVersion"));
	EXPECT_FALSE(effective.Struct().contains("fourOfferProgressionVersion"));
	EXPECT_EQ(effective["unifiedSkillProgressionVersion"].Integer(), 1);
	EXPECT_EQ(effective["skills"][ESTATES]["perks"].Vector().size(), 10u);
}

TEST_F(EstatesTransactionsTest, AcceptedRecruitmentPaysAndAdvancesOneKingdomLedgerOnly)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "conscription"));
	ASSERT_NO_FATAL_FAILURE(select(second, "taxCollector", "conscription"));
	town->setVisitingHero(first);
	const auto recruit = creature("core:pixie");
	town->creatures = {{100, {recruit}}};
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	const auto unit = town->getRecruitmentCost(recruit);
	const auto together = town->getTotalRecruitmentCost(recruit, 2, first);
	setResources(handler, together);
	const auto before = gameState()->getPlayerState(PLAYER)->estates;
	const auto firstPrice = town->getTotalRecruitmentCost(recruit, 1, first);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->estates, before); // Preview is read-only.
	ASSERT_TRUE(handler.recruitCreatures(town->id, first->id, recruit, 1, 0, PLAYER));
	const auto secondPrice = town->getTotalRecruitmentCost(recruit, 1, second);
	EXPECT_EQ(firstPrice + secondPrice, together);
	town->setVisitingHero(second);
	ASSERT_TRUE(handler.recruitCreatures(town->id, second->id, recruit, 1, 0, PLAYER));
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
	for(size_t resource = 0; resource < 7; ++resource)
		EXPECT_EQ(gameState()->getPlayerState(PLAYER)->estates.recruitmentBaseCost[resource], 2u * unit[resource]);
	const auto committed = gameState()->getPlayerState(PLAYER)->estates;
	EXPECT_FALSE(handler.recruitCreatures(town->id, second->id, recruit, 1, 0, PLAYER));
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->estates, committed);
	EXPECT_EQ(town->creatures.front().first, 98u);
}

TEST_F(EstatesTransactionsTest, ForgedUnaffordableOrStalePaymentChangesNeitherStateNorResources)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ResourceSet treasury;
	treasury[EGameResID::GOLD] = 5;
	setResources(handler, treasury);
	const auto before = gameState()->getPlayerState(PLAYER)->estates;
	EstatesKingdomChanged packet;
	packet.owner = PLAYER;
	packet.before = packet.after = before;
	packet.after.recruitmentBaseCost[6] = 10;
	packet.payment[EGameResID::GOLD] = 6;
	EXPECT_THROW(gameState()->apply(packet), std::runtime_error);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->estates, before);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, treasury);
	packet.payment = ResourceSet();
	packet.before.recruitmentBaseCost[6] = 1;
	EXPECT_THROW(gameState()->apply(packet), std::runtime_error);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->estates, before);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, treasury);
}

TEST_F(EstatesTransactionsTest, StewardIsRemoteUniqueReplaceableAndDismissible)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "stewardship"));
	ASSERT_NO_FATAL_FAILURE(select(second, "taxCollector", "stewardship"));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ASSERT_NE(first->getVisitedTown(), town);
	const auto treasury = gameState()->getPlayerState(PLAYER)->resources;
	const auto ordinary = town->dailyIncome()[EGameResID::GOLD];
	ASSERT_TRUE(handler.assignTownSteward(town->id, first->id, PLAYER));
	EXPECT_EQ(newHorizonsEstates::townSteward(*town), first);
	EXPECT_EQ(town->dailyIncome()[EGameResID::GOLD], ordinary + 1000);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, treasury); // Appointment never collects income.
	ASSERT_TRUE(handler.assignTownSteward(town->id, second->id, PLAYER));
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->estates.stewards.size(), 1u);
	EXPECT_EQ(newHorizonsEstates::townSteward(*town), second);
	ASSERT_TRUE(handler.assignTownSteward(town->id, ObjectInstanceID::NONE, PLAYER));
	EXPECT_EQ(town->dailyIncome()[EGameResID::GOLD], ordinary);
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->estates.stewards.empty());
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, treasury);
}

TEST_F(EstatesTransactionsTest, StewardQualificationAndOwnershipLossEndsAppointment)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "stewardship"));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	ASSERT_TRUE(handler.assignTownSteward(town->id, first->id, PLAYER));
	EXPECT_FALSE(handler.assignTownSteward(town->id, second->id, PLAYER));
	EXPECT_EQ(newHorizonsEstates::townSteward(*town), first);
	SetSecSkill lower;
	lower.id = first->id;
	lower.which = SecondarySkill(SecondarySkill::decode(ESTATES));
	lower.mode = ChangeValueMode::ABSOLUTE;
	lower.val = MasteryLevel::BASIC;
	handler.sendAndApply(lower);
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->estates.stewards.empty());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "stewardship"));
	EXPECT_EQ(newHorizonsEstates::townSteward(*town), nullptr);
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->estates.stewards.empty());
	ASSERT_TRUE(handler.assignTownSteward(town->id, first->id, PLAYER));
	handler.setOwner(town, PlayerColor::NEUTRAL);
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->estates.stewards.empty());
}

TEST_F(EstatesTransactionsTest, CompanyUsesHighestLevelAndAccumulatesToThreeTimesItsLevel)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "mercenaryCompany"));
	ASSERT_NO_FATAL_FAILURE(select(second, "mercenaryCompany"));
	first->level = 3;
	second->level = 7;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	EXPECT_EQ(newHorizonsEstates::companyDefenseLevel(*gameState()->getPlayerState(PLAYER)), 7);
	handler.onNewTurn();
	EXPECT_EQ(rogues(mine), 7);
	EXPECT_TRUE(mine->hasCompanyDefenders());
	mine->grantWeeklyCompanyDefenders(handler, 0, 7);
	EXPECT_EQ(rogues(mine), 7);
	mine->grantWeeklyCompanyDefenders(handler, 1, 7);
	mine->grantWeeklyCompanyDefenders(handler, 2, 7);
	mine->grantWeeklyCompanyDefenders(handler, 3, 7);
	EXPECT_EQ(rogues(mine), 21);
	mine->grantWeeklyCompanyDefenders(handler, 4, 2);
	EXPECT_EQ(rogues(mine), 21); // Lower capacity never deletes existing defenders.
	EXPECT_EQ(mine->getPendingMercenaries(), 0);
}

TEST_F(EstatesTransactionsTest, FullAuthoredMineHasNoCompanyDebtOrGuardReplacement)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	for(int slot = 0; slot < 7; ++slot)
		ASSERT_TRUE(mine->setCreature(SlotID(slot), CreatureID(slot), slot + 1));
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	mine->grantWeeklyCompanyDefenders(handler, 0, 7);
	EXPECT_EQ(mine->stacksCount(), 7u);
	EXPECT_FALSE(mine->hasCompanyDefenders());
	EXPECT_EQ(mine->getPendingMercenaries(), 0);
	for(int slot = 0; slot < 7; ++slot)
		EXPECT_EQ(mine->getStackCount(SlotID(slot)), slot + 1);
	ASSERT_TRUE(handler.eraseStack(StackLocation(mine->id, SlotID(6))));
	mine->grantWeeklyCompanyDefenders(handler, 0, 7);
	EXPECT_FALSE(mine->hasCompanyDefenders()); // No refill/owed debt in the same week.
	mine->grantWeeklyCompanyDefenders(handler, 1, 7);
	EXPECT_TRUE(mine->hasCompanyDefenders());
	EXPECT_EQ(rogues(mine), 7);
}

TEST_F(EstatesTransactionsTest, CompanyCannotBeTransferredDismissedOrInheritedOnCapture)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_TRUE(mine->setCreature(SlotID(0), creature("core:rogue"), 2)); // Authored ordinary guard.
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	mine->grantWeeklyCompanyDefenders(handler, 0, 5);
	ASSERT_TRUE(mine->isCompanySlot(SlotID(1)));
	EXPECT_FALSE(handler.moveStack(StackLocation(mine->id, SlotID(1)), StackLocation(first->id, SlotID(1)), 1));
	EXPECT_FALSE(handler.disbandCreature(mine->id, SlotID(1)));
	EXPECT_FALSE(handler.bulkMoveArmy(mine->id, first->id, SlotID(0)));
	EXPECT_EQ(rogues(mine), 7);
	ASSERT_TRUE(handler.moveStack(StackLocation(mine->id, SlotID(0)), StackLocation(first->id, SlotID(1)), 1));
	EXPECT_EQ(mine->getStackCount(SlotID(0)), 1);
	handler.setOwner(mine, PlayerColor::NEUTRAL);
	EXPECT_EQ(rogues(mine), 1); // Only the independent authored guard remains.
	EXPECT_FALSE(mine->hasCompanyDefenders());
}

TEST_F(EstatesTransactionsTest, HoarderConsumesAllowanceOnlyAfterSuccessfulArtifactPurchase)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "hoarder"));
	town->setVisitingHero(first);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	if(!town->hasBuilt(BuildingID::MAGES_GUILD_1))
		ASSERT_TRUE(handler.buildStructure(town->id, BuildingID::MAGES_GUILD_1, true));
	if(first->hasArt(ArtifactID::SPELLBOOK))
		handler.removeArtifact(ArtifactLocation(first->id, ArtifactPosition::SPELLBOOK));
	setResources(handler, {});
	EXPECT_FALSE(handler.buyArtifact(first->id, ArtifactID::SPELLBOOK));
	EXPECT_EQ(first->getNewHorizonsHoarderLastWeek(), -1);
	ResourceSet book;
	book[EGameResID::GOLD] = GameConstants::SPELLBOOK_GOLD_COST;
	const auto quote = newHorizonsEstates::artifactPurchaseQuote(book, first);
	ASSERT_TRUE(quote.usesHoarder);
	setResources(handler, quote.cost);
	ASSERT_TRUE(handler.buyArtifact(first->id, ArtifactID::SPELLBOOK));
	EXPECT_EQ(first->getNewHorizonsHoarderLastWeek(), quote.week);
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
	EXPECT_EQ(newHorizonsEstates::artifactPurchaseQuote(book, first).cost, book);
	EXPECT_FALSE(handler.buyArtifact(first->id, ArtifactID::SPELLBOOK));
	EXPECT_EQ(first->getNewHorizonsHoarderLastWeek(), quote.week);
}

TEST_F(EstatesTransactionsTest, ProspectiveArtifactReservationPricesBookAndScrollWithoutSpendingLiveAllowance)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "hoarder"));
	ResourceSet bookBase;
	bookBase[EGameResID::GOLD] = GameConstants::SPELLBOOK_GOLD_COST;
	const auto book = newHorizonsEstates::artifactPurchaseQuote(bookBase, first);
	EXPECT_EQ(book.cost[EGameResID::GOLD], 250);
	EXPECT_TRUE(book.usesHoarder);
	const auto scrollBase = newHorizonsHouseOfWisdom::price(SpellID(SpellID::MAGIC_ARROW));
	ASSERT_GT(scrollBase[EGameResID::GOLD], 0);
	const auto scroll = newHorizonsEstates::artifactPurchaseQuote(scrollBase, first);
	EXPECT_EQ(scroll.cost[EGameResID::GOLD], 500);
	ResourceSet shortFunds = scroll.cost;
	--shortFunds[EGameResID::GOLD];
	EXPECT_FALSE(shortFunds.canAfford(scroll.cost));
	EXPECT_FALSE(scroll.cost.canAfford(scrollBase));
	const auto reserved = newHorizonsEstates::artifactPurchaseQuote(scrollBase, first, book.usesHoarder);
	EXPECT_EQ(reserved.cost, scrollBase);
	EXPECT_FALSE(reserved.usesHoarder);
	EXPECT_EQ(first->getNewHorizonsHoarderLastWeek(), -1);
	EXPECT_EQ(newHorizonsEstates::artifactPurchaseQuote(bookBase, first, true).cost, bookBase);
	ResourceSet mixed = scrollBase;
	mixed[EGameResID::GEMS] = 3;
	EXPECT_EQ(newHorizonsEstates::artifactPurchaseQuote(mixed, first).cost[EGameResID::GEMS], 3);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	handler.setObjPropertyValue(first->id, ObjProperty::NEW_HORIZONS_HOARDER_LAST_WEEK, book.week);
	EXPECT_EQ(newHorizonsEstates::artifactPurchaseQuote(scrollBase, first).cost, scrollBase);
}

TEST_F(EstatesTransactionsTest, SaveLoadPreservesSpentLedgerAssignmentsMineProvenanceAndHoarderWeek)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "conscription"));
	ASSERT_NO_FATAL_FAILURE(select(second, "mercenaryCompany", "stewardship"));
	town->setVisitingHero(first);
	const auto recruit = creature("core:pixie");
	town->creatures = {{100, {recruit}}};
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	setResources(handler, town->getTotalRecruitmentCost(recruit, 1, first));
	ASSERT_TRUE(handler.recruitCreatures(town->id, first->id, recruit, 1, 0, PLAYER));
	ASSERT_TRUE(handler.assignTownSteward(town->id, second->id, PLAYER));
	mine->grantWeeklyCompanyDefenders(handler, 0, 5);
	handler.setObjPropertyValue(first->id, ObjProperty::NEW_HORIZONS_HOARDER_LAST_WEEK, 0);
	const auto expected = gameState()->getPlayerState(PLAYER)->estates;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	EXPECT_EQ(restored.getPlayerState(PLAYER)->estates, expected);
	const auto * copiedMine = dynamic_cast<const CGMine *>(restored.getObjInstance(mine->id));
	ASSERT_NE(copiedMine, nullptr);
	EXPECT_TRUE(copiedMine->hasCompanyDefenders());
	EXPECT_EQ(copiedMine->getCompanyDefenseLastWeek(), 0);
	EXPECT_EQ(rogues(copiedMine), 5);
	EXPECT_EQ(restored.getHero(first->id)->getNewHorizonsHoarderLastWeek(), 0);
}

TEST_F(EstatesTransactionsTest, WarMachineRecruitmentUsesArtifactDiscountAndLeavesArmyLedgerUntouched)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "conscription"));
	first->applyPerkSelection({ESTATES, ESTATES + ".hoarder"});
	town->setVisitingHero(first);
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	if(first->hasArt(ArtifactID::BALLISTA))
		handler.removeArtifact(ArtifactLocation(first->id, ArtifactPosition::MACH1));
	const CreatureID machine(CreatureID::BALLISTA);
	town->creatures = {{2, {machine}}};
	const auto expected = newHorizonsEstates::artifactPurchaseQuote(town->getRecruitmentCost(machine), first);
	ASSERT_TRUE(expected.usesHoarder);
	EXPECT_EQ(town->getTotalRecruitmentCost(machine, 1, first), expected.cost);
	setResources(handler, expected.cost);
	EXPECT_EQ(town->getRecruitmentAmount(machine, expected.cost, 2, first), 1);
	const auto ledger = gameState()->getPlayerState(PLAYER)->estates.recruitmentBaseCost;
	const auto armySize = first->stacksCount();
	EXPECT_FALSE(handler.recruitCreatures(town->id, first->id, machine, 2, 0, PLAYER));
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, expected.cost);
	ASSERT_TRUE(handler.recruitCreatures(town->id, first->id, machine, 1, 0, PLAYER));
	EXPECT_TRUE(first->hasArt(ArtifactID::BALLISTA));
	EXPECT_EQ(first->stacksCount(), armySize);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->estates.recruitmentBaseCost, ledger);
	EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
	EXPECT_EQ(first->getNewHorizonsHoarderLastWeek(), expected.week);
}

TEST_F(EstatesTransactionsTest, BasketAdmissionAccountsForEarlierRowsWithoutChangingTheLiveArmy)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	for(int slot = 1; slot < 6; ++slot)
		ASSERT_TRUE(first->setCreature(SlotID(slot), CreatureID(slot), 1));
	const auto original = newHorizonsHeroes::commandArmySlots(*first);
	const std::array rejected = {recruitmentBasket::Row{creature("core:pixie"), 1},
		recruitmentBasket::Row{creature("core:rogue"), 1}};
	EXPECT_FALSE(recruitmentBasket::fits(*first, rejected));
	const std::array accepted = {recruitmentBasket::Row{creature("core:pikeman"), 3},
		recruitmentBasket::Row{creature("core:pixie"), 2}};
	EXPECT_TRUE(recruitmentBasket::fits(*first, accepted));
	const auto unchanged = newHorizonsHeroes::commandArmySlots(*first);
	ASSERT_EQ(unchanged.size(), original.size());
	for(size_t i = 0; i < original.size(); ++i)
	{
		EXPECT_EQ(unchanged[i].slot, original[i].slot);
		EXPECT_EQ(unchanged[i].creature, original[i].creature);
		EXPECT_EQ(unchanged[i].count, original[i].count);
	}
}
