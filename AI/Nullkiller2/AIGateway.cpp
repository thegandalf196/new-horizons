/*
 * AIGateway.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsMasteryEffects.h"

#include "../../lib/AsyncRunner.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/UnlockGuard.h"
#include "../../lib/StartInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/entities/artifact/ArtifactUtils.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/mapObjects/MapObjects.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGMarket.h"
#include "../../lib/mapping/TerrainTile.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/UpgradeInfo.h"
#include "../../lib/serializer/CTypeList.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../lib/battle/BattleStateInfoForRetreat.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/CPlayerState.h"

#include "AIGateway.h"
#include "Goals/Goals.h"
#include "Helpers/ArmyFormation.h"
#include "Helpers/NewHorizonsMuster.h"

namespace NK2AI
{

AIGateway::AIGateway()
	:status(this)
{
	LOG_TRACE(logAi);
	destinationTeleport = ObjectInstanceID();
	destinationTeleportPos = int3(-1);
	nullkiller.reset(new Nullkiller());
	asyncTasks = std::make_unique<AsyncRunner>();
}

AIGateway::~AIGateway()
{
	LOG_TRACE(logAi);
	finish();
	nullkiller.reset();
}

void AIGateway::availableCreaturesChanged(const CGDwelling * town)
{
	LOG_TRACE(logAi);
	static_cast<void>(town);
	clearReplicatedMusters();
}

void AIGateway::heroMoved(const TryMoveHero & details, bool verbose)
{
	LOG_TRACE(logAi);
	const auto hero = cc->getHero(details.id);
	if(!hero)
		validateObject(ObjectIdRef(details.id, cc.get())); //enemy hero may have left visible area

	nullkiller->invalidatePathfinderData();

	const int3 from = hero ? hero->convertToVisitablePos(details.start) : (details.start - int3(0,1,0));
	const int3 to   = hero ? hero->convertToVisitablePos(details.end)   : (details.end   - int3(0,1,0));

	const CGObjectInstance * o1 = vstd::frontOrNull(cc->getVisitableObjs(from, verbose));
	const CGObjectInstance * o2 = vstd::frontOrNull(cc->getVisitableObjs(to, verbose));

	if(details.result == TryMoveHero::TELEPORTATION)
	{
		auto t1 = dynamic_cast<const CGTeleport *>(o1);
		auto t2 = dynamic_cast<const CGTeleport *>(o2);
		if(t1 && t2)
		{
			if(cc->isTeleportChannelBidirectional(t1->channel))
			{
				if(o1->ID == Obj::SUBTERRANEAN_GATE && o1->ID == o2->ID) // We need to only add subterranean gates in knownSubterraneanGates. Used for features not yet ported to use teleport channels
				{
					nullkiller->memory->addSubterraneanGate(o1, o2);
				}
			}
		}
	}
	else if(details.result == TryMoveHero::EMBARK && hero)
	{
		//make sure AI not attempt to visit used boat
		validateObject(hero->getBoat());
	}
	else if(details.result == TryMoveHero::DISEMBARK && o1)
	{
		auto boat = dynamic_cast<const CGBoat *>(o1);
		if(boat)
			memorizeVisitableObj(boat, nullkiller->memory, nullkiller->dangerHitMap, playerID, cc);
	}
}

void AIGateway::heroInGarrisonChange(const CGTownInstance * town)
{
	LOG_TRACE(logAi);
}

void AIGateway::centerView(int3 pos, int focusTime)
{
	LOG_TRACE_PARAMS(logAi, "focusTime '%i'", focusTime);
}

void AIGateway::artifactMoved(const ArtifactLocation & src, const ArtifactLocation & dst)
{
	LOG_TRACE(logAi);
}

void AIGateway::artifactAssembled(const ArtifactLocation & al)
{
	LOG_TRACE(logAi);
}

void AIGateway::showTavernWindow(const CGObjectInstance * object, const CGHeroInstance * visitor, QueryID queryID)
{
	LOG_TRACE(logAi);
	status.addQuery(queryID, "TavernWindow");
	executeActionAsync("showTavernWindow", [this, queryID](){ answerQuery(queryID, 0); });
}

void AIGateway::showThievesGuildWindow(const CGObjectInstance * obj)
{
	LOG_TRACE(logAi);
}

void AIGateway::playerBlocked(int reason, bool start)
{
	LOG_TRACE_PARAMS(logAi, "reason '%i', start '%i'", reason % start);
	if(start && reason == PlayerBlocked::UPCOMING_BATTLE)
		status.setBattle(UPCOMING_BATTLE);

	if(reason == PlayerBlocked::ONGOING_MOVEMENT)
		status.setMove(start);
}

void AIGateway::showPuzzleMap()
{
	LOG_TRACE(logAi);
}

void AIGateway::showShipyardDialog(const IShipyard * obj)
{
	LOG_TRACE(logAi);
}

void AIGateway::gameOver(PlayerColor player, const EVictoryLossCheckResult & victoryLossCheckResult)
{
	LOG_TRACE_PARAMS(logAi, "victoryLossCheckResult '%s'", victoryLossCheckResult.messageToSelf.toString(LIBRARY->staticTexts()));
	logAi->debug("Player %d (%s): I heard that player %d (%s) %s.", playerID, playerID.toString(), player, player.toString(), (victoryLossCheckResult.victory() ? "won" : "lost"));

	// some whitespace to flush stream
	logAi->debug(std::string(200, ' '));

	if(player == playerID)
	{
		if(victoryLossCheckResult.victory())
		{
			logAi->debug("AIGateway: Player %d (%s) won. I won! Incredible!", player, player.toString());
			logAi->debug("Turn nr %d", cc->getCalendar().getCurrentDay());
		}
		else
		{
			logAi->debug("AIGateway: Player %d (%s) lost. It's me. What a disappointment! :(", player, player.toString());
		}

		nullkiller->makingTurnInterruption.interruptThread();
	}
}

void AIGateway::artifactPut(const ArtifactLocation & al)
{
	LOG_TRACE(logAi);
}

void AIGateway::artifactRemoved(const ArtifactLocation & al)
{
	LOG_TRACE(logAi);
}

void AIGateway::artifactDisassembled(const ArtifactLocation & al)
{
	LOG_TRACE(logAi);
}

void AIGateway::heroVisit(const CGHeroInstance * visitor, const CGObjectInstance * visitedObj, bool start)
{
	LOG_TRACE_PARAMS(logAi, "start '%i'; obj '%s'", start % (visitedObj ? visitedObj->getObjectNameTextID() : std::string("n/a")));

	if(start && visitedObj) //we can end visit with null object, anyway
	{
		nullkiller->memory->markObjectVisited(visitedObj);
		nullkiller->objectClusterizer->invalidate(visitedObj->id);
	}

	status.heroVisit(visitedObj, start);
}

void AIGateway::availableArtifactsChanged(const CGBlackMarket * bm)
{
	LOG_TRACE(logAi);
}

void AIGateway::heroVisitsTown(const CGHeroInstance * hero, const CGTownInstance * town)
{
	LOG_TRACE(logAi);
}

void AIGateway::tileHidden(const FowTilesType & pos)
{
	LOG_TRACE(logAi);

	nullkiller->memory->removeInvisibleOrDeletedObjects(*cc);
}

void AIGateway::tileRevealed(const FowTilesType & pos)
{
	LOG_TRACE(logAi);

	for(int3 tile : pos)
	{
		for(const CGObjectInstance * obj : cc->getVisitableObjs(tile))
			memorizeVisitableObj(obj, nullkiller->memory, nullkiller->dangerHitMap, playerID, cc);
	}

	if (nullkiller->settings->isUpdateHitmapOnTileReveal() && !pos.empty())
		nullkiller->dangerHitMap->resetTileOwners();
}

void AIGateway::heroExchangeStarted(ObjectInstanceID hero1, ObjectInstanceID hero2, QueryID query)
{
	LOG_TRACE(logAi);
	auto firstHero = cc->getHero(hero1);
	auto secondHero = cc->getHero(hero2);

	status.addQuery(
		query,
		boost::str(
			boost::format("Exchange between heroes %s (%d) and %s (%d)") % firstHero->getNameTextID() % firstHero->tempOwner
			% secondHero->getNameTextID() % secondHero->tempOwner
		)
	);

	executeActionAsync(
		"heroExchangeStarted",
		[this, firstHero, secondHero, query]()
		{
			auto transferFrom2to1 = [this](const CGHeroInstance * h1, const CGHeroInstance * h2) -> void
			{
				this->pickBestCreatures(h1, h2);
				pickBestArtifacts(cc, h1, h2);
			};

			//Do not attempt army or artifacts exchange if we visited ally player
			//Visits can still be useful if hero have skills like Scholar
			if(firstHero->tempOwner != secondHero->tempOwner)
			{
				logAi->debug("Heroes owned by different players. Do not exchange army or artifacts.");
			}
			else
			{
				if(nullkiller->isActive(firstHero))
					transferFrom2to1(secondHero, firstHero);
				else
					transferFrom2to1(firstHero, secondHero);
			}

			answerQuery(query, 0);
		}
	);
}

void AIGateway::heroExperienceChanged(const CGHeroInstance * hero, si64 val)
{
	LOG_TRACE_PARAMS(logAi, "val '%i'", val);
}

void AIGateway::heroPrimarySkillChanged(const CGHeroInstance * hero, PrimarySkill which, si64 val)
{
	LOG_TRACE_PARAMS(logAi, "which '%i', val '%i'", which.getNum() % val);
}

void AIGateway::showRecruitmentDialog(const CGDwelling * dwelling, const CArmedInstance * dst, int level, QueryID queryID)
{
	LOG_TRACE_PARAMS(logAi, "level '%i'", level);
	status.addQuery(queryID, "RecruitmentDialog");

	executeActionAsync("showRecruitmentDialog", [this, dwelling, dst, queryID](){
		recruitCreatures(dwelling, dst);
		answerQuery(queryID, 0);
	});
}

void AIGateway::heroMovePointsChanged(const CGHeroInstance * hero)
{
	LOG_TRACE(logAi);
}

void AIGateway::garrisonsChanged(ObjectInstanceID id1, ObjectInstanceID id2)
{
	LOG_TRACE(logAi);
}

void AIGateway::newObject(const CGObjectInstance * obj)
{
	LOG_TRACE(logAi);
	nullkiller->invalidatePathfinderData();
	if(obj->isVisitable())
		memorizeVisitableObj(obj, nullkiller->memory, nullkiller->dangerHitMap, playerID, cc);
}

//to prevent AI from accessing objects that got deleted while they became invisible (Cover of Darkness, enemy hero moved etc.) below code allows AI to know deletion of objects out of sight
//see: RemoveObject::applyFirstCl, to keep AI "not cheating" do not use advantage of this and use this function just to prevent crashes
void AIGateway::objectRemoved(const CGObjectInstance * obj, const PlayerColor & initiator)
{
	LOG_TRACE(logAi);
	if(!nullkiller) // crash protection
		return;

	nullkiller->memory->removeFromMemory(obj);
	nullkiller->objectClusterizer->onObjectRemoved(obj->id);

	if(nullkiller->baseGraph && nullkiller->isObjectGraphAllowed())
	{
		nullkiller->baseGraph->removeObject(obj, *nullkiller->cc);
	}

	if(obj->ID == Obj::HERO && obj->tempOwner == playerID)
	{
		lostHero(HeroPtr(cc->getHero(obj->id), cc.get())); //we can promote, since objectRemoved is called just before actual deletion
	}

	if(obj->ID == Obj::HERO && cc->getPlayerRelations(obj->tempOwner, playerID) == PlayerRelations::ENEMIES)
		nullkiller->dangerHitMap->resetHitmap();

	if(obj->ID == Obj::TOWN)
		nullkiller->dangerHitMap->resetTileOwners();
}

void AIGateway::showHillFortWindow(const CGObjectInstance * object, const CGHeroInstance * visitor)
{
	LOG_TRACE(logAi);
}

void AIGateway::playerBonusChanged(const Bonus & bonus, bool gain)
{
	LOG_TRACE_PARAMS(logAi, "gain '%i'", gain);
}

void AIGateway::heroCreated(const CGHeroInstance * h)
{
	LOG_TRACE(logAi);
	nullkiller->invalidatePathfinderData(); // new hero needs to look around
}

void AIGateway::advmapSpellCast(const CGHeroInstance * caster, SpellID spellID)
{
	LOG_TRACE_PARAMS(logAi, "spellID '%i", spellID);
}

void AIGateway::showInfoDialog(EInfoWindowMode type, const std::string & text, const std::vector<Component> & components, int soundID)
{
	LOG_TRACE_PARAMS(logAi, "soundID '%i'", soundID);
}

void AIGateway::requestRealized(PackageApplied * pa)
{
	LOG_TRACE(logAi);
	// Muster produces two ordered client packets (stock, then weekly markers).
	// Only release the AI-side pending guard after those authoritative markers
	// are visible, so a same-turn BuyArmy action cannot race the request.
	clearReplicatedMusters();
	if(status.haveTurn())
	{
		if(pa->packType == CTypeList::getInstance().getTypeID<EndTurn>(nullptr))
		{
			if(pa->result)
				status.madeTurn();
		}
	}

	if(pa->packType == CTypeList::getInstance().getTypeID<QueryReply>(nullptr)
		|| pa->packType == CTypeList::getInstance().getTypeID<HeroMasteryReply>(nullptr))
	{
		status.receivedAnswerConfirmation(pa->requestID, pa->result);
	}
}

void AIGateway::receivedResource()
{
	LOG_TRACE(logAi);
}

void AIGateway::showUniversityWindow(const IMarket * market, const CGHeroInstance * visitor, QueryID queryID)
{
	LOG_TRACE(logAi);

	status.addQuery(queryID, "UniversityWindow");
	executeActionAsync("showUniversityWindow", [this, queryID](){ answerQuery(queryID, 0); });
}

void AIGateway::heroManaPointsChanged(const CGHeroInstance * hero)
{
	LOG_TRACE(logAi);
}

void AIGateway::heroSecondarySkillChanged(const CGHeroInstance * hero, int which, int val)
{
	LOG_TRACE_PARAMS(logAi, "which '%d', val '%d'", which % val);
}

void AIGateway::battleResultsApplied()
{
	LOG_TRACE(logAi);
	assert(status.getBattle() == ENDING_BATTLE);
}

void AIGateway::battleEnded()
{
	LOG_TRACE(logAi);
	assert(status.getBattle() == ENDING_BATTLE);
	status.setBattle(NO_BATTLE);
}

void AIGateway::beforeObjectPropertyChanged(const SetObjectProperty * sop)
{

}


void AIGateway::objectPropertyChanged(const SetObjectProperty * sop)
{
	LOG_TRACE(logAi);
	if(sop->what == ObjProperty::OWNER)
	{
		auto relations = cc->getPlayerRelations(playerID, sop->identifier.as<PlayerColor>());
		auto obj = cc->getObj(sop->id, false);

		if(!nullkiller) // crash protection
			return;

		if(obj)
		{
			if(relations == PlayerRelations::ENEMIES)
			{
				//we want to visit objects owned by oppponents
				//addVisitableObj(obj); // TODO: Remove once save compatibility broken. In past owned objects were removed from this set
				nullkiller->memory->markObjectUnvisited(obj);
			}
			else if(relations == PlayerRelations::SAME_PLAYER && obj->ID == Obj::TOWN)
			{
				// reevaluate defence for a new town
				nullkiller->dangerHitMap->resetHitmap();
			}
		}
	}
}

void AIGateway::buildChanged(const CGTownInstance * town, BuildingID buildingID, int what)
{
	LOG_TRACE_PARAMS(logAi, "what '%i'", what);
}

void AIGateway::heroBonusChanged(const CGHeroInstance * hero, const Bonus & bonus, bool gain)
{
	LOG_TRACE_PARAMS(logAi, "gain '%i'", gain);
}

void AIGateway::showMarketWindow(const IMarket * market, const CGHeroInstance * visitor, QueryID queryID)
{
	LOG_TRACE(logAi);
	status.addQuery(queryID, "MarketWindow");
	executeActionAsync("showMarketWindow", [this, queryID](){ answerQuery(queryID, 0); });
}

void AIGateway::showWorldViewEx(const std::vector<ObjectPosInfo> & objectPositions, bool showTerrain)
{
	//TODO: AI support for ViewXXX spell
	LOG_TRACE(logAi);
}

std::optional<BattleAction> AIGateway::makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState)
{
	LOG_TRACE(logAi);

	if(battleState.ourHero && battleState.ourHero->patrol.patrolling)
	{
		return std::nullopt;
	}

	double ourStrength = battleState.getOurStrength();
	double fightRatio = ourStrength / (double)battleState.getEnemyStrength();

	// if we have no towns - things are already bad, so retreat is not an option.
	if(cc->getTownsInfo().size() && ourStrength < nullkiller->settings->getRetreatThresholdAbsolute() && fightRatio < nullkiller->settings->getRetreatThresholdRelative() && battleState.canFlee)
	{
		return BattleAction::makeRetreat(battleState.ourSide);
	}

	return std::nullopt;
}


void AIGateway::initGameInterface(std::shared_ptr<Environment> env, std::shared_ptr<CCallback> callback)
{
	LOG_TRACE(logAi);
	cbc = callback;
	cc = callback;
	this->env = env;
	playerID = *cc->getPlayerID();
	cc->waitTillRealize = true;

	nullkiller->init(callback, this);
	memorizeVisitableObjs(nullkiller->memory, nullkiller->dangerHitMap, playerID, cc);
}

void AIGateway::yourTurn(QueryID queryID)
{
	LOG_TRACE_PARAMS(logAi, "queryID '%i'", queryID);
	nullkiller->invalidatePathfinderData();
	status.addQuery(queryID, "YourTurn");
	executeActionAsync("yourTurn", [this, queryID](){ answerQuery(queryID, 0); });
	status.startedTurn();

	nullkiller->makingTurnInterruption.reset();

	asyncTasks->run([this]()
	{
		ScopedThreadName guard("NK2AI::AIGateway::makingTurn");
		status.waitTillFree();
		makeTurn();
	});
}

void AIGateway::heroGotMastery(const newHorizonsHeroes::MasteryOffer & offer, QueryID queryID)
{
	status.addQuery(queryID, "Post-Expert mastery choice");
	executeActionAsync("heroGotMastery", [this, offer, queryID]()
	{
		const auto * hero = cc->getHero(offer.hero);
		if(!hero || hero->getOwner() != offer.player)
		{
			logAi->error("Cannot answer mastery query for a missing or transferred hero");
			return;
		}
		const int choice = newHorizonsHeroes::chooseMasteryForArmy(offer, *hero, cc.get());
		logAi->debug("Mastery query %d: army valuation selects %s", queryID, offer.options[choice].id.value);
		cc->chooseHeroMastery(offer.hero, queryID, offer.sequence, choice);
	});
}

void AIGateway::heroGotLevel(const CGHeroInstance * hero, PrimarySkill pskill, std::vector<SecondarySkill> & skills,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks, QueryID queryID)
{
	LOG_TRACE_PARAMS(logAi, "queryID '%i'", queryID);
	status.addQuery(queryID, boost::str(boost::format("Hero %s got level %d") % hero->getNameTextID() % hero->level));
	HeroPtr heroPtr(hero, cc.get());

	executeActionAsync("heroGotLevel", [this, heroPtr, skills, perks, queryID]()
	{
		int sel = 0;

		if(heroPtr.isVerified())
		{
			std::unique_lock lockGuard(nullkiller->aiStateMutex);
			nullkiller->heroManager->update();
			if(!skills.empty())
				sel = nullkiller->heroManager->selectBestSkillIndex(heroPtr, skills);
			if(!perks.empty())
			{
				const auto * observedHero = heroPtr.getUnverified();
				std::vector<size_t> usablePerks;
				if(observedHero)
				{
					for(size_t index = 0; index < perks.size(); ++index)
					{
						const auto & candidate = perks[index];
						try
						{
							const auto definition = newHorizonsHeroes::perkDefinition(
								observedHero->getPerkState().rules,
								candidate.selection.skillId, candidate.selection.perkId);
							// Level-up offers are server-authored, but retain this check at
							// the AI boundary so a stale/legacy planned entry is never
							// deliberately selected by Nullkiller.
							if(definition
								&& definition->effect["status"].String() == "active"
								&& candidate.requiredRank == newHorizonsHeroes::perkRequiredRank(definition->requiredRank)
								&& !observedHero->getPerkState().hasSelection(
									candidate.selection.skillId, candidate.selection.perkId)
								&& observedHero->getPerkSkillRank(candidate.selection.skillId)
									>= newHorizonsHeroes::perkRequiredRank(definition->requiredRank))
								usablePerks.push_back(index);
						}
						catch(const std::exception &)
						{
							// The authoritative query will reject malformed data. Do not
							// turn an invalid candidate into an AI-selected answer.
						}
					}
				}

				if(usablePerks.empty())
					return answerQuery(queryID, skills.empty() ? -1 : sel);

				const auto ownFactionSkill = observedHero
					? newHorizonsHeroes::factionSkill(observedHero->getPrimaryGrowthRules(), observedHero->getFactionID())
					: std::nullopt;
				const auto isFactionPerk = [observedHero, &ownFactionSkill, &perks](size_t index)
				{
					if(!observedHero || !ownFactionSkill)
						return false;
					const int decoded = SecondarySkill::decode(perks[index].selection.skillId);
					return decoded >= 0 && newHorizonsHeroes::isFactionSkillForFaction(
						observedHero->getPrimaryGrowthRules(), observedHero->getFactionID(), SecondarySkill(decoded));
				};
				const auto inspirationalLeaderScore = [observedHero, &perks](size_t index)
				{
					if(!observedHero
						|| perks[index].selection.perkId
							!= "new-horizons:discipline.inspirationalLeader")
						return 0;

					// Inspirational Leader scales with the number of friendly
					// activations that can benefit from a morale trigger. Use the
					// currently visible positive morale and army size as a stable
					// valuation signal instead of selecting every basic perk blindly.
					const int positiveMorale = std::max(0, observedHero->moraleVal());
					const int armyStacks = std::max(1, static_cast<int>(observedHero->Slots().size()));
					return positiveMorale * armyStacks;
				};
				const auto best = std::max_element(usablePerks.begin(), usablePerks.end(),
					[&](size_t leftIndex, size_t rightIndex)
					{
						const auto & left = perks[leftIndex];
						const auto & right = perks[rightIndex];
						return std::tuple{
							isFactionPerk(leftIndex), inspirationalLeaderScore(leftIndex),
							left.requiredRank, left.selection.perkId}
							< std::tuple{
								isFactionPerk(rightIndex), inspirationalLeaderScore(rightIndex),
								right.requiredRank, right.selection.perkId};
					});
				// Advanced/Expert perks are specialized enough to outrank another
				// generic skill roll. Basic perks remain the fallback when no skill
				// choice exists, preserving early-game skill development. A valid
				// faction perk is deliberately preferred even at Basic: it is the
				// hero's faction identity and is the intended early-game path.
				if(skills.empty() || isFactionPerk(*best)
					|| perks[*best].requiredRank >= 2 || inspirationalLeaderScore(*best) > 0)
					sel = static_cast<int>(skills.size() + *best);
			}
		}

		answerQuery(queryID, sel);
	});
}

void AIGateway::commanderGotLevel(const CCommanderInstance * commander, std::vector<ui32> skills, QueryID queryID)
{
	LOG_TRACE_PARAMS(logAi, "queryID '%i'", queryID);
	status.addQuery(queryID, boost::str(boost::format("Commander %s of %s got level %d") % commander->name % commander->getArmy()->nodeName() % (int)commander->level));
	executeActionAsync("commanderGotLevel", [this, queryID](){ answerQuery(queryID, 0); });
}

void AIGateway::showBlockingDialog(const std::string & text, const std::vector<Component> & components, QueryID askID, const int soundID, bool selection, bool cancel, bool safeToAutoaccept)
{
	LOG_TRACE_PARAMS(logAi, "text '%s', askID '%i', soundID '%i', selection '%i', cancel '%i', autoaccept '%i'", text % askID % soundID % selection % cancel % safeToAutoaccept);
	status.addQuery(askID, boost::str(boost::format("Blocking dialog query with %d components - %s")
									  % components.size() % text));

	auto heroPtr = nullkiller->getActiveHero();
	auto target = nullkiller->getTargetTile();

	if(!selection && cancel)
	{
		executeActionAsync("showBlockingDialog", [this, heroPtr, target, components, askID]()
		{
			// Skill-teaching dialogs are decisions, not generic adventure
			// confirmations.  Accept an upgrade when it advances a known skill,
			// or when the hero has a legal slot for a new one; decline an Expert
			// skill (or an unavailable/retired skill) explicitly.
			if(heroPtr.isVerified())
			{
				for(const auto & component : components)
				{
					if(component.type != ComponentType::SEC_SKILL)
						continue;

					const auto skill = component.subType.as<SecondarySkill>();
					const int currentRank = heroPtr->getSecSkillLevel(skill);
					const bool accept = currentRank > MasteryLevel::NONE
						? currentRank < MasteryLevel::EXPERT
						: heroPtr->canLearnSkill(skill);
					answerQuery(askID, accept ? 1 : 0);
					return;
				}
			}

			// yes&no for non-teaching dialogs: retain the existing danger-aware
			// adventure decision.
			bool answer = true;
			auto objects = cc->getVisitableObjs(target);

			if(heroPtr.isVerified() && target.isValid() && !objects.empty())
			{
				auto topObj = objects.front()->id == heroPtr->id ? objects.back() : objects.front();
				auto objType = topObj->ID; // top object should be our hero
				auto goalObjectID = nullkiller->getTargetObject();
				auto danger = nullkiller->dangerEvaluator->evaluateDanger(target, heroPtr.get());
				auto ratio = static_cast<float>(danger) / heroPtr->getTotalStrength();

				answer = true;

				if(topObj->id != goalObjectID && nullkiller->dangerEvaluator->evaluateDanger(topObj) > 0)
				{
					// no if we do not aim to visit this object
					answer = false;
				}

				logAi->trace("Query hook: %s(%s) by %s danger ratio %f", target.toString(), topObj->getObjectNameTextID(), heroPtr.nameOrDefault(), ratio);

				if(cc->getObj(goalObjectID, false))
				{
					logAi->trace("AI expected %s", cc->getObj(goalObjectID, false)->getObjectNameTextID());
				}

				if(objType == Obj::BORDERGUARD || objType == Obj::QUEST_GUARD)
				{
					answer = true;
				}
				else if(objType == Obj::ARTIFACT || objType == Obj::RESOURCE)
				{
					bool dangerUnknown = danger == 0;
					bool dangerTooHigh = ratio * nullkiller->settings->getSafeAttackRatio() > 1;

					answer = !dangerUnknown && !dangerTooHigh;
				}
			}

			answerQuery(askID, answer ? 1 : 0);
		});

		return;
	}

	executeActionAsync("showBlockingDialog", [this, selection, components, heroPtr, askID]()
	{
		int sel = 0;

		if(selection) // select the last component; they are indexed in range [1, size]
			sel = components.size();
		{
				std::unique_lock mxLock(nullkiller->aiStateMutex);

				// TODO: Find better way to understand it is Chest of Treasures
				if(heroPtr.isVerified()
					&& components.size() == 2
					&& components.front().type == ComponentType::RESOURCE
					&& (nullkiller->heroManager->getHeroRoleOrDefault(heroPtr) != HeroRole::MAIN
						|| nullkiller->buildAnalyzer->isGoldPressureOverMax()))
				{
					sel = 1;
				}
		}

		answerQuery(askID, sel);
	});
}

void AIGateway::showTeleportDialog(const CGHeroInstance * hero, TeleportChannelID channel, TTeleportExitsList exits, bool impassable, QueryID askID)
{
	status.addQuery(askID, boost::str(boost::format("Teleport dialog query with %d exits") % exits.size()));

	int chosenExit = -1;
	if(impassable)
	{
		nullkiller->memory->knownTeleportChannels[channel]->passability = TeleportChannel::IMPASSABLE;
	}
	else if(destinationTeleport != ObjectInstanceID() && destinationTeleportPos.isValid())
	{
		auto neededExit = std::make_pair(destinationTeleport, destinationTeleportPos);
		if(destinationTeleport != ObjectInstanceID() && vstd::contains(exits, neededExit))
			chosenExit = vstd::find_pos(exits, neededExit);
	}

	for(auto exit : exits)
	{
		if(status.channelProbing() && exit.first == destinationTeleport)
		{
			chosenExit = vstd::find_pos(exits, exit);
			break;
		}
		else
		{
			// TODO: Implement checking if visiting that teleport will uncovert any FoW
			// So far this is the best option to handle decision about probing
			auto obj = cc->getObj(exit.first, false);
			if(obj == nullptr && !vstd::contains(teleportChannelProbingList, exit.first))
			{
				if(exit.first != destinationTeleport)
					teleportChannelProbingList.push_back(exit.first);
			}
		}
	}

	executeActionAsync("showTeleportDialog", [this, askID, chosenExit]()
	{
		answerQuery(askID, chosenExit);
	});
}

void AIGateway::showGarrisonDialog(const CArmedInstance * up, const CGHeroInstance * down, bool removableUnits, QueryID queryID, const MetaString & customTitle)
{
	LOG_TRACE_PARAMS(logAi, "removableUnits '%i', queryID '%i'", removableUnits % queryID);
	std::string s1 = up->nodeName();
	std::string s2 = down->nodeName();

	status.addQuery(queryID, boost::str(boost::format("Garrison dialog with %s and %s") % s1 % s2));

	//you can't request action from action-response thread
	executeActionAsync("showGarrisonDialog", [this, up, down, removableUnits, queryID]()
	{
		if(removableUnits && up->tempOwner == down->tempOwner && nullkiller->settings->isGarrisonTroopsUsageAllowed() && !cc->getStartInfo()->restrictedGarrisonsForAI())
		{
			pickBestCreatures(down, up);
		}

		answerQuery(queryID, 0);
	});
}

void AIGateway::showMapObjectSelectDialog(QueryID askID, const Component & icon, const MetaString & title, const MetaString & description, const std::vector<ObjectInstanceID> & objects)
{
	status.addQuery(askID, "Map object select query");
	executeActionAsync("showMapObjectSelectDialog", [this, askID](){ answerQuery(askID, selectedObject.getNum()); });
}

bool AIGateway::makePossibleUpgrades(const CArmedInstance * obj)
{
	if(!obj)
		return false;

	bool upgraded = false;

	for(int i = 0; i < GameConstants::ARMY_SIZE; i++)
	{
		if(const CStackInstance * s = obj->getStackPtr(SlotID(i)))
		{
			UpgradeInfo upgradeInfo(s->getId());
			do
			{
				cc->fillUpgradeInfo(obj, SlotID(i), upgradeInfo);

				if(upgradeInfo.hasUpgrades())
				{
					std::vector<CreatureID> viableUpgrades;
					for(const auto upgrade : upgradeInfo.getAvailableUpgrades())
					{
						const auto * hero = dynamic_cast<const CGHeroInstance *>(obj);
						const auto capacity = hero ? hero->getLeadershipSlotCapacity(upgrade) : std::nullopt;
						if(!capacity || capacity->accepts(s->getCount()))
							viableUpgrades.push_back(upgrade);
					}
					if(viableUpgrades.empty())
						break;
					// creature at given slot might have alternative upgrades, pick best one
					CreatureID upgID = *vstd::maxElementByFun(viableUpgrades, [](const CreatureID & id)
						{
							return id.toCreature()->getAIValue();
						});

					int oldValue = s->getCreature()->getAIValue();
					int newValue = upgID.toCreature()->getAIValue();

					if(newValue > oldValue && nullkiller->getFreeResources().canAfford(upgradeInfo.getUpgradeCostsFor(upgID) * s->getCount()))
					{
						cc->upgradeCreature(obj, SlotID(i), upgID);
						upgraded = true;
						logAi->debug("Upgraded %d %s to %s", s->getCount(), upgradeInfo.oldID.toCreature()->getNamePluralTranslated(),
							upgradeInfo.getUpgrade().toCreature()->getNamePluralTranslated());
					}
					else
						break;
				}
			}
			while(upgradeInfo.hasUpgrades());
		}
	}

	return upgraded;
}

void AIGateway::makeTurn()
{
	try
	{
		auto day = cc->getCalendar().getCurrentDay();
		logAi->info("Player %d (%s) starting turn, day %d", playerID, playerID.toString(), day);

		std::shared_lock gsLock(CGameState::mutex);
		cheatMapReveal(nullkiller);
		memorizeVisitableObjs(nullkiller->memory, nullkiller->dangerHitMap, playerID, cc);
		memorizeRevisitableObjs(nullkiller->memory, playerID, cc);

		const auto start = std::chrono::high_resolution_clock::now();
		nullkiller->makeTurn();
		const auto timeElapsedMs = timeElapsed(start);
		if(timeElapsedMs > 5000)
			logAi->warn("PERFORMANCE: NK2 makeTurn took %ld ms", timeElapsedMs);
		else
			logAi->info("PERFORMANCE: NK2 makeTurn took %ld ms", timeElapsedMs);

		for (const auto *h : cc->getHeroesInfo())
		{
			if (h->movementPointsRemaining())
				logAi->warn("Hero %s has %d MP left", h->getNameTextID(), h->movementPointsRemaining());
		}

		endTurn();
	}
	catch (const InterruptionRequestedException &)
	{
		logAi->debug("Making turn thread has been interrupted. We'll end without calling endTurn.");
	}
	catch (const TerminationRequestedException &)
	{
		logAi->debug("Making turn thread has been terminated. We'll end without calling endTurn");
	}
}

void AIGateway::performObjectInteraction(const CGObjectInstance * obj, HeroPtr heroPtr)
{
	LOG_TRACE_PARAMS(logAi, "Hero %s and object %s at %s", heroPtr->getNameTextID() % obj->getObjectNameTextID() % obj->anchorPos().toString());
	switch(obj->ID)
	{
	case Obj::TOWN:
		if(heroPtr->getVisitedTown()) //we are inside, not just attacking
		{
			makePossibleUpgrades(heroPtr.get());

			std::unique_lock lockGuard(nullkiller->aiStateMutex);

			if(!heroPtr->getVisitedTown()->getGarrisonHero() || !nullkiller->isHeroLocked(heroPtr->getVisitedTown()->getGarrisonHero()))
				moveCreaturesToHero(heroPtr->getVisitedTown());

			if(nullkiller->heroManager->getHeroRoleOrDefault(heroPtr) == HeroRole::MAIN && !heroPtr->hasSpellbook()
				&& nullkiller->getFreeGold() >= GameConstants::SPELLBOOK_GOLD_COST)
			{
				if(heroPtr->getVisitedTown()->hasBuilt(BuildingID::MAGES_GUILD_1))
					cc->buyArtifact(heroPtr.get(), ArtifactID::SPELLBOOK);
			}

			const auto * visitedTown = heroPtr->getVisitedTown();
			if(visitedTown && newHorizonsHouseOfWisdom::active(visitedTown, cc->getMagicRules()))
			{
				for(const auto & offer : visitedTown->availableItemsIds(EMarketMode::RESOURCE_SKILL))
				{
					const auto spell = offer.as<SpellID>();
					if(!spell.hasValue())
						continue;
					if(!cc->getResourceAmount().canAfford(newHorizonsHouseOfWisdom::price(spell)))
						continue;
					if(ArtifactUtils::getArtAnyPosition(heroPtr.get(), ArtifactID::SPELL_SCROLL) == ArtifactPosition::PRE_FIRST)
						break;

					// The regular trade request goes through the server validator,
					// just like a human player's House of Wisdom purchase.
					cc->trade(visitedTown->getObjInstanceID(), EMarketMode::RESOURCE_SKILL,
						GameResID(GameResID::GOLD), spell, 1, heroPtr.get());
					break;
				}
			}
		}
		break;
	case Obj::HILL_FORT:
		makePossibleUpgrades(heroPtr.get());
		break;
	}
}

void AIGateway::moveCreaturesToHero(const CGTownInstance * t)
{
	if(t->getVisitingHero() && t->armedGarrison() && t->getVisitingHero()->tempOwner == t->tempOwner)
	{
		pickBestCreatures(t->getVisitingHero(), t->getUpperArmy());
	}
}

bool AIGateway::hasPendingMuster(const CGHeroInstance * hero, const CGTownInstance * town) const
{
	if(!hero || !town)
		return false;

	// The marker packets can arrive after requestRealized.  Reconcile before
	// inspecting the guard so a completed Muster never blocks the next town
	// action indefinitely.
	const_cast<AIGateway *>(this)->clearReplicatedMusters();

	const auto & calendar = cc->getCalendar();
	const int currentWeek = ::newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	std::lock_guard lock(musterMutex);

	// A failed or delayed request must not permanently suppress the hero after
	// the weekly reset.  Stale entries are harmless, but pruning here also keeps
	// the guard bounded during long AI games.
	std::erase_if(pendingMusters, [currentWeek](const PendingMuster & pending)
	{
		return pending.week != currentWeek;
	});

	return std::any_of(pendingMusters.begin(), pendingMusters.end(), [&](const PendingMuster & pending)
	{
		return pending.hero == hero->id && pending.target == town->id && pending.week == currentWeek;
	});
}

bool AIGateway::hasPendingMuster(const CGTownInstance * town) const
{
	if(!town)
		return false;

	const_cast<AIGateway *>(this)->clearReplicatedMusters();
	const auto & calendar = cc->getCalendar();
	const int currentWeek = ::newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	std::lock_guard lock(musterMutex);
	return std::any_of(pendingMusters.begin(), pendingMusters.end(), [&](const PendingMuster & pending)
	{
		return pending.target == town->id && pending.week == currentWeek;
	});
}

void AIGateway::clearReplicatedMusters()
{
	const auto & calendar = cc->getCalendar();
	const int currentWeek = ::newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	std::lock_guard lock(musterMutex);
	std::erase_if(pendingMusters, [&](const PendingMuster & pending)
	{
		if(pending.week != currentWeek)
			return true;

		const auto * hero = cc->getHero(pending.hero);
		const auto * target = dynamic_cast<const CGDwelling *>(cc->getObj(pending.target));
		return hero && target && hero->hasUsedNewHorizonsMuster(currentWeek)
			&& target->getNewHorizonsMusterLastWeek() == currentWeek;
	});
}

void AIGateway::tryMusterCreatures(const CGHeroInstance * hero, const CGTownInstance * town)
{
	if(!hero || !town || hero->tempOwner != playerID || town->tempOwner != playerID)
		return;

	const int recruitmentRank = hero->getPerkSkillRank(std::string(::newHorizonsMuster::RECRUITMENT_SKILL));
	const auto & calendar = cc->getCalendar();
	const int currentWeek = ::newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	::newHorizonsMuster::PerkModifiers modifiers;
	modifiers.volunteerNetwork = hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(::newHorizonsMuster::VOLUNTEER_NETWORK_PERK));
	modifiers.eliteDraft = hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(::newHorizonsMuster::ELITE_DRAFT_PERK));
	modifiers.championsCall = hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(::newHorizonsMuster::CHAMPIONS_CALL_PERK));
	modifiers.masterRecruiter = hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(::newHorizonsMuster::MASTER_RECRUITER_PERK));
	if(recruitmentRank <= 0 || hasPendingMuster(hero, town)
		|| hero->getNewHorizonsMusterUsesThisWeek(currentWeek) >= ::newHorizonsMuster::maximumUsesPerWeek(modifiers)
		|| town->getNewHorizonsMusterLastWeek() == currentWeek)
		return;

	const auto candidate = newHorizonsMuster::chooseTownCandidate(*town, *cc, recruitmentRank, modifiers);
	if(!candidate)
		return; // No category snapshot means legacy/no-category; do not Muster.

	{
		std::lock_guard lock(musterMutex);
		const auto duplicate = std::any_of(pendingMusters.begin(), pendingMusters.end(), [&](const PendingMuster & pending)
		{
			return pending.hero == hero->id && pending.target == town->id && pending.week == currentWeek;
		});
		if(duplicate)
			return;
		pendingMusters.push_back({hero->id, town->id, currentWeek});
	}

	logAi->debug("Hero %s musters %d %s from town %s (rank %d, category amount %d, weighted army value %lld)",
		hero->getNameTextID(), candidate->amount, candidate->creature.toCreature()->getNamePluralTranslated(),
		town->getNameTextID(), recruitmentRank, candidate->amount, static_cast<long long>(candidate->armyValue));

	// The callback is the only legal mutation path.  The server validates both
	// weekly uses and the target row; this AI-side pending guard prevents a
	// second request before the resulting pool update is replicated.
	cc->musterCreatures(hero, town, candidate->creature);
}

void AIGateway::swapGarrisonHero(const CGTownInstance * town)
{
	if(!armyFormation::canSwapGarrisonHero(town))
		throw cannotFulfillGoalException("Cannot swap garrison hero: the destination army would exceed its Leadership limits.");
	cc->swapGarrisonHero(town);
}

void AIGateway::pickBestCreatures(const CArmedInstance * destinationArmy, const CArmedInstance * source)
{
	if(source->stacksCount() == 0)
		return;

	const auto * receiverHero = dynamic_cast<const CGHeroInstance *>(destinationArmy);
	const auto * sourceHero = dynamic_cast<const CGHeroInstance *>(source);
	if(armyFormation::hasLeadershipCapacityRules(receiverHero, destinationArmy)
		|| armyFormation::hasLeadershipCapacityRules(receiverHero, source)
		|| armyFormation::hasLeadershipCapacityRules(sourceHero, destinationArmy)
		|| armyFormation::hasLeadershipCapacityRules(sourceHero, source))
	{
		armyFormation::ArmyExchangeProjection projection;
		nullkiller->armyManager->getBestArmy(
			destinationArmy, destinationArmy, source,
			cc->getTile(source->visitablePos())->getTerrainID(), sourceHero, &projection);
		const bool previousWaitTillRealize = cc->waitTillRealize;
		cc->waitTillRealize = true;
		auto restoreWaitTillRealize = vstd::makeScopeGuard([callback = cc, previousWaitTillRealize]()
		{
			callback->waitTillRealize = previousWaitTillRealize;
		});

		auto stackMatches = [](const CArmedInstance * army, SlotID slot, CreatureID creature, int count)
		{
			const auto * stack = army->getStackPtr(slot);
			if(count == 0)
				return stack == nullptr;
			return stack && stack->getCreatureID() == creature && stack->getCount() == count;
		};

		for(const auto & transfer : projection.transfers)
		{
			const bool fromReceiver = transfer.sourceSide == armyFormation::ArmyExchangeSide::RECEIVER;
			const auto * fromArmy = fromReceiver ? destinationArmy : source;
			const auto * toArmy = fromReceiver ? source : destinationArmy;
			if(!stackMatches(fromArmy, transfer.sourceSlot, transfer.expectedSourceCreature, transfer.expectedSourceCount)
				|| !stackMatches(toArmy, transfer.destinationSlot,
					transfer.expectedDestinationCreature, transfer.expectedDestinationCount))
			{
				logAi->warn("Stopping projected army exchange after stack state changed at slots %d and %d",
					transfer.sourceSlot.getNum(), transfer.destinationSlot.getNum());
				break;
			}

			if(transfer.swapsStacks || transfer.transferCount == transfer.expectedSourceCount)
			{
				cc->mergeOrSwapStacks(fromArmy, toArmy, transfer.sourceSlot, transfer.destinationSlot);
			}
			else
			{
				const auto * destinationStack = toArmy->getStackPtr(transfer.destinationSlot);
				if(destinationStack && destinationStack->getCreatureID() != transfer.expectedSourceCreature)
				{
					logAi->warn("Stopping projected army exchange before partial transfer to an occupied different stack");
					break;
				}
				cc->splitStack(fromArmy, toArmy, transfer.sourceSlot, transfer.destinationSlot,
					transfer.resultingDestinationCount);
			}

			const int remainingSourceCount = transfer.expectedSourceCount - transfer.transferCount;
			const bool postStateMatches = transfer.swapsStacks
				? stackMatches(fromArmy, transfer.sourceSlot,
					transfer.expectedDestinationCreature, transfer.expectedDestinationCount)
					&& stackMatches(toArmy, transfer.destinationSlot,
						transfer.expectedSourceCreature, transfer.expectedSourceCount)
				: stackMatches(fromArmy, transfer.sourceSlot,
					remainingSourceCount > 0 ? transfer.expectedSourceCreature : CreatureID::NONE,
					remainingSourceCount)
					&& stackMatches(toArmy, transfer.destinationSlot,
						transfer.expectedSourceCreature, transfer.resultingDestinationCount);
			if(!postStateMatches)
			{
				logAi->warn("Stopping projected army exchange because the server did not apply the planned stack change");
				break;
			}
		}

		return;
	}

	const CArmedInstance * armies[] = {destinationArmy, source};
	auto arrangeStack = [this](const CArmedInstance * sourceArmy, const CArmedInstance * destinationArmy,
		SlotID sourceSlot, SlotID destinationSlot)
	{
		const auto * sourceStack = sourceArmy->getStackPtr(sourceSlot);
		if(!sourceStack)
			return;

		const int transferCount = armyFormation::maxLegalTransferCount(
			sourceArmy, destinationArmy, sourceSlot, destinationSlot);
		if(transferCount <= 0)
			return;

		const auto * destinationStack = destinationArmy->getStackPtr(destinationSlot);
		const int existingCount = destinationStack ? destinationStack->getCount() : 0;
		if(transferCount == sourceStack->getCount())
		{
			if(armyFormation::canMergeOrSwapStacks(sourceArmy, destinationArmy, sourceSlot, destinationSlot))
				cc->mergeOrSwapStacks(sourceArmy, destinationArmy, sourceSlot, destinationSlot);
			return;
		}

		// A partial transfer is legal only into an empty slot or a stack of the
		// same creature.  Different occupied stacks require a whole-stack swap;
		// leave that excess in place when the destination cap is smaller.
		if(destinationStack && destinationStack->getCreatureID() != sourceStack->getCreatureID())
			return;

		const int resultingDestinationCount = existingCount + transferCount;
		if(armyFormation::canSplitStack(
			sourceArmy, destinationArmy, sourceSlot, destinationSlot, resultingDestinationCount))
			cc->splitStack(sourceArmy, destinationArmy, sourceSlot, destinationSlot, resultingDestinationCount);
	};

	auto bestArmy = nullkiller->armyManager->getBestArmy(destinationArmy, destinationArmy, source, cc->getTile(source->visitablePos())->getTerrainID());

	for(auto army : armies)
	{
		// move first stack at first slot if empty to avoid can not take away last creature
		if(!army->hasStackAtSlot(SlotID(0)) && army->stacksCount() > 0)
		{
			const auto sourceSlot = army->Slots().begin()->first;
			arrangeStack(army, army, sourceSlot, SlotID(0));
		}
	}

	//foreach best type -> iterate over slots in both armies and if it's the appropriate type, send it to the slot where it belongs
	for(SlotID i = SlotID(0); i.validSlot(); i.advance(1)) //i-th strongest creature type will go to i-th slot
	{
		if(i.getNum() >= bestArmy.size())
		{
			if(destinationArmy->hasStackAtSlot(i))
			{
				auto creature = destinationArmy->getCreature(i);
				auto targetSlot = source->getSlotFor(creature);

				if(targetSlot.validSlot())
				{
					// remove unwanted creatures
					arrangeStack(destinationArmy, source, i, targetSlot);
				}
				else if(destinationArmy->getStack(i).getPower() < destinationArmy->getArmyStrength() / 100)
				{
					// dismiss creatures if the amount is small
					cc->dismissCreature(destinationArmy, i);
				}
			}

			continue;
		}

		const CCreature * targetCreature = bestArmy[i.getNum()].creature;

		for(auto armyPtr : armies)
		{
			for(SlotID j = SlotID(0); j.validSlot(); j.advance(1))
			{
				if(armyPtr->getCreature(j) == targetCreature && (i != j || armyPtr != destinationArmy)) //it's a searched creature not in dst SLOT
				{
					arrangeStack(armyPtr, destinationArmy, j, i);
				}
			}
		}
	}

	//TODO - having now strongest possible army, we may want to think about arranging stacks
}

void AIGateway::recruitCreatures(const CGDwelling * d, const CArmedInstance * recruiter)
{
	//now used only for visited dwellings / towns, not BuyArmy goal
	if(const auto * town = dynamic_cast<const CGTownInstance *>(d))
	{
		if(hasPendingMuster(town))
		{
			logAi->debug("Deferring recruitment from town %s until its Muster result is replicated", town->getNameTextID());
			return;
		}
	}
	for(int i = 0; i < d->creatures.size(); i++)
	{
		if(!d->creatures[i].second.size())
			continue;

		int count = d->creatures[i].first;
		CreatureID creID = d->creatures[i].second.back();

		if(!recruiter->getSlotFor(creID).validSlot())
		{
			bool freedSlot = false;
			for(const auto & stack : recruiter->Slots())
			{
				if(!stack.second->getType())
					continue;

				auto duplicatingSlot = recruiter->getSlotFor(stack.second->getCreature());

				if(duplicatingSlot != stack.first
					&& armyFormation::canMergeOrSwapStacks(recruiter, recruiter, stack.first, duplicatingSlot))
				{
					cc->mergeStacks(recruiter, recruiter, stack.first, duplicatingSlot);
					freedSlot = true;
					break;
				}
			}

			if(!freedSlot || !recruiter->getSlotFor(creID).validSlot())
			{
				continue;
			}
		}

		vstd::amin(count, cc->getResourceAmount() / creID.toCreature()->getFullRecruitCost());
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(recruiter))
		{
			if(const auto capacity = hero->getLeadershipSlotCapacity(creID))
			{
				const auto slot = recruiter->getSlotFor(creID);
				const int alreadyPresent = slot.validSlot() ? recruiter->getStackCount(slot) : 0;
				vstd::amin(count, std::max(0, capacity->maximum - alreadyPresent));
			}
		}
		if(count > 0)
			cc->recruitCreatures(d, recruiter, creID, count, i);
	}
}

void AIGateway::battleStart(const BattleID & battleID, const CCreatureSet * army1, const CCreatureSet * army2, int3 tile, const CGHeroInstance * hero1, const CGHeroInstance * hero2, BattleSide side, bool replayAllowed)
{
	assert(!playerID.isValidPlayer() || status.getBattle() == UPCOMING_BATTLE);
	status.setBattle(ONGOING_BATTLE);
	const CGObjectInstance * presumedEnemy = vstd::backOrNull(cc->getVisitableObjs(tile)); //may be nullptr in some very are cases -> eg. visited monolith and fighting with an enemy at the FoW covered exit
	battlename = boost::str(boost::format("Starting battle of %s attacking %s at %s") % (hero1 ? hero1->getNameTextID() : "a army") % (presumedEnemy ? presumedEnemy->getObjectNameTextID() : "unknown enemy") % tile.toString());
	CAdventureAI::battleStart(battleID, army1, army2, tile, hero1, hero2, side, replayAllowed);
}

void AIGateway::battleEnd(const BattleID & battleID, const BattleResult * br, QueryID queryID)
{
	assert(status.getBattle() == ONGOING_BATTLE);
	status.setBattle(ENDING_BATTLE);
	bool won = br->winner == cc->getBattle(battleID)->battleGetMySide();
	logAi->debug("Player %d (%s): I %s the %s!", playerID, playerID.toString(), (won ? "won" : "lost"), battlename);
	battlename.clear();

	CAdventureAI::battleEnd(battleID, br, queryID);

	// gosolo
	if(queryID != QueryID::NONE && cc->getPlayerState(playerID)->isHuman())
	{
		status.addQuery(queryID, "Confirm battle query");

		executeActionAsync("battleEnd", [this, queryID]()
		{
			answerQuery(queryID, 0);
		});
	}
}

void AIGateway::waitTillFree()
{
	auto unlock = vstd::makeUnlockSharedGuard(CGameState::mutex);
	status.waitTillFree();
}

std::vector<const CGObjectInstance *> AIGateway::getFlaggedObjects() const
{
	std::vector<const CGObjectInstance *> ret;
	for(const ObjectInstanceID objId : nullkiller->memory->visitableObjs)
	{
		const CGObjectInstance * obj = cc->getObjInstance(objId);
		if(obj && obj->tempOwner == playerID)
			ret.push_back(obj);
	}
	return ret;
}

bool AIGateway::moveHeroToTile(const int3 dst, const HeroPtr & heroPtr)
{
	if(!heroPtr.isVerified())
		throw cannotFulfillGoalException("Hero was lost!");

	if(heroPtr->isGarrisoned() && heroPtr->getVisitedTown())
	{
		swapGarrisonHero(heroPtr->getVisitedTown());
		moveCreaturesToHero(heroPtr->getVisitedTown());
	}

	//TODO: consider if blockVisit objects change something in our checks: AIUtility::isBlockVisitObj()

	auto afterMovementCheck = [&]() -> void
	{
		waitTillFree(); //movement may cause battle or blocking dialog
		if(!heroPtr.isVerified())
		{
			lostHero(heroPtr);
			teleportChannelProbingList.clear();
			if(status.channelProbing()) // if hero lost during channel probing we need to switch this mode off
				status.setChannelProbing(false);
			throw cannotFulfillGoalException("Hero was lost!");
		}
	};

	logAi->debug("Moving hero %s to tile %s", heroPtr->getNameTextID(), dst.toString());
	int3 startHpos = heroPtr->visitablePos();
	bool ret = false;
	if(startHpos == dst)
	{
		//FIXME: this assertion fails also if AI moves onto defeated guarded object
		//assert(cb->getVisitableObjs(dst).size() > 1); //there's no point in revisiting tile where there is no visitable object
		cc->moveHero(*heroPtr, heroPtr->convertFromVisitablePos(dst), false);
		afterMovementCheck(); // TODO: is it feasible to hero get killed there if game work properly?
		// If revisiting, teleport probing is never done, and so the entries into the list would remain unused and uncleared
		teleportChannelProbingList.clear();
		// not sure if AI can currently reconsider to attack bank while staying on it. Check issue 2084 on mantis for more information.
		ret = true;
	}
	else
	{
		CGPath path;
		nullkiller->getPathsInfo(heroPtr.get())->getPath(path, dst);
		if(path.nodes.empty())
		{
			logAi->error("Hero %s cannot reach %s from %s with %d movement points.",
				heroPtr->getNameTextID(), dst.toString(), startHpos.toString(), heroPtr->movementPointsRemaining());
			throw cannotFulfillGoalException("No live path to movement target!");
		}
		int i = (int)path.nodes.size() - 1;

		auto getObj = [&](int3 coord, bool ignoreHero)
		{
			auto tile = cc->getTile(coord, false);
			assert(tile);
			return cc->getObj(tile->topVisitableObj(ignoreHero), false);
		};

		auto isTeleportAction = [&](EPathNodeAction action) -> bool
		{
			if(action != EPathNodeAction::TELEPORT_NORMAL && action != EPathNodeAction::TELEPORT_BLOCKING_VISIT)
			{
				if(action != EPathNodeAction::TELEPORT_BATTLE)
				{
					return false;
				}
			}

			return true;
		};

		auto getDestTeleportObj = [&](const CGObjectInstance * currentObject, const CGObjectInstance * nextObjectTop, const CGObjectInstance * nextObject) -> const CGObjectInstance *
		{
			if(CGTeleport::isConnected(currentObject, nextObjectTop))
				return nextObjectTop;
			if(nextObjectTop && nextObjectTop->ID == Obj::HERO)
			{
				if(CGTeleport::isConnected(currentObject, nextObject))
					return nextObject;
			}

			return nullptr;
		};

		auto doMovement = [&](int3 dst, bool transit, const EPathfindingLayer & layer)
		{
			cc->moveHero(*heroPtr, heroPtr->convertFromVisitablePos(dst), transit, layer);
		};

		auto doTeleportMovement = [&](ObjectInstanceID exitId, int3 exitPos)
		{
			if(cc->getObj(exitId) && cc->getObj(exitId)->ID == Obj::WHIRLPOOL)
			{
				nullkiller->armyFormation->rearrangeArmyForWhirlpool(*heroPtr);
			}

			destinationTeleport = exitId;
			if(exitPos.isValid())
				destinationTeleportPos = exitPos;
			cc->moveHero(*heroPtr, heroPtr->pos, false);
			destinationTeleport = ObjectInstanceID();
			destinationTeleportPos = int3(-1);
			afterMovementCheck();
		};

		auto doChannelProbing = [&]() -> void
		{
			auto currentPos = heroPtr->visitablePos();
			auto currentTeleport = getObj(currentPos, true);

			if(currentTeleport)
			{
				auto currentExit = currentTeleport->id;

				status.setChannelProbing(true);
				for(auto exit : teleportChannelProbingList)
					doTeleportMovement(exit, int3(-1));

				teleportChannelProbingList.clear();
				status.setChannelProbing(false);

				doTeleportMovement(currentExit, currentPos);
			}
			else
			{
				logAi->debug("Unexpected channel probbing at " + currentPos.toString());

				teleportChannelProbingList.clear();
				status.setChannelProbing(false);
			}
		};

		teleportChannelProbingList.clear();
		status.setChannelProbing(false);

		for(; i > 0; i--)
		{
			int3 currentCoord = path.nodes[i].coord;
			const auto & nextNode = path.nodes[i - 1];
			int3 nextCoord = nextNode.coord;

			auto currentObject = getObj(currentCoord, currentCoord == heroPtr->visitablePos());
			auto nextObjectTop = getObj(nextCoord, false);
			auto nextObject = getObj(nextCoord, true);
			auto destTeleportObj = getDestTeleportObj(currentObject, nextObjectTop, nextObject);
			if(isTeleportAction(nextNode.action) && destTeleportObj != nullptr)
			{
				//we use special login if hero standing on teleporter it's mean we need
				doTeleportMovement(destTeleportObj->id, nextCoord);
				if(teleportChannelProbingList.size())
					doChannelProbing();
				nullkiller->memory->markObjectVisited(destTeleportObj); //FIXME: Monoliths are not correctly visited

				continue;
			}

			//stop sending move requests if the next node can't be reached at the current turn (hero exhausted his move points)
			if(nextNode.turns)
			{
				//blockedHeroes.insert(h); //to avoid attempts of moving heroes with very little MPs
				return false;
			}

			if(nextCoord == heroPtr->visitablePos())
				continue;

			bool isConnected = false;
			bool isNextObjectTeleport = false;
			// Check there is node after next one; otherwise transit is pointless
			if(i - 2 >= 0)
			{
				isConnected = CGTeleport::isConnected(nextObjectTop, getObj(path.nodes[i - 2].coord, false));
				isNextObjectTeleport = CGTeleport::isTeleport(nextObjectTop);
			}
			if(isConnected || isNextObjectTeleport || nextNode.layer == EPathfindingLayer::AIR)
				// Hero should be able to go through object if it's allow transit
				doMovement(nextCoord, true, nextNode.layer);
			else
				doMovement(nextCoord, false, nextNode.layer);

			afterMovementCheck();

			if(teleportChannelProbingList.size())
				doChannelProbing();
		}

		if(path.nodes[0].action == EPathNodeAction::BLOCKING_VISIT || path.nodes[0].action == EPathNodeAction::BATTLE)
		{
			// when we take resource we do not reach its position. We even might not move
			// also guarded town is not get visited automatically after capturing
			ret = heroPtr.isVerified() && i == 0;
		}
	}
	if(heroPtr.isVerified())
	{
		if(auto visitedObject = vstd::frontOrNull(cc->getVisitableObjs(heroPtr->visitablePos()))) //we stand on something interesting
		{
			if(visitedObject != *heroPtr)
			{
				performObjectInteraction(visitedObject, heroPtr);
				ret = true;
			}
		}
	}
	if(heroPtr.isVerified()) //we could have lost hero after last move
	{
		ret = ret || (dst == heroPtr->visitablePos());

		if(startHpos == heroPtr->visitablePos() && !ret) //we didn't move and didn't reach the target
		{
			throw cannotFulfillGoalException("Invalid path found!");
		}

		logAi->debug("Hero %s moved from %s to %s. Returning %d.", heroPtr->getNameTextID(), startHpos.toString(), heroPtr->visitablePos().toString(), ret);
	}
	return ret;
}

void AIGateway::buildStructure(const CGTownInstance * t, BuildingID building)
{
	auto name = t->getTown()->buildings.at(building)->getNameTranslated();
	logAi->debug("Player %d will build %s in town of %s at %s", playerID, name, t->getNameTextID(), t->anchorPos().toString());
	cc->buildBuilding(t, building); //just do this;
}

void AIGateway::tryRealize(Goals::DigAtTile & g)
{
	assert(g.hero->visitablePos() == g.tile); //surely we want to crash here?
	if(g.hero->diggingStatus() == EDiggingStatus::CAN_DIG)
	{
		cc->dig(g.hero);
	}
	else
	{
		throw cannotFulfillGoalException("A hero can't dig!\n");
	}
}

void AIGateway::tryRealize(Goals::Trade & g) //trade
{
	if(cc->getResourceAmount(GameResID(g.resID)) >= g.value) //goal is already fulfilled. Why we need this check, anyway?
		throw goalFulfilledException(sptr(g));

	int acquiredResources = 0;
	if(const CGObjectInstance * obj = cc->getObj(ObjectInstanceID(g.objid), false))
	{
		if(const auto * m = dynamic_cast<const IMarket*>(obj))
		{
			auto freeRes = cc->getResourceAmount(); //trade only resources which are not reserved
			for(auto it = ResourceSet::nziterator(freeRes); it.valid(); it++)
			{
				auto res = it->resType;
				if(res.getNum() == g.resID) //sell any other resource
					continue;

				int toGive;
				int toGet;
				m->getOffer(res.getNum(), g.resID, toGive, toGet, EMarketMode::RESOURCE_RESOURCE);
				toGive = static_cast<int>(toGive * (it->resVal / toGive)); //round down
				//TODO trade only as much as needed
				if (toGive) //don't try to sell 0 resources
				{
					cc->trade(m->getObjInstanceID(), EMarketMode::RESOURCE_RESOURCE, res, GameResID(g.resID), toGive);
					acquiredResources = static_cast<int>(toGet * (it->resVal / toGive));
					logAi->debug("Traded %d of %s for %d of %s at %s", toGive, res, acquiredResources, g.resID, obj->getObjectNameTextID());
				}
				if (cc->getResourceAmount(GameResID(g.resID)))
					throw goalFulfilledException(sptr(g)); //we traded all we needed
			}

			throw cannotFulfillGoalException("I cannot get needed resources by trade!");
		}
		else
		{
			throw cannotFulfillGoalException("I don't know how to use this object to raise resources!");
		}
	}
	else
	{
		throw cannotFulfillGoalException("No object that could be used to raise resources!");
	}
}

void AIGateway::endTurn()
{
	logAi->info("Player %d (%s) ends turn", playerID, playerID.toString());
	if(!status.haveTurn())
	{
		logAi->error("Not having turn at the end of turn???");
	}

	logAi->debug("Resources at the end of turn: %s", cc->getResourceAmount().toString());

	if(cc->getPlayerStatus(playerID) != EPlayerStatus::INGAME)
	{
		logAi->info("Ending turn is not needed because we already lost");
		return;
	}

	do
	{
		cc->endTurn();
	}
	while(status.haveTurn()); //for some reasons, our request may fail -> stop requesting end of turn only after we've received a confirmation that it's over

	logGlobal->info("Player %d (%s) ended turn", playerID, playerID.toString());
}

void AIGateway::buildArmyIn(const CGTownInstance * t)
{
	const auto * recruitmentHero = t
		? (t->getVisitingHero() ? t->getVisitingHero() : t->getGarrisonHero())
		: nullptr;
	tryMusterCreatures(recruitmentHero, t);
	makePossibleUpgrades(t->getVisitingHero());
	makePossibleUpgrades(t);
	recruitCreatures(t, t->getUpperArmy());
	moveCreaturesToHero(t);
}

void AIGateway::finish()
{
	nullkiller->makingTurnInterruption.interruptThread();

	if (asyncTasks)
	{
		asyncTasks->wait();
		asyncTasks.reset();
	}
}

void AIGateway::executeActionAsync(const std::string & description, const std::function<void()> & whatToDo)
{
	if (!asyncTasks)
		throw std::runtime_error("Attempt to execute task on shut down AI state!");

	asyncTasks->run([description, whatToDo]() noexcept
	{
		ScopedThreadName guard("NK2AI::AIGateway::" + description);
		std::shared_lock gsLock(CGameState::mutex);
		try
		{
			whatToDo();
		}
		catch (const TerminationRequestedException &)
		{
			logAi->debug("%s thread has been terminated. We'll end it immediately", description);
		}
	});
}

void AIGateway::lostHero(const HeroPtr & heroPtr) const
{
	logAi->debug("I lost my hero %s. It's best to forget and move on.", heroPtr.nameOrDefault());
	nullkiller->invalidatePathfinderData();
}

void AIGateway::answerQuery(const QueryID queryID, const int selection) const
{
	logAi->debug("I'll answer the query %d giving the choice %d", queryID, selection);
	if(queryID != QueryID(-1))
	{
		cc->selectionMade(selection, queryID);
	}
	else
	{
		logAi->debug("Since the query ID is %d, the answer won't be sent. This is not a real query!", queryID);
		//do nothing
	}
}

void AIGateway::requestSent(const CPackForServer * pack, int requestID)
{
	//BNLOG("I have sent request of type %s", typeid(*pack).name());
	if(auto reply = dynamic_cast<const QueryReply *>(pack))
	{
		status.attemptedAnsweringQuery(reply->qid, requestID);
	}
	if(auto reply = dynamic_cast<const HeroMasteryReply *>(pack))
		status.attemptedAnsweringQuery(reply->qid, requestID);
}

std::string AIGateway::getBattleAIName() const
{
	if(settings["ai"]["combatEnemyAI"].getType() == JsonNode::JsonType::DATA_STRING)
		return settings["ai"]["combatEnemyAI"].String();
	else
		return "BattleAI";
}

void AIGateway::validateObject(const CGObjectInstance * obj)
{
	validateObject(ObjectIdRef(obj->id, cc.get()));
}

void AIGateway::validateObject(ObjectIdRef obj)
{
	if(!obj)
	{
		nullkiller->memory->removeFromMemory(obj);
	}
}

AIStatus::AIStatus(AIGateway * aiGw)
	: aiGw(aiGw)
{
	battle = NO_BATTLE;
	havingTurn = false;
	ongoingHeroMovement = false;
	ongoingChannelProbing = false;
}

AIStatus::~AIStatus()
{

}

void AIStatus::setBattle(BattleState BS)
{
	std::unique_lock<std::mutex> lock(mx);
	LOG_TRACE_PARAMS(logAi, "battle state=%d", (int)BS);
	battle = BS;
	cv.notify_all();
}

BattleState AIStatus::getBattle()
{
	std::unique_lock<std::mutex> lock(mx);
	return battle;
}

void AIStatus::addQuery(QueryID ID, std::string description)
{
	if(ID == QueryID(-1))
	{
		logAi->debug("The \"query\" has an id %d, it'll be ignored as non-query. Description: %s", ID, description);
		return;
	}

	assert(ID.getNum() >= 0);
	std::unique_lock<std::mutex> lock(mx);

	assert(!vstd::contains(remainingQueries, ID));

	remainingQueries[ID] = description;

	cv.notify_all();
	logAi->debug("Adding query %d - %s. Total queries count: %d", ID, description, remainingQueries.size());
}

void AIStatus::removeQuery(QueryID ID)
{
	assert(vstd::contains(remainingQueries, ID));

	std::string description = remainingQueries[ID];
	remainingQueries.erase(ID);

	cv.notify_all();
	logAi->debug("Removing query %d - %s. Total queries count: %d", ID, description, remainingQueries.size());
}

int AIStatus::getQueriesCount()
{
	std::unique_lock<std::mutex> lock(mx);
	return static_cast<int>(remainingQueries.size());
}

void AIStatus::startedTurn()
{
	std::unique_lock<std::mutex> lock(mx);
	havingTurn = true;
	cv.notify_all();
}

void AIStatus::madeTurn()
{
	std::unique_lock<std::mutex> lock(mx);
	havingTurn = false;
	cv.notify_all();
}

void AIStatus::waitTillFree()
{
	std::unique_lock<std::mutex> lock(mx);
	while(battle != NO_BATTLE || !remainingQueries.empty() || !objectsBeingVisited.empty() || ongoingHeroMovement)
	{
		cv.wait_for(lock, std::chrono::milliseconds(10));
		aiGw->nullkiller->makingTurnInterruption.interruptionPoint();
	}
}

bool AIStatus::haveTurn()
{
	std::unique_lock<std::mutex> lock(mx);
	return havingTurn;
}

void AIStatus::attemptedAnsweringQuery(QueryID queryID, int answerRequestID)
{
	std::unique_lock<std::mutex> lock(mx);
	assert(vstd::contains(remainingQueries, queryID));
	std::string description = remainingQueries[queryID];
	logAi->debug("Attempted answering query %d - %s. Request id=%d. Waiting for results...", queryID, description, answerRequestID);
	requestToQueryID[answerRequestID] = queryID;
}

void AIStatus::receivedAnswerConfirmation(int answerRequestID, int result)
{
	std::unique_lock<std::mutex> lock(mx);
	assert(vstd::contains(requestToQueryID, answerRequestID));
	QueryID query = requestToQueryID[answerRequestID];
	assert(vstd::contains(remainingQueries, query));
	requestToQueryID.erase(answerRequestID);

	if(result)
	{
		removeQuery(query);
	}
	else
	{
		logAi->error("Something went really wrong, failed to answer query %d : %s", query.getNum(), remainingQueries[query]);
		//TODO safely retry
	}
}

void AIStatus::heroVisit(const CGObjectInstance * obj, bool started)
{
	std::unique_lock<std::mutex> lock(mx);
	if(started)
	{
		objectsBeingVisited.push_back(obj);
	}
	else
	{
		// There can be more than one object visited at the time (eg. hero visits Subterranean Gate
		// causing visit to hero on the other side.
		// However, we are guaranteed that start/end visit notification maintain stack order.
		assert(!objectsBeingVisited.empty());
		objectsBeingVisited.pop_back();
	}
	cv.notify_all();
}

void AIStatus::setMove(bool ongoing)
{
	std::unique_lock<std::mutex> lock(mx);
	ongoingHeroMovement = ongoing;
	cv.notify_all();
}

void AIStatus::setChannelProbing(bool ongoing)
{
	std::unique_lock<std::mutex> lock(mx);
	ongoingChannelProbing = ongoing;
	cv.notify_all();
}

bool AIStatus::channelProbing()
{
	return ongoingChannelProbing;
}

void AIGateway::invalidatePaths()
{
	nullkiller->invalidatePaths();
}

std::string AIGateway::heroRoleDebugText(const CGHeroInstance * hero) const
{
	// Reuse GatherArmyBehavior's baseline: 500 minimum strength and 10% of the receiving army.
	static constexpr uint64_t MIN_REINFORCEMENT_ARMY_STRENGTH = 500;
	static constexpr uint64_t REINFORCEMENT_ARMY_STRENGTH_DIVISOR = 10;
	std::unique_lock lockGuard(nullkiller->aiStateMutex, std::try_to_lock);
	if(!lockGuard.owns_lock())
		return {};

	const auto role = nullkiller->heroManager->getHeroRoleOrDefaultInefficient(hero);
	const auto armyStrength = hero->getArmyStrength();
	uint64_t mainArmyStrength = armyStrength;
	bool isMainArmy = true;
	for(const auto * otherHero : cc->getHeroesInfo())
	{
		if(otherHero == hero)
			continue;

		const auto otherArmyStrength = otherHero->getArmyStrength();
		mainArmyStrength = std::max(mainArmyStrength, otherArmyStrength);
		if(otherArmyStrength > armyStrength
			|| (otherArmyStrength == armyStrength && otherHero->id.getNum() < hero->id.getNum()))
			isMainArmy = false;
	}
	const auto reinforcementArmyStrength = std::max(
		MIN_REINFORCEMENT_ARMY_STRENGTH,
		mainArmyStrength / REINFORCEMENT_ARMY_STRENGTH_DIVISOR);

	if(role == HeroRole::MAIN && isMainArmy)
		return "MAIN/ARMY";
	if(role == HeroRole::SCOUT && isMainArmy)
		return "SCOUT/ARMY";
	if(role == HeroRole::SCOUT && armyStrength > reinforcementArmyStrength)
		return "SCOUT/REINF";
	if(role == HeroRole::MAIN)
		return "MAIN";
	if(isMainArmy)
		return "ARMY";
	return "SCOUT";
}

/*
 * ////////////////////////////////////
 * ////////// STATIC Methods //////////
 * ////////////////////////////////////
 */

