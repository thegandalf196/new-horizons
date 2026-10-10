/*
 * NetPacksLib.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../pathfinder/NewHorizonsLighthouse.h"
#include "PacksForClient.h"
#include "PacksForClientBattle.h"
#include "PacksForServer.h"
#include "SaveLocalState.h"
#include "SetRewardableConfiguration.h"
#include "PacksForLobby.h"
#include "SetStackEffect.h"
#include "NetPackVisitor.h"
#include "../gameState/CGameState.h"

void CPack::visit(ICPackVisitor & visitor)
{
	visitBasic(visitor);

	// visitBasic may destroy this and in such cases we do not want to call visitTyped
	if(visitor.callTyped())
	{
		visitTyped(visitor);
	}
}

void CPack::visitBasic(ICPackVisitor & visitor)
{
}

void CPack::visitTyped(ICPackVisitor & visitor)
{
	throw std::runtime_error(std::string("CPack::visitTyped called for class ") + typeid(*this).name());
}

void CPackForClient::visitBasic(ICPackVisitor & visitor)
{
	visitor.visitForClient(*this);
}

void CPackForServer::visitBasic(ICPackVisitor & visitor)
{
	visitor.visitForServer(*this);
}

void CPackForLobby::visitBasic(ICPackVisitor & visitor)
{
	visitor.visitForLobby(*this);
}

bool CPackForLobby::isForServer() const
{
	return false;
}

bool CLobbyPackToServer::isForServer() const
{
	return true;
}

void SaveLocalState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSaveLocalState(*this);
}

void PackageApplied::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitPackageApplied(*this);
}

void QueryResolved::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitQueryResolved(*this);
}

void PackageReceived::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitPackageReceived(*this);
}

void SystemMessage::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSystemMessage(*this);
}

void PlayerBlocked::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitPlayerBlocked(*this);
}

void PlayerCheated::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitPlayerCheated(*this);
}

void PlayerStartsTurn::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitPlayerStartsTurn(*this);
}

void DaysWithoutTown::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitDaysWithoutTown(*this);
}

void EntitiesChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitEntitiesChanged(*this);
}

void SetRewardableConfiguration::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetRewardableConfiguration(*this);
}

void SetResources::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetResources(*this);
}

void SetPrimarySkill::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetPrimarySkill(*this);
}

void SetHeroExperience::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetHeroExperience(*this);
}

void GiveStackExperience::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitGiveStackExperience(*this);
}

void SetSecSkill::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetSecSkill(*this);
}

void HeroVisitCastle::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitHeroVisitCastle(*this);
}

void ChangeSpells::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitChangeSpells(*this);
}

void SetResearchedSpells::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetResearchedSpells(*this);
}
void SetMana::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetMana(*this);
}

void SetMovePoints::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetMovePoints(*this);
}

void FoWChange::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitFoWChange(*this);
}

void SetAvailableHero::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetAvailableHero(*this);
}

void GiveBonus::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitGiveBonus(*this);
}

void ChangeObjPos::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitChangeObjPos(*this);
}

void PlayerEndsTurn::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitPlayerEndsTurn(*this);
}

void PlayerEndsGame::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitPlayerEndsGame(*this);
}

void RemoveBonus::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitRemoveBonus(*this);
}

void SetCommanderProperty::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetCommanderProperty(*this);
}

void AddQuest::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitAddQuest(*this);
}

void ChangeFormation::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitChangeFormation(*this);
}

void ChangeTactics::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitChangeTactics(*this);
}

void ChangeTownName::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitChangeTownName(*this);
}

void RemoveObject::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitRemoveObject(*this);
}

void TryMoveHero::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitTryMoveHero(*this);
}

void NewStructures::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitNewStructures(*this);
}

void RazeStructures::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitRazeStructures(*this);
}

void SetAvailableCreatures::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetAvailableCreatures(*this);
}

void SetHeroesInTown::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetHeroesInTown(*this);
}

void HeroRecruited::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitHeroRecruited(*this);
}

void GiveHero::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitGiveHero(*this);
}

void OpenWindow::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitOpenWindow(*this);
}

void NewObject::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitNewObject(*this);
}

void SetAvailableArtifacts::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetAvailableArtifacts(*this);
}

void SetHouseOfWisdomScrolls::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetHouseOfWisdomScrolls(*this);
}

void SetNewHorizonsAdventureSpellUnlock::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsAdventureSpellUnlock(*this);
}

void NewArtifact::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitNewArtifact(*this);
}

void RecruitTrainedStack::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitRecruitTrainedStack(*this);
}

void ChangeStackCount::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitChangeStackCount(*this);
}

void SetStackType::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetStackType(*this);
}

void EraseStack::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitEraseStack(*this);
}

void SwapStacks::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSwapStacks(*this);
}

void InsertNewStack::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitInsertNewStack(*this);
}

void RebalanceStacks::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitRebalanceStacks(*this);
}

void BulkRebalanceStacks::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBulkRebalanceStacks(*this);
}

void GrowUpArtifact::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitGrowUpArtifact(*this);
}

void PutArtifact::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitPutArtifact(*this);
}

void BulkEraseArtifacts::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBulkEraseArtifacts(*this);
}

void BulkMoveArtifacts::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBulkMoveArtifacts(*this);
}

void AssembledArtifact::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitAssembledArtifact(*this);
}

void DischargeArtifact::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitDischargeArtifact(*this);
}

void DisassembledArtifact::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitDisassembledArtifact(*this);
}

void HeroVisit::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitHeroVisit(*this);
}

void NewTurn::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitNewTurn(*this);
}

void InfoWindow::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitInfoWindow(*this);
}

void SetObjectProperty::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetObjectProperty(*this);
}

void SetScriptVariable::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetScriptVariable(*this);
}

void SetQuestHint::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetQuestHint(*this);
}

void ChangeObjectVisitors::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitChangeObjectVisitors(*this);
}

void ChangeArtifactsCostume::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitChangeArtifactsCostume(*this);
}

void HeroMasteryOffer::visitTyped(ICPackVisitor & visitor) { visitor.visitHeroMasteryOffer(*this); }
void HeroMasteryDialog::visitTyped(ICPackVisitor & visitor) { visitor.visitHeroMasteryDialog(*this); }
void HeroMasteryChosen::visitTyped(ICPackVisitor & visitor) { visitor.visitHeroMasteryChosen(*this); }
void HeroMasteryReply::visitTyped(ICPackVisitor & visitor) { visitor.visitHeroMasteryReply(*this); }
void HeroPerkChosen::visitTyped(ICPackVisitor & visitor) { visitor.visitHeroPerkChosen(*this); }

void HeroLevelUp::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitHeroLevelUp(*this);
}

void CommanderLevelUp::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitCommanderLevelUp(*this);
}

void BlockingDialog::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBlockingDialog(*this);
}

void GarrisonDialog::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitGarrisonDialog(*this);
}

void ExchangeDialog::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitExchangeDialog(*this);
}

void TeleportDialog::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitTeleportDialog(*this);
}

void MapObjectSelectDialog::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitMapObjectSelectDialog(*this);
}

void BattleStart::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleStart(*this);
}

void BattleNextRound::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleNextRound(*this);
}

void BattleDeploymentPhaseChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleDeploymentPhaseChanged(*this);
}

void BattleSetActiveStack::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleSetActiveStack(*this);
}

void BattleCrisisCommandChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleCrisisCommandChanged(*this);
}

void BattleResult::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleResult(*this);
}

void BattleLogMessage::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleLogMessage(*this);
}

void BattleStackMoved::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleStackMoved(*this);
}

void BattleUnitsChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleUnitsChanged(*this);
}

void BattleAttack::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleAttack(*this);
}

void StartAction::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitStartAction(*this);
}

void BattleHeroOrderStateChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleHeroOrderStateChanged(*this);
}

void BattleDivineMandateRecipientsChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleDivineMandateRecipientsChanged(*this);
}

void BattleDemonicGatingStateChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleDemonicGatingStateChanged(*this);
}

void BattleAdverseRerollStateChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleAdverseRerollStateChanged(*this);
}

void BattleMoraleSuppressionStateChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleMoraleSuppressionStateChanged(*this);
}

void BattleRapidResponseStateChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleRapidResponseStateChanged(*this);
}

void BattleRapidResponseStateChanged::validateAgainst(const CBattleInfoCallback & battle) const
{
	if(battleID != battle.getBattle()->getBattleID()
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| expected != battle.getBattle()->getRapidResponseState(side))
		throw std::runtime_error("Stale Rapid Response queue transition");
	expected.validateShape();
	state.validateShape();
	RapidResponseState planned;
	switch(transition)
	{
		case Transition::CAPTURE: planned = newHorizonsRapidResponse::capture(battle, side); break;
		case Transition::CONSUME: planned = newHorizonsRapidResponse::resolve(battle, side, true); break;
		case Transition::CLEAR:
			if(newHorizonsRapidResponse::pendingWaiter(battle, side))
				throw std::runtime_error("Cannot discard a valid pending Rapid Response waiter");
			planned = newHorizonsRapidResponse::resolve(battle, side, false);
			break;
		default: throw std::runtime_error("Invalid Rapid Response transition");
	}
	if(state != planned)
		throw std::runtime_error("Rapid Response update differs from shared queue plan");
}

void BattleReducedExtraActivationStateChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleReducedExtraActivationStateChanged(*this);
}

void SetArmorerDefiantState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetArmorerDefiantState(*this);
}

void SetArmorerDefiantState::validateAgainst(const CBattleInfoCallback & battle) const
{
	const auto * current = battle.getBattle();
	if(!current || current->getBattleID() != battleID)
		throw std::runtime_error("Defiant consumption targets another battle");
	validateTransitionFrom(current->getArmorerDefiantState(side), current->getRound());
	const auto * attacker = battle.battleGetUnitByID(attackerId);
	const auto * target = battle.battleGetUnitByID(targetId);
	if(!attacker || !target)
		throw std::runtime_error("Defiant consumption references a missing unit");
	BattleAttackInfo attack(attacker, target, 0, false);
	if(battle.playerToSide(battle.battleGetActionController(target)) != side
		|| !battle.battleCanUseDefiant(attack, cause))
		throw std::runtime_error("Defiant consumption has no eligible enemy denial");
}

void SetReactiveWeaveState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetReactiveWeaveState(*this);
}

void SetSpellResponseState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetSpellResponseState(*this);
}

void SetOverwhelmingFormulaState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetOverwhelmingFormulaState(*this);
}

void SetBattlecraftMasteryAward::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetBattlecraftMasteryAward(*this);
}

void EndAction::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitEndAction(*this);
}

void BattleNormalActivationCompleted::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleNormalActivationCompleted(*this);
}

void BattleSpellCast::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleSpellCast(*this);
}

void SetStackEffect::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetStackEffect(*this);
}

void StacksInjured::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitStacksInjured(*this);
}

void BattleResultsApplied::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleResultsApplied(*this);
}

void BattleEnded::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleEnded(*this);
}

void BattleObstaclesChanged::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleObstaclesChanged(*this);
}

void BattleSetStackProperty::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleSetStackProperty(*this);
}

void BattleTriggerEffect::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleTriggerEffect(*this);
}

void BattleAnimationPlayed::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleAnimationPlayed(*this);
}

void BattleUpdateGateState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleUpdateGateState(*this);
}

void AdvmapSpellCast::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitAdvmapSpellCast(*this);
}

void SetNewHorizonsAdventureSpellState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsAdventureSpellState(*this);
}

void SetNewHorizonsCastleGateState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsCastleGateState(*this);
}

void SetNewHorizonsForcedMarchState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsForcedMarchState(*this);
}

void SetNewHorizonsMusterState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsMusterState(*this);
}

void SetNewHorizonsLearningMentorState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsLearningMentorState(*this);
}

void SetNewHorizonsSageGuildVisit::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsSageGuildVisit(*this);
}

void SetNewHorizonsScholarMeeting::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsScholarMeeting(*this);
}

void SetNewHorizonsDiplomacyState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsDiplomacyState(*this);
}

void SetNewHorizonsDemonicReserve::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetNewHorizonsDemonicReserve(*this);
}

void SetPortalDwellingSource::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetPortalDwellingSource(*this);
}

void ShowWorldViewEx::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitShowWorldViewEx(*this);
}

void EndTurn::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitEndTurn(*this);
}

void GamePause::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitGamePause(*this);
}

void DismissHero::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitDismissHero(*this);
}

void MoveHero::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitMoveHero(*this);
}

void CastleTeleportHero::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitCastleTeleportHero(*this);
}

void ArrangeStacks::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitArrangeStacks(*this);
}

void BulkMoveArmy::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBulkMoveArmy(*this);
}

void BulkSplitStack::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBulkSplitStack(*this);
}

void BulkMergeStacks::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBulkMergeStacks(*this);
}

void BulkSplitAndRebalanceStack::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBulkSplitAndRebalanceStack(*this);
}

void DisbandCreature::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitDisbandCreature(*this);
}

void BuildStructure::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBuildStructure(*this);
}

void VisitTownBuilding::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitVisitTownBuilding(*this);
}

void RazeStructure::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitRazeStructure(*this);
}

void SpellResearch::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSpellResearch(*this);
}

void UnlockNewHorizonsAdventureSpell::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitUnlockNewHorizonsAdventureSpell(*this);
}

void RecruitCreatures::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitRecruitCreatures(*this);
}

void SelectPortalDwelling::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSelectPortalDwelling(*this);
}

void MusterCreatures::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitMusterCreatures(*this);
}

void ArrangeDemonicReserve::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitArrangeDemonicReserve(*this);
}

void UpgradeCreature::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitUpgradeCreature(*this);
}

void GarrisonHeroSwap::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitGarrisonHeroSwap(*this);
}

void ExchangeArtifacts::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitExchangeArtifacts(*this);
}

void BulkExchangeArtifacts::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBulkExchangeArtifacts(*this);
}

void ManageBackpackArtifacts::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitManageBackpackArtifacts(*this);
}

void ManageEquippedArtifacts::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitManageEquippedArtifacts(*this);
}

void AssembleArtifacts::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitAssembleArtifacts(*this);
}

void EraseArtifactByClient::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitEraseArtifactByClient(*this);
}

void BuyArtifact::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBuyArtifact(*this);
}

void TradeOnMarketplace::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitTradeOnMarketplace(*this);
}

void SetFormation::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetFormation(*this);
}

void SetTactics::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetTactics(*this);
}

void SetTownName::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSetTownName(*this);
}

void HireHero::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitHireHero(*this);
}

void BuildBoat::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBuildBoat(*this);
}

void QueryReply::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitQueryReply(*this);
}

void MakeAction::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitMakeAction(*this);
}

void DigWithHero::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitDigWithHero(*this);
}

void CastAdvSpell::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitCastAdvSpell(*this);
}

void RequestStatistic::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitRequestStatistic(*this);
}

void SaveGame::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitSaveGame(*this);
}

void PlayerMessage::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitPlayerMessage(*this);
}

void PlayerMessageClient::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitPlayerMessageClient(*this);
}

void CenterView::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitCenterView(*this);
}

void LobbyClientConnected::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyClientConnected(*this);
}

void LobbyClientDisconnected::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyClientDisconnected(*this);
}

void LobbyChatMessage::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyChatMessage(*this);
}

void LobbyGuiAction::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyGuiAction(*this);
}

void LobbyLoadProgress::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyLoadProgress(*this);
}

void LobbyRestartGame::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyRestartGame(*this);
}

void LobbyStartGame::validateOpportunistSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateOpportunistSerialization(supported);
}

void LobbyStartGame::validateCrossSchoolFormulaSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateCrossSchoolFormulaSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsProspectorSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsProspectorSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsHasteSpecialtySerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsHasteSpecialtySerialization(supported);
}

void LobbyStartGame::validatePlagueRulesSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validatePlagueRulesSerialization(supported);
}

void LobbyStartGame::validateRecruitmentTrainingSerialization(bool supported, bool cohortsSupported) const
{
	if(initializedGameState)
		initializedGameState->validateRecruitmentTrainingSerialization(supported, cohortsSupported);
}

void LobbyStartGame::validateNewHorizonsSageSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsSageSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsLegendaryReputationSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsLegendaryReputationSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsRecruitersContactsSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsRecruitersContactsSerialization(supported);
}

void LobbyStartGame::validateExtendSpellSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateExtendSpellSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsScholarSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsScholarSerialization(supported);
}

void LobbyStartGame::validateProtectedAdventureMobilitySerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateProtectedAdventureMobilitySerialization(supported);
}

void LobbyStartGame::validateNewHorizonsThantReanimateSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsThantReanimateSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsFrailtySpecialtySerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsFrailtySpecialtySerialization(supported);
}

void LobbyStartGame::validateNewHorizonsAenainFrailtySpecialtySerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsAenainFrailtySpecialtySerialization(supported);
}

void LobbyStartGame::validateNewHorizonsDefensiveStartSpecialtySerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsDefensiveStartSpecialtySerialization(supported);
}

void LobbyStartGame::validateNewHorizonsOffensiveStartSpecialtySerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsOffensiveStartSpecialtySerialization(supported);
}

void LobbyStartGame::validateNewHorizonsRemainingSpellSpecialtySerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsRemainingSpellSpecialtySerialization(supported);
}

void LobbyStartGame::validateNewHorizonsArtifactManaRegenerationSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsArtifactManaRegenerationSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsGlyphsOfFearSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsGlyphsOfFearSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsLighthouseSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsLighthouseSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsStartingDevelopmentSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsStartingDevelopmentSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsRemainingStartSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsRemainingStartSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsWaterWalkDayEndSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsWaterWalkDayEndSerialization(supported);
}

void LobbyStartGame::validateCanonicalSpellClausesSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateCanonicalSpellClausesSerialization(supported);
}

void LobbyStartGame::validateCombatScalarSerialization(bool finalLuckSupported, bool moraleSupported) const
{
	if(initializedGameState)
		initializedGameState->validateCombatScalarSerialization(finalLuckSupported, moraleSupported);
}

void LobbyStartGame::validateNewHorizonsCoroniusHolyWrathSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsCoroniusHolyWrathSerialization(supported);
}

void LobbyStartGame::validateCrisisCommandSerialization(bool supported) const
{
	if(initializedGameState) initializedGameState->validateCrisisCommandSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsStartingBookSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsStartingBookSerialization(supported);
}

void LobbyStartGame::validateNavigationStartSerialization(bool supported) const
{
	if(initializedGameState) initializedGameState->validateNavigationStartSerialization(supported);
}

void LobbyStartGame::validateDefaultCreatureLineSerialization(bool supported) const
{
	if(initializedStartInfo) initializedStartInfo->validateDefaultCreatureLineSerialization(supported);
	if(initializedGameState) initializedGameState->validateDefaultCreatureLineSerialization(supported);
}

void LobbyStartGame::validateNewHorizonsMagnateSerialization(bool supported) const
{
	if(initializedGameState)
		initializedGameState->validateNewHorizonsMagnateSerialization(supported);
}

void LobbyStartGame::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyStartGame(*this);
}

void LobbyPrepareStartGame::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyPrepareStartGame(*this);
}

void LobbyQuickLoadGame::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyQuickLoadGame(*this);
}

void LobbyChangeHost::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyChangeHost(*this);
}

void LobbyQueryState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyQueryState(*this);
}

void LobbyModsCheck::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyModsCheck(*this);
}

void LobbyUpdateState::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyUpdateState(*this);
}

void LobbySetMap::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetMap(*this);
}

void LobbySetCampaign::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetCampaign(*this);
}

void LobbySetCampaignMap::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetCampaignMap(*this);
}

void LobbySetCampaignBonus::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetCampaignBonus(*this);
}

void LobbySetBattleOnlyModeStartInfo::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetBattleOnlyModeStartInfo(*this);
}

void LobbyChangePlayerOption::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyChangePlayerOption(*this);
}

void LobbySetPlayer::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetPlayer(*this);
}

void LobbySetPlayerName::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetPlayerName(*this);
}

void LobbySetPlayerHandicap::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetPlayerHandicap(*this);
}

void LobbySetSimturns::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetSimturns(*this);
}

void LobbySetTurnTime::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetTurnTime(*this);
}

void LobbySetExtraOptions::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetExtraOptions(*this);
}

void LobbySetDifficulty::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbySetDifficulty(*this);
}

void LobbyForceSetPlayer::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyForceSetPlayer(*this);
}

void BattleStructureRepaired::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleStructureRepaired(*this);
}

void LobbyShowMessage::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyShowMessage(*this);
}

void LobbyPvPAction::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyPvPAction(*this);
}

void LobbyDelete::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitLobbyDelete(*this);
}

void CatapultAttack::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitCatapultAttack(*this);
}

void BattleResultAccepted::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleResultAccepted(*this);
}

void BattleCancelled::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitBattleCancelled(*this);
}

void TurnTimeUpdate::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitTurnTimeUpdate(*this);
}

void ResponseStatistic::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitResponseStatistic(*this);
}

void AdvInterfaceReady::visitTyped(ICPackVisitor & visitor)
{
	visitor.visitAdvInterfaceReady(*this);
}