void AIGateway::cheatMapReveal(const std::unique_ptr<Nullkiller> & nullkiller)
{
	if(nullkiller->cc->getCalendar().getCurrentDay() == 1) // No need to execute every day, only the first time
	{
		if(nullkiller->isOpenMap())
		{
			nullkiller->cc->sendMessage("vcmieagles");
		}
	}
}

void AIGateway::memorizeVisitableObjs(const std::unique_ptr<AIMemory> & memory,
                                      const std::unique_ptr<DangerHitMapAnalyzer> & dangerHitMap,
                                      const PlayerColor & playerID,
                                      const std::shared_ptr<CCallback> & cc)
{
	foreach_tile_pos(*cc, [&](const int3 & pos)
	{
		// TODO: Inspect what not visible means when using verbose true
		for(const CGObjectInstance * obj : cc->getVisitableObjs(pos, false))
		{
			memorizeVisitableObj(obj, memory, dangerHitMap, playerID, cc);
		}
	});
}

void AIGateway::memorizeVisitableObj(const CGObjectInstance * obj,
                                     const std::unique_ptr<AIMemory> & memory,
                                     const std::unique_ptr<DangerHitMapAnalyzer> & dangerHitMap,
                                     const PlayerColor & playerID,
                                     const std::shared_ptr<CCallback> & cc)
{
	if(obj->ID == Obj::EVENT)
		return;

	memory->addVisitableObject(obj);

	if(obj->ID == Obj::HERO && cc->getPlayerRelations(obj->tempOwner, playerID) == PlayerRelations::ENEMIES)
	{
		dangerHitMap->resetHitmap();
	}
}

void AIGateway::memorizeRevisitableObjs(const std::unique_ptr<AIMemory> & memory, const PlayerColor & playerID, const std::shared_ptr<CCallback> & cc)
{
	if(cc->getCalendar().getDayOfWeek() == 1)
	{
		for(const ObjectInstanceID objId : memory->visitableObjs)
		{
			const CGObjectInstance * obj = cc->getObjInstance(objId);
			if(obj && isWeeklyRevisitable(playerID, obj))
				memory->markObjectUnvisited(obj);
		}
	}
}

void AIGateway::pickBestArtifacts(const std::shared_ptr<CCallback> & cc, const CGHeroInstance * h, const CGHeroInstance * other)
{
	auto equipBest = [cc](const CGHeroInstance * h, const CGHeroInstance * otherh, bool giveStuffToFirstHero) -> void
	{
		bool changeMade = false;
		std::set<std::pair<ArtifactInstanceID, ArtifactInstanceID>> swappedSet;
		do
		{
			changeMade = false;

			//we collect gear always in same order
			std::vector<ArtifactLocation> allArtifacts;
			if(giveStuffToFirstHero)
			{
				for(auto p : h->artifactsWorn)
				{
					if(p.second.getArt())
						allArtifacts.push_back(ArtifactLocation(h->id, p.first));
				}
			}
			for(auto slot : h->artifactsInBackpack)
				allArtifacts.push_back(ArtifactLocation(h->id, h->getArtPos(slot.getArt())));

			if(otherh)
			{
				for(auto p : otherh->artifactsWorn)
				{
					if(p.second.getArt())
						allArtifacts.push_back(ArtifactLocation(otherh->id, p.first));
				}
				for(auto slot : otherh->artifactsInBackpack)
					allArtifacts.push_back(ArtifactLocation(otherh->id, otherh->getArtPos(slot.getArt())));
			}
			//we give stuff to one hero or another, depending on giveStuffToFirstHero

			const CGHeroInstance * target = nullptr;
			if(giveStuffToFirstHero || !otherh)
				target = h;
			else
				target = otherh;

			for(auto location : allArtifacts)
			{
				if(location.artHolder == target->id && ArtifactUtils::isSlotEquipment(location.slot))
					continue; //don't reequip artifact we already wear

				if(location.slot == ArtifactPosition::MACH4) // don't attempt to move catapult
					continue;

				auto artHolder = cc->getHero(location.artHolder);
				auto s = artHolder->getSlot(location.slot);
				if(!s || s->locked) //we can't move locks
					continue;
				auto artifact = s->getArt();
				if(!artifact)
					continue;
				//FIXME: why are the above possible to be null?

				bool emptySlotFound = false;
				for(auto slot : artifact->getType()->getPossibleSlots().at(target->bearerType()))
				{
					if(target->isPositionFree(slot) && artifact->canBePutAt(target, slot, true)) //combined artifacts are not always allowed to move
					{
						ArtifactLocation destLocation(target->id, slot);
						cc->swapArtifacts(location, destLocation); //just put into empty slot
						emptySlotFound = true;
						changeMade = true;
						break;
					}
				}
				if(!emptySlotFound) //try to put that atifact in already occupied slot
				{
					int64_t artifactScore = getArtifactScoreForHero(target, artifact);

					for(auto slot : artifact->getType()->getPossibleSlots().at(target->bearerType()))
					{
						auto otherSlot = target->getSlot(slot);
						if(otherSlot && otherSlot->getArt()) //we need to exchange artifact for better one
						{
							int64_t otherArtifactScore = getArtifactScoreForHero(target, otherSlot->getArt());
							logAi->trace(
								"Comparing artifacts of %s: %s vs %s. Score: %d vs %d",
								target->getHeroTypeName(),
								artifact->getType()->getJsonKey(),
								otherSlot->getArt()->getType()->getJsonKey(),
								artifactScore,
								otherArtifactScore
							);

							//if that artifact is better than what we have, pick it
							//combined artifacts are not always allowed to move
							if(artifactScore > otherArtifactScore && artifact->canBePutAt(target, slot, true))
							{
								std::pair swapPair = std::minmax<ArtifactInstanceID>({artifact->getId(), otherSlot->artifactID});
								if(swappedSet.find(swapPair) != swappedSet.end())
								{
									logAi->warn(
										"Artifacts % s < -> % s have already swapped before, ignored.",
										artifact->getType()->getJsonKey(),
										otherSlot->getArt()->getType()->getJsonKey()
									);
									continue;
								}
								logAi->trace("Exchange artifacts %s <-> %s", artifact->getType()->getJsonKey(), otherSlot->getArt()->getType()->getJsonKey());

								if(!otherSlot->getArt()->canBePutAt(artHolder, location.slot, true))
								{
									ArtifactLocation destLocation(target->id, slot);
									ArtifactLocation backpack(artHolder->id, ArtifactPosition::BACKPACK_START);

									cc->swapArtifacts(destLocation, backpack);
									cc->swapArtifacts(location, destLocation);
								}
								else
								{
									cc->swapArtifacts(location, ArtifactLocation(target->id, target->getArtPos(otherSlot->getArt())));
								}

								changeMade = true;
								swappedSet.insert(swapPair);
								break;
							}
						}
					}
				}
				if(changeMade)
					break; //start evaluating artifacts from scratch
			}
		} while(changeMade);
	};

	equipBest(h, other, true);

	if(other)
		equipBest(h, other, false);
}

}
