/*
 * CGameHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CGameHandler.h"

#include "CVCMIServer.h"
#include "TurnTimerHandler.h"
#include "ServerNetPackVisitors.h"
#include "ServerSpellCastEnvironment.h"
#include "TurnStartVisitScheduler.h"
#include "battles/BattleProcessor.h"
#include "processors/HeroPoolProcessor.h"
#include "processors/NewTurnProcessor.h"
#include "processors/PlayerMessageProcessor.h"
#include "processors/TurnOrderProcessor.h"
#include "queries/BattleQueries.h"
#include "queries/QueriesProcessor.h"
#include "queries/LuaScriptQuery.h"
#include "queries/MapQueries.h"
#include "queries/VisitQueries.h"

#include "../lib/CConfigHandler.h"
#include "../lib/CCreatureHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/CSoundBase.h"
#include "../lib/GameConstants.h"
#include "../lib/IGameSettings.h"
#include "../lib/StartInfo.h"
#include "../lib/TerrainHandler.h"
#include "../lib/GameLibrary.h"
#include "../lib/CSkillHandler.h"
#include "../lib/int3.h"

#include "../lib/battle/BattleInfo.h"
#include "../lib/battle/PhysicalAffliction.h"
#include "../lib/battle/NewHorizonsSoulChain.h"
#include "../lib/bonuses/BonusParameters.h"
#include "../lib/callback/GameRandomizer.h"
#include "../lib/campaign/CampaignState.h"

#include "../lib/entities/ResourceTypeHandler.h"
#include "../lib/entities/artifact/ArtifactUtils.h"
#include "../lib/entities/artifact/CArtifact.h"
#include "../lib/entities/artifact/CArtifactFittingSet.h"
#include "../lib/entities/building/CBuilding.h"
#include "../lib/entities/creature/NewHorizonsMusterRules.h"
#include "../lib/entities/faction/CTownHandler.h"
#include "../lib/entities/hero/CHeroHandler.h"
#include "../lib/entities/hero/NewHorizonsHeroRules.h"

#include "../lib/filesystem/Filesystem.h"
#include "../lib/filesystem/SavegamePath.h"

#include "../lib/gameState/CGameState.h"

#include <vcmi/scripting/MapEventDispatcher.h>
#include "../lib/gameState/QuestInfo.h"
#include "../lib/gameState/UpgradeInfo.h"

#include "../lib/mapping/CMap.h"
#include "../lib/mapping/CMapService.h"
#include "../lib/mapping/HotaScriptConverter.h"

#include "../lib/mapObjects/CGCreature.h"
#include "../lib/mapObjects/CGMarket.h"
#include "../lib/mapObjects/CGPandoraBox.h"
#include "../lib/mapObjects/Quest.h"
#include "../lib/mapObjects/TownBuildingInstance.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapObjects/MiscObjects.h"
#include "../lib/mapObjectConstructors/AObjectTypeHandler.h"
#include "../lib/mapObjectConstructors/CObjectClassesHandler.h"

#include "../lib/modding/ModIncompatibility.h"

#include "../lib/networkPacks/StackLocation.h"
#include "../lib/networkPacks/PacksForClient.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include "../lib/networkPacks/SetStackEffect.h"
#include "../lib/CStack.h"

#include "../lib/pathfinder/CPathfinder.h"
#include "../lib/pathfinder/PathfinderOptions.h"
#include "../lib/pathfinder/TurnInfo.h"

#include "../lib/rmg/CMapGenOptions.h"

#include "../lib/serializer/CSaveFile.h"
#include "../lib/serializer/CLoadFile.h"

#include "../lib/spells/CSpell.h"
#include "../lib/spells/NewHorizonsMagic.h"
#include "../lib/spells/NewHorizonsSpellAvailability.h"

#include <vstd/RNG.h>
#include <vstd/CLoggerBase.h>
#include <vcmi/events/EventBus.h>
#include <vcmi/events/GenericEvents.h>
#include <vcmi/events/AdventureEvents.h>


#define COMPLAIN_RET_IF(cond, txt) do {if (cond){complain(txt); return;}} while(0)
#define COMPLAIN_RET_FALSE_IF(cond, txt) do {if (cond){complain(txt); return false;}} while(0)
#define COMPLAIN_RET(txt) {complain(txt); return false;}
#define COMPLAIN_RETF(txt, FORMAT) {complain(boost::str(boost::format(txt) % FORMAT)); return false;}

template <typename T>
void callWith(std::vector<T> args, std::function<void(T)> fun, ui32 which)
{
	fun(args[which]);
}

const Services * CGameHandler::services() const
{
	return LIBRARY;
}

IGameInfoCallback & CGameHandler::gameInfo()
{
	return *gs;
}

const CGameHandler::BattleCb * CGameHandler::battle(const BattleID & battleID) const
{
	return gameState().getBattle(battleID);
}

const CGameHandler::GameCb * CGameHandler::game() const
{
	return gs.get();
}

IGameServer & CGameHandler::gameServer() const
{
	return server;
}

void CGameHandler::levelUpHero(const CGHeroInstance * hero, SecondarySkill skill, bool continueProgression)
{
	if(hero && newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules))
	{
		const auto * definition = LIBRARY->skillh->getById(skill);
		if(!definition || !hero->getPerkState().canAdvanceSkillNormally(definition->getJsonKey(),
			hero->getSecSkillLevel(skill)))
			throw std::runtime_error("New Horizons skill advancement requires its preceding perk tier");
	}
	changeSecSkill(hero, skill, 1, ChangeValueMode::RELATIVE);
	if(continueProgression)
		heroLevelUpChoiceDone(hero);
}

void CGameHandler::levelUpHero(const CGHeroInstance * hero,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & offer, size_t choice, uint64_t seed,
	bool continueProgression)
{
	if(!hero)
		throw std::runtime_error("Cannot choose a perk for a missing hero");
	const auto rankLookup = [hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	};
	// Validate without mutating authoritative state. The replicated pack below is
	// the sole state-change path for the server and every connected client.
	auto validated = hero->getPerkState();
	validated.acceptOffer(offer, choice, rankLookup, seed);

	HeroPerkChosen chosen;
	chosen.hero = hero->id;
	chosen.selection = offer.at(choice).selection;
	const int previousSight = hero->getSightRadius();
	sendAndApply(chosen);
	if(hero->getOwner().isValidPlayer() && hero->getSightRadius() > previousSight)
		changeFogOfWar(hero->getSightCenter(), hero->getSightRadius(), hero->getOwner(), ETileVisibility::REVEALED);
	if(continueProgression)
		heroLevelUpChoiceDone(hero);
}

void CGameHandler::heroLevelUpChoiceDone(const CGHeroInstance * hero)
{
	if(!offerHeroMastery(hero))
		expGiven(hero);
}

bool CGameHandler::offerHeroMastery(const CGHeroInstance * hero)
{
	if(!hero->getOwner().isValidPlayer())
		return false;
	for(const auto & existing : queries->allQueries())
		if(existing->getType() == CHeroMasteryDialogQuery::TYPE
			&& static_cast<const CHeroMasteryDialogQuery &>(*existing).heroId == hero->id)
			return true;
	if(const auto offer = hero->prepareMasteryOffer())
	{
		if(hero->getSecSkillLevel(offer->skill) != MasteryLevel::EXPERT)
			return false;
		HeroMasteryOffer pack;
		pack.offer = *offer;
		sendAndApply(pack);
	}
	if(!hero->getMasteryState().pending
		|| hero->getSecSkillLevel(hero->getMasteryState().pending->skill) != MasteryLevel::EXPERT)
		return false;
	queries->addQuery(std::make_shared<CHeroMasteryDialogQuery>(this, hero));
	return true;
}

void CGameHandler::resumeMasteryQueries(PlayerColor player)
{
	if(!player.isValidPlayer() || !uiReadyForDialogs.contains(player) || queries->topQuery(player))
		return;
	const auto * state = gameInfo().getPlayerState(player);
	if(!state)
		return;
	for(const auto * hero : state->getHeroes())
		if(offerHeroMastery(hero))
			return;
}

void CGameHandler::levelUpHero(const CGHeroInstance * hero)
{
	if(hero->getMasteryState().pending && offerHeroMastery(hero))
		return;
	// required exp for at least 1 lvl-up hasn't been reached
	if (!hero->gainsLevel())
	{
		if (hero->getCommander() && hero->getCommander()->gainsLevel())
			levelUpCommander(hero->getCommander());
		return;
	}

	const bool artilleryExpertBeforeGain = hero->getSecSkillLevel(SecondarySkill::ARTILLERY) == MasteryLevel::EXPERT;
	const bool logisticsExpertBeforeGain = hero->getSecSkillLevel(SecondarySkill::LOGISTICS) == MasteryLevel::EXPERT
		&& newHorizonsHeroes::masteryOptions(hero->getMasteryState().rules, SecondarySkill::LOGISTICS).has_value();
	// give primary skill
	logGlobal->trace("%s got level %d", hero->getNameTextID(), hero->level);
	auto gains = randomizer->rollPrimarySkillsForLevelup(hero);
	const auto primarySkill = PrimarySkill(std::distance(gains.begin(), std::max_element(gains.begin(), gains.end())));
	for(int i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
	{
		const auto before = hero->getBasePrimarySkillValue(PrimarySkill(i));
		if(gains[i])
		{
			SetPrimarySkill sps;
			sps.id = hero->id;
			sps.which = PrimarySkill(i);
			sps.mode = ChangeValueMode::RELATIVE;
			sps.val = gains[i];
			sendAndApply(sps);
		}
		gains[i] = hero->getBasePrimarySkillValue(PrimarySkill(i)) - before;
	}

	HeroLevelUp hlu;
	hlu.player = hero->tempOwner;
	hlu.heroId = hero->id;
	hlu.primskill = primarySkill;
	hlu.primaryGains = gains;
	hlu.artilleryExpertBeforeGain = artilleryExpertBeforeGain;
	hlu.logisticsExpertBeforeGain = logisticsExpertBeforeGain;
	auto prepareChoices = [this, hero, &hlu]()
	{
		hlu.skills = randomizer->rollSecondarySkills(hero);
		if(!newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules))
			return;

		const auto & perkState = hero->getPerkState();
		std::erase_if(hlu.skills, [hero, &perkState](SecondarySkill skill)
		{
			const auto * definition = LIBRARY->skillh->getById(skill);
			return !definition || !perkState.canAdvanceSkillNormally(definition->getJsonKey(),
				hero->getSecSkillLevel(skill));
		});
		const size_t maxSkillChoices = static_cast<size_t>(perkState.rules["maxSkillChoices"].Integer());
		if(hlu.skills.size() > maxSkillChoices)
			hlu.skills.resize(maxSkillChoices);
		hlu.perkOfferSeed = static_cast<uint32_t>(randomizer->getDefault().nextInt());
		hlu.perks = perkState.prepareOffer([hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		}, hlu.perkOfferSeed);
	};
	prepareChoices();

	const int reachedLevel = hero->level + 1;
	if(reachedLevel % 5 == 0
		&& hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.quickStudy"))
		prepareChoices();

	if (!hero->getOwner().isValidPlayer())
	{
		sendAndApply(hlu);
		if(hlu.skills.empty() && hlu.perks.empty())
			levelUpHero(hero);
		else if(!hlu.skills.empty())
			levelUpHero(hero, hlu.skills.front());
		else
			levelUpHero(hero, hlu.perks, 0, hlu.perkOfferSeed);
	}
	else
	{
		auto levelUpQuery = std::make_shared<CHeroLevelUpDialogQuery>(this, hlu, hero);
		queries->addQuery(levelUpQuery);
		//level up will be called on query reply
	}
}

void CGameHandler::levelUpCommander (const CCommanderInstance * c, int skill)
{
	SetCommanderProperty scp;

	const auto * hero = dynamic_cast<const CGHeroInstance *>(c->getArmy());
	if (hero)
		scp.heroid = hero->id;
	else
	{
		complain ("Commander is not led by hero!");
		return;
	}

	scp.accumulatedBonus.parameters = 0;
	scp.accumulatedBonus.duration = BonusDuration::PERMANENT;
	scp.accumulatedBonus.turnsRemain = 0;
	scp.accumulatedBonus.source = BonusSource::COMMANDER;
	scp.accumulatedBonus.valType = BonusValueType::BASE_NUMBER;
	if (skill <= ECommander::SPELL_POWER)
	{
		scp.which = SetCommanderProperty::BONUS;

		auto difference = [](std::vector< std::vector <ui8> > skillLevels, std::vector <ui8> secondarySkills, int skillToTest)->int
		{
			int s = std::min (skillToTest, static_cast<int>(ECommander::SPELL_POWER)); //spell power level controls also casts and resistance
			return skillLevels.at(skillToTest).at(secondarySkills.at(s)) - (secondarySkills.at(s) ? skillLevels.at(skillToTest).at(secondarySkills.at(s)-1) : 0);
		};

		switch (skill)
		{
			case ECommander::ATTACK:
				scp.accumulatedBonus.type = BonusType::PRIMARY_SKILL;
				scp.accumulatedBonus.subtype = BonusSubtypeID(PrimarySkill::ATTACK);
				break;
			case ECommander::DEFENSE:
				scp.accumulatedBonus.type = BonusType::PRIMARY_SKILL;
				scp.accumulatedBonus.subtype = BonusSubtypeID(PrimarySkill::DEFENSE);
				break;
			case ECommander::HEALTH:
				scp.accumulatedBonus.type = BonusType::STACK_HEALTH;
				scp.accumulatedBonus.valType = BonusValueType::PERCENT_TO_ALL; //TODO: check how it accumulates in original WoG with artifacts such as vial of life blood, elixir of life etc.
				break;
			case ECommander::DAMAGE:
				scp.accumulatedBonus.type = BonusType::CREATURE_DAMAGE;
				scp.accumulatedBonus.subtype = BonusCustomSubtype::creatureDamageBoth;
				scp.accumulatedBonus.valType = BonusValueType::PERCENT_TO_ALL;
				break;
			case ECommander::SPEED:
				scp.accumulatedBonus.type = BonusType::STACKS_SPEED;
				break;
			case ECommander::SPELL_POWER:
				scp.accumulatedBonus.type = BonusType::SPELL_DAMAGE_REDUCTION;
				scp.accumulatedBonus.subtype = BonusSubtypeID(SpellSchool::ANY);
				scp.accumulatedBonus.val = difference (LIBRARY->creh->skillLevels, c->secondarySkills, ECommander::RESISTANCE);
				sendAndApply(scp); //additional pack
				scp.accumulatedBonus.type = BonusType::CREATURE_SPELL_POWER;
				scp.accumulatedBonus.val = difference (LIBRARY->creh->skillLevels, c->secondarySkills, ECommander::SPELL_POWER) * 100; //like hero with spellpower = ability level
				sendAndApply(scp); //additional pack
				scp.accumulatedBonus.type = BonusType::CASTS;
				scp.accumulatedBonus.val = difference (LIBRARY->creh->skillLevels, c->secondarySkills, ECommander::CASTS);
				sendAndApply(scp); //additional pack
				scp.accumulatedBonus.type = BonusType::CREATURE_ENCHANT_POWER; //send normally
				break;
		}

		scp.accumulatedBonus.val = difference (LIBRARY->creh->skillLevels, c->secondarySkills, skill);
		sendAndApply(scp);

		scp.which = SetCommanderProperty::SECONDARY_SKILL;
		scp.additionalInfo = skill;
		scp.amount = c->secondarySkills.at(skill) + 1;
		sendAndApply(scp);
	}
	else if (skill >= 100)
	{
		for(const auto & bonus : LIBRARY->creh->skillRequirements.at(skill - 100).first)
		{
			scp.which = SetCommanderProperty::SPECIAL_SKILL;
			scp.accumulatedBonus = *bonus;
			scp.additionalInfo = skill; //unnormalized
			sendAndApply(scp);
		}
	}
	expGiven(hero);
}

void CGameHandler::levelUpCommander(const CCommanderInstance * c)
{
	if (!c->gainsLevel())
	{
		return;
	}
	CommanderLevelUp clu;

	const auto * hero = dynamic_cast<const CGHeroInstance *>(c->getArmy());
	if(hero)
	{
		clu.heroId = hero->id;
		clu.player = hero->tempOwner;
	}
	else
	{
		complain ("Commander is not led by hero!");
		return;
	}

	//picking sec. skills for choice

	for (int i = 0; i <= ECommander::SPELL_POWER; ++i)
	{
		if (c->secondarySkills.at(i) < ECommander::MAX_SKILL_LEVEL)
			clu.skills.push_back(i);
	}
	int i = 100;
	for (const auto & specialSkill : LIBRARY->creh->skillRequirements)
	{
		if (c->secondarySkills.at(specialSkill.second.first) >= ECommander::MAX_SKILL_LEVEL - 1
			&&  c->secondarySkills.at(specialSkill.second.second) >= ECommander::MAX_SKILL_LEVEL - 1
			&&  !vstd::contains (c->specialSkills, i))
			clu.skills.push_back (i);
		++i;
	}
	if (!hero->getOwner().isValidPlayer()) //choose skill automatically
	{
		sendAndApply(clu);
		if(clu.skills.empty())
			levelUpCommander(c);
		else
			levelUpCommander(c, *RandomGeneratorUtil::nextItem(clu.skills, getRandomGenerator()));
	}
	else
	{
		auto commanderLevelUp = std::make_shared<CCommanderLevelUpDialogQuery>(this, clu, hero);
		queries->addQuery(commanderLevelUp);
	}
}

void CGameHandler::expGiven(const CGHeroInstance *hero)
{
	if (hero->gainsLevel())
		levelUpHero(hero);
	else if (hero->getCommander() && hero->getCommander()->gainsLevel())
		levelUpCommander(hero->getCommander());
}

void CGameHandler::giveStackExperience(const CArmedInstance * army, TExpType val)
{
	GiveStackExperience gse;
	gse.id = army->id;

	for (const auto & stack : army->Slots())
	{
		int experienceBonusMultiplier = stack.second->valOfBonuses(BonusType::STACK_EXPERIENCE_GAIN_PERCENT);
		gse.val[stack.first] = val + val * experienceBonusMultiplier / 100;
	}
	sendAndApply(gse);
}

void CGameHandler::giveExperience(const CGHeroInstance * hero, TExpType amountToGain)
{
	giveExperienceWithoutLevelUp(hero, amountToGain);
	expGiven(hero);
}

void CGameHandler::giveExperienceWithoutLevelUp(const CGHeroInstance * hero, TExpType amountToGain)
{
	TExpType maxExp = LIBRARY->heroh->reqExp(LIBRARY->heroh->maxSupportedLevel());
	TExpType currHeroExp = hero->exp;

	if (gameState().getMap().levelLimit != 0)
		maxExp = LIBRARY->heroh->reqExp(gameState().getMap().levelLimit);

	TExpType canGainHeroExp = 0;
	if (maxExp > currHeroExp)
		canGainHeroExp = maxExp - currHeroExp;

	TExpType actualHeroExperience = 0;

	if (amountToGain > canGainHeroExp)
	{
		// set given experience to max possible, but don't decrease if hero already over top
		actualHeroExperience = canGainHeroExp;

		InfoWindow iw;
		iw.player = hero->tempOwner;
		iw.text.appendTextID("core.genrltxt.1"); //can gain no more XP
		iw.text.replaceTextID(hero->getNameTextID());
		sendAndApply(iw);
	}
	else
		actualHeroExperience = amountToGain;

	SetHeroExperience she;
	she.id = hero->id;
	she.mode = ChangeValueMode::RELATIVE;
	she.val = actualHeroExperience;
	sendAndApply(she);

	//hero may level up
	if (hero->getCommander() && hero->getCommander()->alive)
	{
		TExpType canGainCommanderExp = 0;
		TExpType currCommanderExp = hero->getCommander()->getTotalExperience();
		if (maxExp > currHeroExp)
			canGainCommanderExp = maxExp - currCommanderExp;

		TExpType actualCommanderExperience = amountToGain > canGainCommanderExp ? canGainCommanderExp : amountToGain;

		SetCommanderProperty scp;
		scp.heroid = hero->id;
		scp.which = SetCommanderProperty::EXPERIENCE;
		scp.amount = actualCommanderExperience;
		sendAndApply(scp);
	}

}

void CGameHandler::changePrimSkill(const CGHeroInstance * hero, PrimarySkill which, si64 val, ChangeValueMode mode)
{
	SetPrimarySkill sps;
	sps.id = hero->id;
	sps.which = which;
	sps.mode = mode;
	sps.val = val;
	sendAndApply(sps);
}

void CGameHandler::changeSecSkill(const CGHeroInstance * hero, SecondarySkill which, int val, ChangeValueMode mode)
{
	if(!hero)
	{
		logGlobal->error("changeSecSkill provided no hero");
		return;
	}
	const auto replaced = newHorizonsMagic::replacementSkill(hero->getMagicRules(), which);
	const auto skill = newHorizonsHeroes::normalizeRewardSkill(hero->getPrimaryGrowthRules(), replaced);
	if(!skill)
	{
		// This is the last authoritative boundary shared by map rewards,
		// scripted rewards and other server-side teachers.  A retired New
		// Horizons skill must not be resurrected by a raw SetSecSkill request.
		logGlobal->warn("Ignoring retired or unavailable secondary skill %s for hero %s",
			SecondarySkill::encode(which.getNum()), hero->nodeName());
		return;
	}

	SetSecSkill sss;
	sss.id = hero->id;
	sss.which = *skill;
	sss.val = val;
	sss.mode = mode;
	sendAndApply(sss);

	if (hero->getVisitedTown())
	{
		const auto * town = hero->getVisitedTown();
		giveSpells(town, hero, hero == town->getVisitingHero());
	}

	// Our scouting range may have changed - update it
	if (hero->getOwner().isValidPlayer())
		changeFogOfWar(hero->getSightCenter(), hero->getSightRadius(), hero->getOwner(), ETileVisibility::REVEALED);

}

void CGameHandler::handleClientDisconnection(GameConnectionID connectionID, const std::vector<PlayerConnectionID> & disconnectedPlayerIds)
{
	if(gameServer().getState() == EServerState::SHUTDOWN || !gameState().getStartInfo())
	{
		assert(0); // game should have shut down before reaching this point!
		return;
	}

	std::vector<PlayerColor> disconnectedPlayers;
	std::vector<PlayerColor> remainingPlayers;

	// this player have left the game - broadcast infowindow to all in-game players
	for (const auto & player : gameState().players)
	{
		if (gameInfo().getPlayerState(player.first)->status != EPlayerStatus::INGAME)
			continue;

		if (gameServer().hasPlayerAt(player.first, connectionID))
			disconnectedPlayers.push_back(player.first);
		else
			remainingPlayers.push_back(player.first);
	}

	if(disconnectedPlayers.empty() && !disconnectedPlayerIds.empty())
	{
		for(const auto & player : gameState().players)
		{
			if (gameInfo().getPlayerState(player.first)->status != EPlayerStatus::INGAME)
				continue;

			const auto playerSettings = gameInfo().getPlayerSettings(player.first);
			if(!playerSettings)
				continue;

			for(const auto disconnectedPlayerId : disconnectedPlayerIds)
			{
				if(vstd::contains(playerSettings->connectedPlayerIDs, disconnectedPlayerId))
				{
					disconnectedPlayers.push_back(player.first);
					break;
				}
			}
		}
	}

	for (const auto & inGamePlayer : remainingPlayers)
	{
		for (const auto & lostPlayer : disconnectedPlayers)
		{
			InfoWindow out;
			out.player = inGamePlayer;
			out.text.appendTextID("vcmi.server.errors.playerLeft");
			out.text.replaceName(lostPlayer);
			out.components.emplace_back(ComponentType::FLAG, lostPlayer);
			sendAndApply(out);
		}
	}
}

void CGameHandler::handleReceivedPack(GameConnectionID connection, CPackForServer & pack)
{
	//prepare struct informing that action was applied
	auto sendPackageResponse = [&](bool successfullyApplied)
	{
		PackageApplied applied(
			pack.player,
			pack.requestID,
			CTypeList::getInstance().getTypeID(&pack),
			successfullyApplied
		);
		gameServer().sendPack(applied, connection);
	};

	PackageReceived received(
		pack.player,
		pack.requestID,
		CTypeList::getInstance().getTypeID(&pack)
	);
	gameServer().sendPack(received, connection);

	if(isBlockedByQueries(&pack, pack.player))
	{
		sendPackageResponse(false);
	}
	else
	{
		bool result;
		try
		{
			ApplyGhNetPackVisitor applier(*this, connection);
			pack.visit(applier);
			result = applier.getResult();
		}
		catch(ExceptionNotAllowedAction &)
		{
			result = false;
		}

		if(result)
			logGlobal->trace("Message %s successfully applied!", typeid(pack).name());
		else
		{
			// A rejected request is normally an expected result of authoritative
			// validation (for example, an ArrangeStacks request that would exceed
			// Leadership).  The validator has already sent the actionable reason
			// to the player.  Do not turn that ordinary user error into a second
			// broadcast claiming that the request was "fishy".
			logGlobal->debug("Message %s was rejected by authoritative validation.", typeid(pack).name());
		}

		// The client uses this acknowledgement to retire/predict requests.  A
		// rejected mutation must be reported as rejected so it cannot be treated
		// as a successful transfer or leave a prediction applied locally.
		sendPackageResponse(result);
	}
}

CGameHandler::CGameHandler(IGameServer & server, const std::shared_ptr<CGameState> & initialGamestate)
	:CGameHandler(server)
{
	gs = initialGamestate;
	randomizer = std::make_unique<GameRandomizer>(*gs);
}

CGameHandler::CGameHandler(IGameServer & server)
	: server(server)
	, heroPool(std::make_unique<HeroPoolProcessor>(this))
	, battles(std::make_unique<BattleProcessor>(this))
	, queries(std::make_unique<QueriesProcessor>())
	, turnStartVisitScheduler(std::make_unique<TurnStartVisitScheduler>(*this, *queries))
	, turnOrder(std::make_unique<TurnOrderProcessor>(this))
	, turnTimerHandler(std::make_unique<TurnTimerHandler>(*this))
	, newTurnProcessor(std::make_unique<NewTurnProcessor>(this))
	, statistics(std::make_unique<StatisticDataSet>())
	, spellEnv(std::make_unique<ServerSpellCastEnvironment>(this))
	, playerMessages(std::make_unique<PlayerMessageProcessor>(this))
	, QID(1)
	, complainNoCreatures("No creatures to split")
	, complainNotEnoughCreatures("Cannot split that stack, not enough creatures!")
	, complainInvalidSlot("Invalid slot accessed!")
{
	queries->setListener(turnStartVisitScheduler.get());
}

CGameHandler::~CGameHandler() = default;

ServerCallback * CGameHandler::spellcastEnvironment() const
{
	return spellEnv.get();
}

void CGameHandler::init(StartInfo *si, Load::ProgressAccumulator & progressTracking)
{
	CMapService mapService;
	gs = std::make_shared<CGameState>();
	int requestedSeed = settings["server"]["seed"].Integer();
	randomizer = std::make_unique<GameRandomizer>(*gs);
	if (requestedSeed != 0)
		randomizer->setSeed(requestedSeed);
	logGlobal->info("Using random seed: %d", randomizer->getDefault().nextInt());
	gs->preInit(LIBRARY);
	logGlobal->info("Gamestate created!");
	gs->init(&mapService, si, *randomizer, progressTracking);
	const auto * startInfo = gs->getStartInfo();
	gs->setSaveDirectory(SavegamePath::generateGameDirectoryName(*startInfo, *gs->getMapHeader()));
	logGlobal->info("Gamestate initialized!");

	for (const auto & elem : gameState().players)
		turnOrder->addPlayer(elem.first);

	configureReplayLog(true);
}

void CGameHandler::setPortalDwelling(const CGTownInstance * town, bool forced=false, bool clear = false)
{// bool forced = true - if creature should be replaced, if false - only if no creature was set
	// The New Horizons Portal links to a real owned external dwelling instead
	// of maintaining the legacy random bonus row in each town.
	if(newHorizonsMagic::rulesActive(gameInfo().getMagicRules()))
		return;

	const PlayerState * p = gameInfo().getPlayerState(town->tempOwner);
	if (!p)
	{
		assert(town->tempOwner == PlayerColor::NEUTRAL);
		return;
	}

	if (forced || town->creatures.at(town->getTown()->creatures.size()).second.empty())//we need to change creature
		{
			SetAvailableCreatures ssi;
			ssi.tid = town->id;
			ssi.creatures = town->creatures;
			ssi.creatures[town->getTown()->creatures.size()].second.clear();//remove old one

			std::set<CreatureID> availableCreatures;
			for (const auto & dwelling : p->getOwnedObjects())
			{
				const auto & dwellingCreatures = dwelling->asOwnable()->providedCreatures();
				availableCreatures.insert(dwellingCreatures.begin(), dwellingCreatures.end());
			}

			if (availableCreatures.empty())
				return;

			CreatureID creatureId = *RandomGeneratorUtil::nextItem(availableCreatures, getRandomGenerator());

			if (clear)
			{
				ssi.creatures[town->getTown()->creatures.size()].first = std::max(1, gameInfo().getCreatureBaseGrowth(creatureId) / 2);
			}
			else
			{
				ssi.creatures[town->getTown()->creatures.size()].first = gameInfo().getCreatureBaseGrowth(creatureId);
			}
			ssi.creatures[town->getTown()->creatures.size()].second.push_back(creatureId);
			sendAndApply(ssi);
		}
}

void CGameHandler::onPlayerTurnStarted(PlayerColor which)
{
	turnTimerHandler->onPlayerGetTurn(which);
	newTurnProcessor->onPlayerTurnStarted(which);
}

void CGameHandler::onPlayerTurnEnded(PlayerColor which)
{
	turnTimerHandler->onEndTurn(which);
	newTurnProcessor->onPlayerTurnEnded(which);
}

void CGameHandler::onAdvInterfaceReady(PlayerColor player)
{
	if(uiReadyForDialogs.count(player))
		return;

	uiReadyForDialogs.insert(player);

	logGlobal->trace("AdvInterfaceReady received for player %s", player);

	resumeMasteryQueries(player);

	// Kick top query for this player: if it's a dialog query waiting for UI, it should prompt now.
	auto top = queries->topQuery(player);
	if(!top)
		return;

	// We only want dialog queries to try prompting here.
	// They should override onExposure() to "prompt when uiReadyForDialogs is true" (next step),
	// so triggering exposure is enough.
	top->onExposure(top);
}

void CGameHandler::addStatistics(StatisticDataSet &stat) const
{
	for (const auto & elem : gameState().players)
	{
		if (elem.first == PlayerColor::NEUTRAL || !elem.first.isValidPlayer())
			continue;

		auto data = StatisticDataSet::createEntry(&elem.second, &gameState(), *statistics);

		stat.add(data);
	}
}

void CGameHandler::onNewTurn()
{
	logGlobal->trace("Turn %d", gameState().day+1);

	auto calendar = gameInfo().getCalendar();
	bool firstTurn = !calendar.getCurrentDay();
	bool newMonth = calendar.getDayOfMonth() == calendar.getDaysInMonth();

	if (firstTurn)
	{
		for (const auto * obj : gameState().getMap().getObjects<CGHeroInstance>())
		{
			if (obj->ID == Obj::PRISON) //give imprisoned hero 0 exp to level him up. easiest to do at this point
			{
				giveExperience(obj, 0);
			}
		}

		for (const auto & elem : gameState().players)
			heroPool->onNewWeek(elem.first);

	}
	else
	{
		addStatistics(*statistics); // write at end of turn
	}

	const auto & currentDaySelector = [day = gameState().day+1](const Bonus * bonus)
	{
		if (!bonus->parameters)
			return true;
		if ((day % bonus->parameters->toNumber()) == 0)
			return true;
		return false;
	};

	const auto & fullMapScoutingSelector = Selector::type()(BonusType::FULL_MAP_SCOUTING).And(currentDaySelector);
	const auto & fullMapDarknessSelector = Selector::type()(BonusType::FULL_MAP_DARKNESS).And(currentDaySelector);
	const auto & darknessSelector = Selector::type()(BonusType::DARKNESS).And(currentDaySelector);

	for (const auto & townID : gameState().getMap().getAllTowns())
	{
		const auto * t = gameState().getTown(townID);
		PlayerColor player = t->tempOwner;

		// Skyship, probably easier to handle same as Veil of darkness
		// do it every new day before veils
		if(t->hasBonus(fullMapScoutingSelector) && player.isValidPlayer())
			changeFogOfWar(t->getSightCenter(), GameConstants::FULL_MAP_RANGE, player, ETileVisibility::REVEALED);
	}

	for (const auto & object : gameState().getMap().getObjects<CArmedInstance>())
	{
		if(!object->hasBonus(darknessSelector) && !object->hasBonus(fullMapDarknessSelector))
			continue;

		for(const auto & player : gameState().players)
		{
			if (gameInfo().getPlayerStatus(player.first) != EPlayerStatus::INGAME)
				continue;

			if (gameInfo().getPlayerRelations(player.first, object->getOwner()) != PlayerRelations::ENEMIES)
				continue;

			if (object->hasBonus(fullMapDarknessSelector))
				changeFogOfWar(object->getSightCenter(), GameConstants::FULL_MAP_RANGE, player.first, ETileVisibility::HIDDEN);
			else
				changeFogOfWar(object->getSightCenter(), object->valOfBonuses(darknessSelector), player.first, ETileVisibility::HIDDEN);
		}
	}

	if (newMonth)
	{
		SetAvailableArtifacts saa;
		saa.id = ObjectInstanceID::NONE;
		saa.arts = randomizer->rollMarketArtifactSet();
		sendAndApply(saa);
	}

	newTurnProcessor->onNewTurn();

	if (!firstTurn)
		checkVictoryLossConditionsForAll(); // check for map turn limit

	//call objects
	for (auto & elem : gameState().getMap().getObjects())
	{
		if (elem)
			elem->newTurn(*this, *randomizer);
	}
}

void CGameHandler::start(bool resume)
{
	LOG_TRACE_PARAMS(logGlobal, "resume=%d", resume);

	if (!resume)
	{
		onNewTurn();
		for(const auto & player : gameState().players)
			turnTimerHandler->onGameplayStart(player.first);
	}

	turnOrder->onGameStarted();
}

void CGameHandler::tick(int millisecondsPassed)
{
	turnTimerHandler->update(millisecondsPassed);
}

void CGameHandler::giveSpells(const CGTownInstance *t, const CGHeroInstance *h, bool includeAdventureSpells)
{
	if (!h->hasSpellbook())
		return; //hero hasn't spellbook
	const auto & magicRules = gameInfo().getMagicRules();
	ChangeSpells cs;
	cs.hid = h->id;
	cs.learn = true;
	if (t->hasBuilt(BuildingSubID::AURORA_BOREALIS) && t->hasBuilt(BuildingID::MAGES_GUILD_1))
	{
		// Aurora Borealis give spells of all levels even if only level 1 mages guild built
		for (int i = 0; i < GameConstants::SPELL_LEVELS; i++)
		{
			std::vector<SpellID> spells;
			gameState().getAllowedSpells(spells, i+1);
			for (const auto & spell : spells)
				if(!newHorizonsMagic::isAdventureSpell(magicRules, spell) && h->canLearnSpell(spell.toSpell()))
					cs.spells.insert(spell);
		}
	}
	else
	{
		for (int i = 0; i < t->mageGuildLevel(); i++)
		{
			for (int j = 0; j < t->spellsAtLevel(i+1, true) && j < t->spells.at(i).size(); j++)
			{
				const auto spellID = t->spells.at(i).at(j);
				if(!newHorizonsMagic::isAdventureSpell(magicRules, spellID) && h->canLearnSpell(spellID.toSpell()))
					cs.spells.insert(spellID);
			}
		}
	}
	if(includeAdventureSpells && newHorizonsMagic::adventureSpellRulesActive(gameInfo().getMagicRules()))
	{
		for(int guildLevel = 1; guildLevel <= 5; ++guildLevel)
		{
			if(!t->hasNewHorizonsAdventureSpellUnlocked(guildLevel))
				continue;

			const auto spellID = newHorizonsMagic::adventureSpellForGuildLevel(magicRules, guildLevel);
			const auto * spell = spellID.toSpell();
			if(spell && gameInfo().isAllowed(spellID) && h->canLearnSpell(spell))
				cs.spells.insert(spellID);
		}
	}
	if (!cs.spells.empty())
		sendAndApply(cs);
}

bool CGameHandler::removeObject(const CGObjectInstance * obj, const PlayerColor & initiator)
{
	if (!obj || !gameInfo().getObj(obj->id))
	{
		logGlobal->error("Something wrong, that object already has been removed or hasn't existed!");
		return false;
	}

	RemoveObject ro;
	ro.objectID = obj->id;
	ro.initiator = initiator;
	sendAndApply(ro);

	checkVictoryLossConditionsForAll(); //e.g. if monster escaped (removing objs after battle is done directly by endBattle, not this function)
	return true;
}

void CGameHandler::addQuest(const PlayerColor & player, const QuestInfo & quest)
{
	AddQuest aq;
	aq.player = player;
	aq.quest = quest;
	sendAndApply(aq);
}

bool CGameHandler::moveHero(ObjectInstanceID hid, int3 dst, EMovementMode movementMode, bool transit, PlayerColor asker, const EPathfindingLayer & layer)
{
	const CGHeroInstance *h = gameInfo().getHero(hid);
	// not turn of that hero or player can't simply teleport hero (at least not with this function)
	if(!h || (asker != PlayerColor::NEUTRAL && movementMode != EMovementMode::STANDARD))
	{
		if(h && gameInfo().getStartInfo()->turnTimerInfo.isEnabled() && gameState().players.at(h->getOwner()).turnTimer.turnTimer == 0)
			return true; //timer expired, no error

		logGlobal->error("Illegal call to move hero!");
		return false;
	}

	logGlobal->trace("Player %d (%s) wants to move hero %d from %s to %s", asker, asker.toString(), hid.getNum(), h->anchorPos().toString(), dst.toString());
	const int3 hmpos = h->convertToVisitablePos(dst);

	if (!gameState().getMap().isInTheMap(hmpos))
	{
		logGlobal->error("Destination tile is outside the map!");
		return false;
	}

	const TerrainTile t = *gameInfo().getTile(hmpos);
	const int3 preferredGuardPos = gameState().guardingCreaturePosition(hmpos);
	int3 guardPos(-1, -1, -1);
	const CGObjectInstance * objectToVisit = nullptr;
	const CGObjectInstance * guardian = nullptr;
	const CGObjectInstance * preferredGuardian = nullptr;

	if (!t.visitableObjects.empty())
		objectToVisit = gameState().getObjInstance(t.visitableObjects.back());

	if(gameState().getMap().isInTheMap(preferredGuardPos))
	{
		for(const auto * candidate : gameState().guardingCreatures(hmpos))
		{
			const int3 candidateVisitablePos = candidate->visitablePos();
			if(candidateVisitablePos == preferredGuardPos)
				preferredGuardian = candidate;

			if(candidate->passableFor(h))
				continue;

			if(!guardian || candidateVisitablePos == preferredGuardPos)
			{
				guardian = candidate;
				guardPos = candidateVisitablePos;
			}

			if(candidateVisitablePos == preferredGuardPos)
				break;
		}
	}

	const bool embarking = !h->inBoat() && objectToVisit && objectToVisit->ID == Obj::BOAT;

	bool disembarking = false;

	if(h->inBoat() && t.isLand())
	{
		const auto * boat = h->getBoat();

		const bool hasDisembarkIntent = (layer == EPathfindingLayer::LAND);

		// Ensure the destination tile is physically valid for the current vehicle
		const bool isStayingInPlace = (dst == h->pos);
		const bool isValidVehicleType = (boat->layer == EPathfindingLayer::SAIL || boat->layer == EPathfindingLayer::AVIATE);
		const bool isDestinationFree = !t.blocked();
		const bool isValidDestination = isStayingInPlace || (isValidVehicleType && isDestinationFree);

		disembarking = hasDisembarkIntent && isValidDestination;
	}

	//result structure for start - movement failed, no move points used
	TryMoveHero tmh;
	tmh.id = hid;
	tmh.start = h->pos;
	tmh.end = dst;
	tmh.result = TryMoveHero::FAILED;
	tmh.movePoints = h->movementPointsRemaining();

	const auto complainRet = [&](const std::string & message)
	{
		//send info about movement failure
		complain(message);
		sendAndApply(tmh);
		return false;
	};

	const bool requiresLayer = movementMode == EMovementMode::STANDARD && dst != h->pos;
	const bool hasValidLayer = layer >= EPathfindingLayer::LAND && layer < EPathfindingLayer::NUM_LAYERS;
	if(requiresLayer && !hasValidLayer)
		return complainRet("Invalid movement layer!");

	//check if destination tile is available
	auto pathfinderHelper = std::make_unique<CPathfinderHelper>(gameState(), h, PathfinderOptions(gameInfo()));
	const auto * ti = pathfinderHelper->getTurnInfo();

	const bool canFly = ti->hasFlyingMovement() || (h->inBoat() && (h->getBoat()->layer == EPathfindingLayer::AIR || h->getBoat()->layer == EPathfindingLayer::AVIATE));
	const bool canWalkOnSea = ti->hasWaterWalking() || (h->inBoat() && h->getBoat()->layer == EPathfindingLayer::WATER);

	const bool movingOntoObstacle = t.blocked() && !t.visitable();
	const bool objectCoastVisitable = objectToVisit && objectToVisit->isCoastVisitable();
	const bool movingOntoWater = !h->inBoat() && t.isWater() && !objectCoastVisitable;

	if(requiresLayer && ti->usesNewHorizonsMovement())
	{
		const bool boatCanSail = h->inBoat() && h->getBoat()->layer == EPathfindingLayer::SAIL;
		const bool boatCanFly = h->inBoat()
			&& (h->getBoat()->layer == EPathfindingLayer::AIR || h->getBoat()->layer == EPathfindingLayer::AVIATE);

		// `transit` controls whether the destination is visited; it does not
		// select the movement surface and is valid for both intermediate and
		// final AIR/WATER steps. The later generic transit check validates any
		// requested pass-through against actual travel capability.
		const bool transitLayer = layer == EPathfindingLayer::AIR
			|| layer == EPathfindingLayer::WATER || layer == EPathfindingLayer::AVIATE;
		bool validLayer = !transit || transitLayer || CGTeleport::isTeleport(objectToVisit);
		const bool coastVisitableOnWater = objectCoastVisitable && objectToVisit->ID != Obj::BOAT;
		switch(layer.toEnum())
		{
		case EPathfindingLayer::LAND:
			validLayer = validLayer && (t.isLand() || coastVisitableOnWater)
				&& !movingOntoObstacle && (!h->inBoat() || disembarking);
			break;
		case EPathfindingLayer::SAIL:
			validLayer = validLayer && t.isWater() && (boatCanSail || embarking
				|| (!h->inBoat() && !transit && pathfinderHelper->isCoastalBlockingVisit(
					*gameInfo().getTile(h->visitablePos()), t)));
			break;
		case EPathfindingLayer::AIR:
			validLayer = validLayer && canFly && (!h->inBoat() || boatCanFly);
			break;
		case EPathfindingLayer::WATER:
			validLayer = validLayer && t.isWater()
				&& ((h->inBoat() && h->getBoat()->layer == EPathfindingLayer::WATER)
					|| (!h->inBoat() && ti->hasWaterWalking()));
			break;
		case EPathfindingLayer::AVIATE:
			validLayer = validLayer && h->inBoat() && h->getBoat()->layer == EPathfindingLayer::AVIATE;
			break;
		default:
			validLayer = false;
			break;
		}

		if(!validLayer)
			return complainRet("Invalid movement layer for destination tile!");
	}

	const bool usesMovementCost = movementMode == EMovementMode::STANDARD || embarking || disembarking;
	const int cost = usesMovementCost
		? pathfinderHelper->getMovementCost(h->visitablePos(), hmpos, layer, h->movementPointsRemaining())
		: 0;

	if ((guardian && getVisitingHero(guardian) != nullptr)
		|| (preferredGuardian && preferredGuardian != guardian && getVisitingHero(preferredGuardian) != nullptr))
		return complainRet("You cannot move your hero there. Simultaneous turns are active and another player is interacting with this wandering monster!");

	if (objectToVisit && getVisitingHero(objectToVisit) != nullptr && getVisitingHero(objectToVisit) != h)
		return complainRet("You cannot move your hero there. Simultaneous turns are active and another player is interacting with this map object!");

	if (objectToVisit &&
		objectToVisit->getOwner().isValidPlayer())
	{
		if (gameInfo().getPlayerRelations(objectToVisit->getOwner(), h->getOwner()) == PlayerRelations::ENEMIES &&
		   !turnOrder->isContactAllowed(objectToVisit->getOwner(), h->getOwner()))
			return complainRet("You cannot move your hero there. This object belongs to another player and simultaneous turns are still active!");

		if (gs->getBattle(objectToVisit->getOwner()) != nullptr)
			return complainRet("You cannot move your hero there. This object belongs to another player who is engaged in battle and simultaneous turns are still active!");
	}

	//it's a rock or blocked and not visitable tile
	//OR hero is on land and dest is water and (there is not present only one object - boat)
	if (!t.getTerrain()->isPassable() || (movingOntoObstacle && !canFly))
		return complainRet("Cannot move hero, destination tile is blocked!");

	//hero is not on boat/water walking and dst water tile doesn't contain boat/hero (objs visitable from land) -> we test back cause boat may be on top of another object (#276)
	if(movingOntoWater && !canFly && !canWalkOnSea)
		return complainRet("Cannot move hero, destination tile is on water!");

	if(h->inBoat() && h->getBoat()->layer == EPathfindingLayer::SAIL && t.isLand())
	{
		if(t.blocked())
			return complainRet("Cannot disembark hero, tile is blocked!");

		//hole is neither visitable nor blocking, so check for it explicitly
		if(pathfinderHelper->isTileBlockedByHole(hmpos))
			return complainRet("Cannot disembark hero, tile contains a hole!");
	}

	if(!h->pos.areNeighbours(dst) && movementMode == EMovementMode::STANDARD)
		return complainRet("Tiles " + h->pos.toString()+ " and "+ dst.toString() +" are not neighboring!");

	if(h->isGarrisoned())
		return complainRet("Can not move garrisoned hero!");

	if(h->movementPointsRemaining() < cost && dst != h->pos && movementMode == EMovementMode::STANDARD)
		return complainRet("Hero doesn't have any movement points left!");

	if (transit && !canFly && !(canWalkOnSea && t.isWater()) && !CGTeleport::isTeleport(objectToVisit))
		return complainRet("Hero cannot transit over this tile!");

	//several generic blocks of code

	// should be called if hero changes tile but before applying TryMoveHero package
	auto leaveTile = [&]()
	{
		for(const auto & objID : gameState().getMap().getTile(h->visitablePos()).visitableObjects)
			gameState().getObjInstance(objID)->onHeroLeave(*this, h);

		gameInfo().getTilesInRange(tmh.fowRevealed, h->getSightCenter()+(tmh.end-tmh.start), h->getSightRadius(), ETileVisibility::HIDDEN, h->tempOwner);
	};

	auto doMove = [&](TryMoveHero::EResult result, EGuardLook lookForGuards,
								EVisitDest visitDest, ELEaveTile leavingTile) -> bool
	{
		LOG_TRACE_PARAMS(logGlobal, "Hero %s starts movement from %s to %s", h->getNameTextID() % tmh.start.toString() % tmh.end.toString());

		auto moveQuery = std::make_shared<CHeroMovementQuery>(this, tmh, h);
		queries->addQuery(moveQuery);

		if (leavingTile == LEAVING_TILE)
			leaveTile();

		if (lookForGuards == CHECK_FOR_GUARDS && gameInfo().isInTheMap(guardPos))
			tmh.attackedFrom = guardPos;

		tmh.result = result;
		sendAndApply(tmh);

		if (visitDest == VISIT_DEST && objectToVisit && objectToVisit->id == h->id)
		{ // Hero should be always able to visit any object he is staying on even if there are guards around
			visitObjectOnTile(t, h);
		}
		else if (lookForGuards == CHECK_FOR_GUARDS && gameInfo().isInTheMap(guardPos))
		{
			const ObjectInstanceID guardianId = guardian->id;
			const ObjectInstanceID objectToVisitId = objectToVisit ? objectToVisit->id : ObjectInstanceID::NONE;
			objectVisited(guardian, h);

			const auto calendar = gameInfo().getCalendar();
			const int week = newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
			const bool passedByPeacemaker = h->isNewHorizonsCreaturePacified(guardianId, week);
			const bool destinationIsGuardian = objectToVisitId == guardianId;
			const bool destinationWasRemoved = destinationIsGuardian && !gameInfo().getObjInstance(objectToVisitId);
			const bool destinationWasResolved = destinationIsGuardian && (passedByPeacemaker || destinationWasRemoved);
			moveQuery->visitDestAfterVictory = visitDest == VISIT_DEST && !destinationWasResolved;
		}
		else if (visitDest == VISIT_DEST)
		{
			visitObjectOnTile(t, h);
		}

		queries->popIfTop(moveQuery);
		logGlobal->trace("Hero %s ends movement", h->getNameTextID());
		return result != TryMoveHero::FAILED;
	};

	//interaction with blocking object (like resources)
	auto blockingVisit = [&]() -> bool
	{
		for (ObjectInstanceID objectID : t.visitableObjects)
		{
			const CGObjectInstance * object = gameInfo().getObj(objectID);

			if(h->inBoat() && !object->isBlockedVisitable() && !h->getBoat()->onboardVisitAllowed)
				return doMove(TryMoveHero::SUCCESS, this->IGNORE_GUARDS, DONT_VISIT_DEST, REMAINING_ON_TILE);

            const auto * questSource = object->asQuestSource();
			const bool stopsHero = object->isBlockedVisitable() || (questSource && questSource->requiresQuestToPass());

			if (object != h && stopsHero && !object->passableFor(h))
			{
				EVisitDest visitDest = VISIT_DEST;
				if(h->inBoat() && !h->getBoat()->onboardVisitAllowed)
					visitDest = DONT_VISIT_DEST;

				return doMove(TryMoveHero::BLOCKING_VISIT, this->IGNORE_GUARDS, visitDest, REMAINING_ON_TILE);
			}
		}
		return false;
	};

	if (settings["general"]["saveBeforeVisit"].Bool() &&
		gameInfo().getPlayerState(h->getOwner())->human &&
	   (guardian || objectToVisit) &&
	   movementMode == EMovementMode::STANDARD)
	{
		const auto savePath = SavegamePath::getPath(
			*gameInfo().getStartInfo(), *gameInfo().getMapHeader(), "BeforeVisitSave");
		save(savePath, PlayerColor::CANNOT_DETERMINE);
	}

	if (!transit && embarking)
	{
		tmh.movePoints = h->movementPointsAfterEmbark(h->movementPointsRemaining(), cost, false, ti);
		return doMove(TryMoveHero::EMBARK, IGNORE_GUARDS, DONT_VISIT_DEST, LEAVING_TILE);
		// In H3 embark ignore guards
	}

	if (disembarking)
	{
		tmh.movePoints = h->movementPointsAfterEmbark(h->movementPointsRemaining(), cost, true, ti);
		return doMove(TryMoveHero::DISEMBARK, CHECK_FOR_GUARDS, VISIT_DEST, LEAVING_TILE);
	}

	if (movementMode != EMovementMode::STANDARD)
	{
		// New Horizons Castle Gate travel consumes the hero's remaining
		// Movement for the day.  Set this before the blocking-visit path as
		// well, because a destination town can still go through that branch.
		if(movementMode == EMovementMode::CASTLE_GATE
			&& newHorizonsMagic::rulesActive(gameState().getMagicRules()))
			tmh.movePoints = 0;

		if (blockingVisit()) // e.g. hero on the other side of teleporter
			return true;

		EGuardLook guardsCheck = (gameInfo().getSettings().getBoolean(EGameSettings::SPELLS_DIMENSION_DOOR_TRIGGERS_GUARDS) && movementMode == EMovementMode::DIMENSION_DOOR)
			? CHECK_FOR_GUARDS
			: IGNORE_GUARDS;

		doMove(TryMoveHero::TELEPORTATION, guardsCheck, DONT_VISIT_DEST, LEAVING_TILE);

		// visit town for town portal / castle gates
		// do not visit any other objects, e.g. monoliths to avoid double-teleporting
		if (objectToVisit)
		{
			if (const auto * town = dynamic_cast<const CGTownInstance *>(objectToVisit))
				objectVisited(town, h);
		}
		return true;
	}

	//still here? it is standard movement!
	{
		tmh.movePoints = h->movementPointsRemaining() >= cost
						? h->movementPointsRemaining() - cost
						: 0;

		EGuardLook lookForGuards = CHECK_FOR_GUARDS;
		EVisitDest visitDest = VISIT_DEST;
		if (transit)
		{
			if (CGTeleport::isTeleport(objectToVisit))
				visitDest = DONT_VISIT_DEST;

			if (canFly || (canWalkOnSea && t.isWater()))
			{
				lookForGuards = IGNORE_GUARDS;
				visitDest = DONT_VISIT_DEST;
			}
		}
		else if (blockingVisit())
			return true;

		if(h->getBoat() && !h->getBoat()->onboardAssaultAllowed)
			lookForGuards = IGNORE_GUARDS;

		turnTimerHandler->setEndTurnAllowed(h->getOwner(), !movingOntoWater && !movingOntoObstacle);
		doMove(TryMoveHero::SUCCESS, lookForGuards, visitDest, LEAVING_TILE);
		statistics->getPlayerAccumulator(asker).movementPointsUsed += tmh.movePoints;
		return true;
	}
}

bool CGameHandler::teleportHero(ObjectInstanceID hid, ObjectInstanceID dstid, ui8 source, PlayerColor asker)
{
	const CGHeroInstance *h = gameState().getHero(hid);
	const CGTownInstance *t = gameInfo().getTown(dstid);

	if (!h || !t)
		COMPLAIN_RET("Invalid call to teleportHero!");

	const CGTownInstance *from = h->getVisitedTown();
	const bool newHorizonsCastleGate = newHorizonsMagic::rulesActive(gameState().getMagicRules());
	if (((h->getOwner() != t->getOwner())
		&& complain("Cannot teleport hero to another player"))

	|| (!from
		&& complain("Hero must be in town with Castle gate for teleporting"))

	|| (newHorizonsCastleGate
		&& (from == nullptr || from->getFactionID() != FactionID::INFERNO
			|| t->getFactionID() != FactionID::INFERNO)
		&& complain("New Horizons Castle Gates connect Inferno towns only"))

	|| (from && from->getFactionID() != t->getFactionID()
		&& complain("Source town and destination town should belong to the same faction"))

	|| ((!from || !from->hasBuilt(BuildingSubID::CASTLE_GATE))
		&& complain("Hero must be in town with Castle gate for teleporting"))

	|| (!t->hasBuilt(BuildingSubID::CASTLE_GATE)
		&& complain("Cannot teleport hero to town without Castle gate in it")))
			return false;

	if(newHorizonsCastleGate && h->hasUsedNewHorizonsCastleGateToday(gameState().getCalendar().getCurrentDay()))
		COMPLAIN_RET("This hero has already used a Castle Gate today");

	int3 pos = h->convertFromVisitablePos(t->visitablePos());
	if(!moveHero(hid,pos,EMovementMode::CASTLE_GATE))
		return false;

	if(newHorizonsCastleGate)
	{
		SetNewHorizonsCastleGateState state;
		state.hid = hid;
		state.lastUseDay = gameState().getCalendar().getCurrentDay();
		sendAndApply(state);
	}
	return true;
}

void CGameHandler::setOwner(const CGObjectInstance * obj, const PlayerColor owner)
{
	PlayerColor oldOwner = gameState().getOwner(obj->id);

	setObjPropertyID(obj->id, ObjProperty::OWNER, owner);

	std::set<PlayerColor> playerColors = {owner, oldOwner};
	checkVictoryLossConditions(playerColors);

	const CGTownInstance * town = dynamic_cast<const CGTownInstance *>(obj);
	if (town) //town captured
	{
		if(owner.isValidPlayer())
			statistics->getPlayerAccumulator(owner).lastCapturedTownDay = gameState().getCalendar().getCurrentDay();

		if (owner.isValidPlayer() && town->hasBuilt(BuildingSubID::PORTAL_OF_SUMMONING))
			setPortalDwelling(town, true, false);
	}

	if ((obj->ID == Obj::CREATURE_GENERATOR1 || obj->ID == Obj::CREATURE_GENERATOR4) && owner.isValidPlayer())
	{
		for (const CGTownInstance * t : gameInfo().getPlayerState(owner)->getTowns())
		{
			if (t->hasBuilt(BuildingSubID::PORTAL_OF_SUMMONING))
				setPortalDwelling(t);//set initial creatures for all portals of summoning
		}
	}
}

void CGameHandler::showBlockingDialog(const IObjectInterface * caller, BlockingDialog *iw)
{
	auto dialogQuery = std::make_shared<CBlockingDialogQuery>(this, caller, *iw);
	queries->addQuery(dialogQuery);
	iw->queryID = dialogQuery->queryID;
	sendAndApply(*iw);
}

void CGameHandler::showScriptDialog(BlockingDialog * iw)
{
	// The dialog sits above the paused script's query; its reply is stashed there and consumed when
	// the script query is exposed and resumes the coroutine.
	auto scriptQuery = std::dynamic_pointer_cast<LuaScriptQuery>(queries->topQuery(iw->player));
	if(!scriptQuery)
	{
		logGlobal->error("showScriptDialog called without an active script query for player %s", iw->player.toString());
		return;
	}

	auto dialogQuery = std::make_shared<CGenericQuery>(this, iw->player,
		[scriptQuery](std::optional<int32_t> reply){ scriptQuery->setPendingAnswer(reply); });
	queries->addQuery(dialogQuery);
	iw->queryID = dialogQuery->queryID;
	sendAndApply(*iw);
}

void CGameHandler::runScriptedEvent(scripting::MapEventDispatcher & dispatcher, PlayerColor player, ObjectInstanceID visitingHero,
	const std::function<std::optional<int>(scripting::MapEventDispatcher &)> & dispatch)
{
	// The script may pause on a blocking action; a LuaScriptQuery keeps its coroutine alive between
	// resumptions and stays on the stack (blocking the event from ending) until the script finishes.
	auto scriptQuery = std::make_shared<LuaScriptQuery>(this, player);
	if(visitingHero.hasValue())
		scriptQuery->setVisitingHero(visitingHero);
	queries->addQuery(scriptQuery);

	auto handle = dispatch(dispatcher);
	if(handle)
		scriptQuery->setCoroutine(*handle);
	else
		queries->popIfTop(scriptQuery);
}

void CGameHandler::showTeleportDialog(TeleportDialog *iw)
{
	auto dialogQuery = std::make_shared<CTeleportDialogQuery>(this, *iw);
	queries->addQuery(dialogQuery);
	iw->queryID = dialogQuery->queryID;
	sendAndApply(*iw);
}

void CGameHandler::setScriptVariable(const std::string & scope, const std::string & name, const JsonNode & value)
{
	SetScriptVariable pack;
	pack.scope = scope;
	pack.name = name;
	pack.value = value;
	sendAndApply(pack);
}

void CGameHandler::setQuestHintText(ObjectInstanceID obj, const MetaString & hint)
{
	SetQuestHint pack;
	pack.object = obj;
	pack.hint = hint;
	sendAndApply(pack);
}

void CGameHandler::giveResource(PlayerColor player, GameResID which, int val)
{
	if (!val)
		return; //don't waste time on empty call

	TResources resources;
	resources[which] = val;
	giveResources(player, resources);
}

void CGameHandler::giveResources(PlayerColor player, const ResourceSet & resources)
{
	if (resources.empty())
		return;

	SetResources sr;
	sr.mode = ChangeValueMode::RELATIVE;
	sr.player = player;
	sr.res = resources;
	sendAndApply(sr);
}

void CGameHandler::giveCreatures(const CGHeroInstance * hero, const CCreatureSet &creatures)
{
	if (!hero->canBeMergedWith(creatures, true))
	{
		complain("Unable to give creatures! Hero does not have enough free slots to receive them!");
		return;
	}
	if(!validateLeadershipArmyAddition(hero, creatures))
		return;

	for (const auto & unit : creatures.Slots())
	{
		SlotID pos = hero->getSlotFor(unit.second->getCreature());
		if (!pos.validSlot())
		{
			//try to merge two other stacks to make place
			std::pair<SlotID, SlotID> toMerge;
			if (hero->mergeableStacks(toMerge))
			{
				if(!moveStack(StackLocation(hero->id, toMerge.first), StackLocation(hero->id, toMerge.second)))
					return;
				pos = toMerge.first;
			}
		}
		if(!pos.validSlot() || (!hero->slotEmpty(pos) && hero->getCreature(pos) != unit.second->getCreature()))
		{
			complain("Unable to give creatures: no valid destination slot remained.");
			return;
		}

		if (hero->hasStackAtSlot(pos))
			changeStackCount(StackLocation(hero->id, pos), unit.second->getCount(), ChangeValueMode::RELATIVE);
		else
			insertNewStack(StackLocation(hero->id, pos), unit.second->getCreature(), unit.second->getCount());
	}
}

void CGameHandler::giveCreatures(const CArmedInstance *obj, const CGHeroInstance * h, const CCreatureSet &creatures, bool remove)
{
	COMPLAIN_RET_IF(!creatures.stacksCount(), "Strange, giveCreatures called without args!");
	COMPLAIN_RET_IF(obj->stacksCount(), "Cannot give creatures from not-cleared object!");
	COMPLAIN_RET_IF(creatures.stacksCount() > GameConstants::ARMY_SIZE, "Too many stacks to give!");
	// A full army with no mergeable slot must still reach tryJoiningArmy so the
	// player can choose an exchange. Individual transfers from that dialog are
	// validated authoritatively by moveStack. Preflight only when the complete
	// reward can be inserted without player intervention.
	if(h->canBeMergedWith(creatures, true) && !validateLeadershipArmyAddition(h, creatures))
		return;

	//first we move creatures to give to make them army of object-source
	for (const auto & elem : creatures.Slots())
	{
		addToSlot(StackLocation(obj->id, obj->getSlotFor(elem.second->getCreature())), elem.second->getCreature(), elem.second->getCount());
	}

	tryJoiningArmy(obj, h, remove, true);
}

void CGameHandler::takeCreatures(ObjectInstanceID objid, const std::vector<CStackBasicDescriptor> &creatures, bool forceRemoval)
{
	std::vector<CStackBasicDescriptor> remainerForTaking = creatures;
	if (remainerForTaking.empty())
		return;

	const auto * army = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(objid));

	for (const CStackBasicDescriptor &stackToTake : remainerForTaking)
	{
		TQuantity collected = 0;
		while(collected < stackToTake.getCount())
		{
			bool foundSth = false;
			for (const auto & armySlot : army->Slots())
			{
				if (armySlot.second->getType() == stackToTake.getType())
				{
					if (stackToTake.getCount() - collected >= armySlot.second->getCount())
					{
						// take entire stack
						collected += armySlot.second->getCount();
						eraseStack(StackLocation(army->id, armySlot.first), forceRemoval);
					}
					else
					{
						// take part of the stack
						changeStackCount(StackLocation(army->id, armySlot.first), collected - stackToTake.getCount(), ChangeValueMode::RELATIVE);
						collected = stackToTake.getCount();
					}
					foundSth = true;
					break;
				}
			}

			if (!foundSth) //we went through the whole loop and haven't found appropriate cres
			{
				complain("Unexpected failure during taking creatures!");
				return;
			}
		}
	}
}

void CGameHandler::heroVisitCastle(const CGTownInstance * obj, const CGHeroInstance * hero)
{
	if (obj->getVisitingHero() != hero && obj->getGarrisonHero() != hero)
	{
		HeroVisitCastle vc;
		vc.hid = hero->id;
		vc.tid = obj->id;
		vc.startVisit = true;
		sendAndApply(vc);
	}
	// Snapshot the meeting before visiting buildings, since those visits may
	// award experience or otherwise change a hero's level before Mentor resolves.
	auto learningMentorAward = prepareLearningMentorAward(obj->getVisitingHero(), obj->getGarrisonHero());
	visitCastleObjects(obj, hero);

	if (obj->getVisitingHero() && obj->getGarrisonHero())
		useScholarSkill(obj->getVisitingHero()->id, obj->getGarrisonHero()->id);
	grantLearningMentorAward(learningMentorAward);
	checkVictoryLossConditionsForPlayer(hero->tempOwner); //transported artifact?
}

void CGameHandler::visitCastleObjects(const CGTownInstance * t, const CGHeroInstance * h)
{
	std::vector<const CGHeroInstance * > visitors;
	visitors.push_back(h);
	visitCastleObjects(t, visitors);
}

void CGameHandler::visitCastleObjects(const CGTownInstance * t, const std::vector<const CGHeroInstance * > & visitors)
{
	std::vector<BuildingID> buildingsToVisit;
	// The Yard is an automatic town visit, so a hero already in town also receives
	// its effect when the building completes and this common visit path runs.
	t->grantBallistaYardSiegeBonus(*this, t->getVisitingHero());
	for (auto const & hero : visitors)
		giveSpells(t, hero, hero == t->getVisitingHero());

	for (const auto & building : t->rewardableBuildings)
	{
		if (!t->getTown()->buildings.at(building.first)->manualHeroVisit && t->hasBuilt(building.first))
			buildingsToVisit.push_back(building.first);
	}

	if (!buildingsToVisit.empty())
	{
		auto visitQuery = std::make_shared<TownBuildingVisitQuery>(this, t, visitors, buildingsToVisit);
		queries->addQuery(visitQuery);
	}
}

void CGameHandler::stopHeroVisitCastle(const CGTownInstance * obj, const CGHeroInstance * hero)
{
	HeroVisitCastle vc;
	vc.hid = hero->id;
	vc.tid = obj->id;
	sendAndApply(vc);
}

void CGameHandler::removeArtifact(const ArtifactLocation & al)
{
	removeArtifact(al.artHolder, {al.slot});
}

void CGameHandler::removeArtifact(const ObjectInstanceID & srcId, const std::vector<ArtifactPosition> & slotsPack)
{
	BulkEraseArtifacts ea;
	ea.artHolder = srcId;
	ea.posPack.insert(ea.posPack.end(), slotsPack.begin(), slotsPack.end());
	sendAndApply(ea);
}

void CGameHandler::changeSpells(const CGHeroInstance * hero, bool give, const std::set<SpellID> &spells)
{
	ChangeSpells cs;
	cs.hid = hero->id;
	cs.spells = spells;
	cs.learn = give;
	sendAndApply(cs);
}

void CGameHandler::setResearchedSpells(const CGTownInstance * town, int level, const std::vector<SpellID> & spells, bool accepted)
{
	SetResearchedSpells cs;
	cs.tid = town->id;
	cs.spells = spells;
	cs.level = level;
	cs.accepted = accepted;
	sendAndApply(cs);
}

void CGameHandler::giveHeroBonus(GiveBonus * bonus)
{
	sendAndApply(*bonus);
}

void CGameHandler::setMovePoints(SetMovePoints * smp)
{
	sendAndApply(*smp);
}

void CGameHandler::setMovePoints(ObjectInstanceID hid, int val)
{
	SetMovePoints smp;
	smp.hid = hid;
	smp.val = val;
	sendAndApply(smp);
}

void CGameHandler::setManaPoints(ObjectInstanceID hid, int val)
{
	SetMana sm(hid, SetMana::Operation::SET_NORMAL, std::max(val, 0));
	sendAndApply(sm);
}

void CGameHandler::restoreSpellPoints(ObjectInstanceID hid, int32_t amount)
{
	SetMana sm(hid, SetMana::Operation::RESTORE_NORMAL, amount);
	sendAndApply(sm);
}

void CGameHandler::spendSpellPoints(ObjectInstanceID hid, int64_t amount)
{
	SetMana sm(hid, SetMana::Operation::SPEND, amount);
	sendAndApply(sm);
}

void CGameHandler::grantBufferSpellPoints(ObjectInstanceID hid, int32_t amount)
{
	SetMana sm(hid, SetMana::Operation::GRANT_BUFFER, amount);
	sendAndApply(sm);
}

void CGameHandler::giveHero(ObjectInstanceID id, PlayerColor player, ObjectInstanceID boatId)
{
	GiveHero gh;
	gh.id = id;
	gh.player = player;
	gh.boatId = boatId;
	sendAndApply(gh);

	//Reveal fow around new hero, especially released from Prison
	const auto * h = gameInfo().getHero(id);
	changeFogOfWar(h->getSightCenter(), h->getSightRadius(), player, ETileVisibility::REVEALED);
}

void CGameHandler::changeObjPos(ObjectInstanceID objid, int3 newPos, const PlayerColor & initiator)
{
	ChangeObjPos cop;
	cop.objid = objid;
	cop.nPos = newPos;
	cop.initiator = initiator;
	sendAndApply(cop);
}

void CGameHandler::useScholarSkill(ObjectInstanceID fromHero, ObjectInstanceID toHero)
{
	const CGHeroInstance * h1 = gameInfo().getHero(fromHero);
	const CGHeroInstance * h2 = gameInfo().getHero(toHero);
	int h1_scholarSpellLevel = h1->valOfBonuses(BonusType::LEARN_MEETING_SPELL_LIMIT);
	int h2_scholarSpellLevel = h2->valOfBonuses(BonusType::LEARN_MEETING_SPELL_LIMIT);

	if (h1_scholarSpellLevel < h2_scholarSpellLevel)
	{
		std::swap (h1,h2);//1st hero need to have higher scholar level for correct message
		std::swap(fromHero, toHero);
	}

	int ScholarSpellLevel = std::max(h1_scholarSpellLevel, h2_scholarSpellLevel);//heroes can trade up to this level
	if (!ScholarSpellLevel || !h1->hasSpellbook() || !h2->hasSpellbook())
		return;//no scholar skill or no spellbook

	ChangeSpells cs1;
	cs1.learn = true;
	cs1.hid = toHero;//giving spells to first hero
	for (auto it : h1->getSpellsInSpellbook())
		if(ScholarSpellLevel >= h2->getSpellLevel(it.toSpell()) && h2->canLearnSpell(it.toSpell()))
			cs1.spells.insert(it);//spell to learn

	ChangeSpells cs2;
	cs2.learn = true;
	cs2.hid = fromHero;

	for (auto it : h2->getSpellsInSpellbook())
		if(ScholarSpellLevel >= h1->getSpellLevel(it.toSpell()) && h1->canLearnSpell(it.toSpell()))
			cs2.spells.insert(it);

	if (!cs1.spells.empty() || !cs2.spells.empty())//create a message
	{
		SecondarySkill scholarSkill = SecondarySkill::SCHOLAR;

		int scholarSkillLevel = std::max(h1->getSecSkillLevel(scholarSkill), h2->getSecSkillLevel(scholarSkill));
		InfoWindow iw;
		iw.player = h1->tempOwner;
		iw.components.emplace_back(ComponentType::SEC_SKILL, scholarSkill, scholarSkillLevel);

		iw.text.appendTextID("core.genrltxt.139");//"%s, who has studied magic extensively,
		iw.text.replaceTextID(h1->getNameTextID());

		if (!cs2.spells.empty())//if found new spell - apply
		{
			iw.text.appendTextID("core.genrltxt.140");//learns
			int size = cs2.spells.size();
			for (auto it : cs2.spells)
			{
				iw.components.emplace_back(ComponentType::SPELL, it);
				iw.text.appendName(it);
				switch (size--)
				{
					case 2:
						iw.text.appendTextID("core.genrltxt.141");
					case 1:
						break;
					default:
						iw.text.appendRawString(", ");
				}
			}
			iw.text.appendTextID("core.genrltxt.142");//from %s
			iw.text.replaceTextID(h2->getNameTextID());
			sendAndApply(cs2);
		}

		if (!cs1.spells.empty() && !cs2.spells.empty())
		{
			iw.text.appendTextID("core.genrltxt.141");//and
		}

		if (!cs1.spells.empty())
		{
			iw.text.appendTextID("core.genrltxt.147");//teaches
			int size = cs1.spells.size();
			for (auto it : cs1.spells)
			{
				iw.components.emplace_back(ComponentType::SPELL, it);
				iw.text.appendName(it);
				switch (size--)
				{
					case 2:
						iw.text.appendTextID("core.genrltxt.141");
					case 1:
						break;
					default:
						iw.text.appendRawString(", ");
				}
			}
			iw.text.appendTextID("core.genrltxt.148");//from %s
			iw.text.replaceTextID(h2->getNameTextID());
			sendAndApply(cs1);
		}
		sendAndApply(iw);
	}
}

void CGameHandler::heroExchange(ObjectInstanceID hero1, ObjectInstanceID hero2)
{
	const auto * h1 = gameInfo().getHero(hero1);
	const auto * h2 = gameInfo().getHero(hero2);

	if (gameInfo().getPlayerRelations(h1->getOwner(), h2->getOwner()) != PlayerRelations::ENEMIES)
	{
		auto learningMentorAward = prepareLearningMentorAward(h1, h2);
		auto exchange = std::make_shared<CGarrisonDialogQuery>(this, h1, h2);
		ExchangeDialog hex;
		hex.queryID = exchange->queryID;
		hex.player = h1->getOwner();
		hex.hero1 = hero1;
		hex.hero2 = hero2;
		sendAndApply(hex);

		useScholarSkill(hero1,hero2);
		queries->addQuery(exchange);
		// Keep the exchange beneath any level-up caused by Mentor's award.
		grantLearningMentorAward(learningMentorAward);
	}
}

std::optional<CGameHandler::LearningMentorAward> CGameHandler::prepareLearningMentorAward(
	const CGHeroInstance * first, const CGHeroInstance * second)
{
	if(!first || !second || first == second || first->id == second->id)
		return std::nullopt;
	if(gameInfo().getPlayerRelations(first->getOwner(), second->getOwner()) == PlayerRelations::ENEMIES)
		return std::nullopt;

	const std::string learningSkillId = "new-horizons:learning";
	const std::string mentorPerkId = "new-horizons:learning.mentor";
	const auto week = newHorizonsMuster::absoluteWeek(gameInfo().getCalendar().getCurrentDay(),
		gameInfo().getCalendar().getDaysInWeek());
	constexpr TExpType baseExperiencePerMentorLevel = 250;

	const auto tryMentor = [&](const CGHeroInstance * mentor, const CGHeroInstance * recipient)
		-> std::optional<LearningMentorAward>
	{
		const int32_t mentorLevel = mentor->level;
		const int32_t recipientLevel = recipient->level;
		if(mentorLevel <= recipientLevel)
			return std::nullopt;
		if(mentor->getPerkSkillRank(learningSkillId) <= 0
			|| !mentor->hasActivePerk(learningSkillId, mentorPerkId)
			|| mentor->hasUsedNewHorizonsLearningMentor(week))
			return std::nullopt;

		const auto baseExperience = baseExperiencePerMentorLevel * mentorLevel;
		return LearningMentorAward{
			mentor->id,
			recipient->id,
			mentorLevel,
			recipientLevel,
			week,
			recipient->calculateXp(baseExperience)
		};
	};

	if(auto award = tryMentor(first, second))
		return award;
	return tryMentor(second, first);
}

void CGameHandler::grantLearningMentorAward(const std::optional<LearningMentorAward> & award)
{
	if(!award || award->mentorLevelAtMeeting <= award->recipientLevelAtMeeting)
		return;

	const auto * mentor = gameInfo().getHero(award->mentorId);
	const auto * recipient = gameInfo().getHero(award->recipientId);
	if(!mentor || !recipient || mentor->hasUsedNewHorizonsLearningMentor(award->week))
		return;

	SetNewHorizonsLearningMentorState state;
	state.heroId = award->mentorId;
	state.lastUseWeek = award->week;
	// Mark before Experience is applied so nested encounter paths cannot spend
	// the same weekly use again while a level-up query is being created.
	sendAndApply(state);
	giveExperience(recipient, award->experience);
}

void CGameHandler::sendAndApply(CPackForClient & pack)
{
	if(auto * effects = dynamic_cast<SetStackEffect *>(&pack))
	{
		// Stamp accepted effect applications in the outgoing packet, so replicas
		// and detached forecasts receive the same ordering. Unmarked packets
		// keep their ordinary path; this is not a recurring battle-wide scan.
		std::set<uint32_t> markedUnits;
		const auto collectMarkedUnits = [&markedUnits](const auto & changes)
		{
			for(const auto & [unitId, bonuses] : changes)
				if(std::ranges::any_of(bonuses, [](const Bonus & bonus)
				{
					return bonus.type == BonusType::PHYSICAL_AFFLICTION;
				}))
					markedUnits.insert(unitId);
		};
		collectMarkedUnits(effects->toUpdate);
		collectMarkedUnits(effects->toAdd);
		if(!markedUnits.empty())
		{
			const auto * battle = gameState().getBattle(effects->battleID);
			if(!battle)
				throw std::runtime_error("Physical-affliction application requires a battle");
			for(const auto unitId : markedUnits)
			{
				const auto * unit = battle->battleGetUnitByID(unitId);
				if(!unit)
					throw std::runtime_error("Physical-affliction application requires a unit");
				std::vector<Bonus> removed;
				for(const auto & [removedId, bonuses] : effects->toRemove)
					if(removedId == unitId)
						removed.insert(removed.end(), bonuses.begin(), bonuses.end());
				std::vector<std::vector<Bonus> *> incoming;
				const auto collectIncoming = [&incoming, unitId](auto & changes)
				{
					for(auto & [changedId, bonuses] : changes)
						if(changedId == unitId)
							incoming.push_back(&bonuses);
				};
				collectIncoming(effects->toUpdate);
				collectIncoming(effects->toAdd);
				physicalAfflictions::stampEffectChanges(*unit, removed, incoming);
			}
		}
	}

	struct SoulChainEcho
	{
		BattleID battleID = BattleID::NONE;
		uint32_t primaryUnitId = 0;
		BattleSide casterSide = BattleSide::NONE;
		BattleSide victimSide = BattleSide::NONE;
		int32_t echoBasisPoints = 0;
		int64_t actualSecondaryDamage = 0;
	};
	std::vector<SoulChainEcho> echoes;
	struct PersonalBloodrageGain
	{
		BattleID battleID = BattleID::NONE;
		CreatureID creature = CreatureID::NONE;
		int32_t countBefore = 0;
	};
	std::map<uint32_t, PersonalBloodrageGain> personalBloodrageGains;
	const auto capturePersonalBloodrageGains = [&](const BattleID & battleID,
		const std::vector<BattleStackAttacked> & hits)
	{
		const auto * battleInfo = gameState().getBattle(battleID);
		if(!battleInfo)
			return;
		for(const auto & hit : hits)
		{
			const auto & earned = hit.newState.data["state"]["personalBloodrageIncrement"];
			if(hit.damageAmount <= 0 || !earned.isNumber() || earned.Float() <= 0)
				continue;
			const auto * unit = battleInfo->battleGetUnitByID(hit.stackAttacked);
			if(unit && unit->getPersonalBloodrageIncrement() == 0)
				personalBloodrageGains.emplace(unit->unitId(),
					PersonalBloodrageGain{battleID, unit->creatureId(), unit->getCount()});
		}
	};

	auto captureSoulChainTriggers = [&](const BattleID & battleID,
		const std::vector<BattleStackAttacked> & hits)
	{
		const auto * battleInfo = gameState().getBattle(battleID);
		if(!battleInfo || !newHorizonsSoulChain::isEnabled(battleInfo->getMagicRules()))
			return;

		for(const auto & hit : hits)
		{
			if(hit.damageAmount <= 0 || newHorizonsSoulChain::isEchoHit(hit))
				continue;

			const auto * secondary = battleInfo->battleGetUnitByID(hit.stackAttacked);
			const auto link = newHorizonsSoulChain::linkFor(secondary);
			if(!secondary || !link || link->primaryUnitId == secondary->unitId()
				|| link->casterSide == secondary->unitSide())
				continue;

			echoes.push_back({battleID, link->primaryUnitId, link->casterSide,
				secondary->unitSide(), link->echoBasisPoints, hit.damageAmount});
		}
	};

	// These are the two authoritative hit-bearing packet types. At present the
	// only BattleUnitsChanged::healthDelta producer is the debug victory path;
	// it is not an ordinary damage event and intentionally does not trigger this
	// combat reaction.
	if(const auto * attack = dynamic_cast<const BattleAttack *>(&pack))
	{
		captureSoulChainTriggers(attack->battleID, attack->bsa);
		capturePersonalBloodrageGains(attack->battleID, attack->bsa);
	}
	else if(const auto * injured = dynamic_cast<const StacksInjured *>(&pack))
	{
		captureSoulChainTriggers(injured->battleID, injured->stacks);
		capturePersonalBloodrageGains(injured->battleID, injured->stacks);
	}

	gameServer().applyPack(pack);
	for(const auto & [unitId, gain] : personalBloodrageGains)
	{
		const auto * battleInfo = gameState().getBattle(gain.battleID);
		const auto * unit = battleInfo ? battleInfo->battleGetUnitByID(unitId) : nullptr;
		if(!unit || unit->getPersonalBloodrageIncrement() <= 0)
			continue;
		BattleLogMessage log;
		log.battleID = gain.battleID;
		MetaString line = MetaString::createFromRawString("Rage Through Pain: ");
		line.appendNumber(gain.countBefore);
		line.appendRawString(" ");
		line.appendName(gain.creature, gain.countBefore);
		line.appendRawString(" gain one personal Bloodrage increment (");
		line.appendNumber(unit->getPersonalBloodrageIncrement());
		line.appendRawString(" percentage points; normal cap applies) for the rest of combat.");
		log.lines.push_back(std::move(line));
		sendAndApply(log);
	}

	if(echoes.empty())
		return;

	static const SpellID soulChainSpell(SpellID::decode(std::string(newHorizonsSoulChain::SPELL_ID)));
	for(const auto & echo : echoes)
	{
		const auto * battleInfo = gameState().getBattle(echo.battleID);
		if(!battleInfo || soulChainSpell == SpellID::NONE)
			continue;

		const auto * primary = battleInfo->battleGetUnitByID(echo.primaryUnitId);
		if(!primary || !primary->alive() || primary->unitSide() != echo.victimSide
			|| echo.casterSide == echo.victimSide)
			continue;

		const int64_t adjustedDamage = newHorizonsSoulChain::adjustedEchoDamage(*battleInfo,
			echo.casterSide, primary, echo.actualSecondaryDamage, echo.echoBasisPoints);
		if(adjustedDamage <= 0)
			continue;

		uint32_t sourceUnitId = primary->unitId();
		for(const auto * candidate : battleInfo->battleGetAllStacks(true))
			if(candidate && candidate->unitSide() == echo.casterSide)
			{
				sourceUnitId = candidate->unitId();
				break;
			}

		BattleStackAttacked hit;
		hit.attackerID = sourceUnitId;
		hit.stackAttacked = primary->unitId();
		hit.damageAmount = adjustedDamage;
		hit.flags = BattleStackAttacked::SPELL_EFFECT;
		hit.spellID = soulChainSpell;
		const auto primaryCreatureId = primary->creatureId();
		const int32_t primaryCountBeforeEcho = primary->getCount();
		CStack::prepareAttacked(hit, getRandomGenerator(), primary->acquireState(),
			false, false, battle::DamageProvenance::SPELL);

		StacksInjured injury;
		injury.battleID = echo.battleID;
		injury.stacks.push_back(hit);
		// Re-enter this bounded hook with an explicit spell-tagged packet; the
		// capture path above rejects it before another echo can be scheduled.
		sendAndApply(injury);

		BattleLogMessage log;
		log.battleID = echo.battleID;
		MetaString line = MetaString::createFromRawString("Soul Chain echoes ");
		line.appendNumber(hit.damageAmount);
		line.appendRawString(" Shadow damage onto ");
		line.appendNumber(primaryCountBeforeEcho);
		line.appendRawString(" ");
		line.appendName(primaryCreatureId, primaryCountBeforeEcho);
		line.appendRawString(".");
		log.lines.push_back(std::move(line));
		sendAndApply(log);
	}
}

void CGameHandler::sendQueryResolved(QueryID queryID)
{
	QueryResolved pack(queryID);
	sendAndApply(pack);
}

void CGameHandler::sendAndApply(CGarrisonOperationPack & pack)
{
	sendAndApply(static_cast<CPackForClient &>(pack));
	checkVictoryLossConditionsForAll();
}

void CGameHandler::sendAndApply(CArtifactOperationPack & pack)
{
	sendAndApply(static_cast<CPackForClient &>(pack));
	checkVictoryLossConditionsForAll();
}

void CGameHandler::sendAndApply(SetResources & pack)
{
	sendAndApply(static_cast<CPackForClient &>(pack));
	checkVictoryLossConditionsForPlayer(pack.player);
}

void CGameHandler::sendAndApply(NewStructures & pack)
{
	sendAndApply(static_cast<CPackForClient &>(pack));
	checkVictoryLossConditionsForPlayer(gameInfo().getTown(pack.tid)->tempOwner);
}

bool CGameHandler::isPlayerOwns(GameConnectionID connectionID, const CPackForServer * pack, ObjectInstanceID id)
{
	return pack->player == gameState().getOwner(id) && hasPlayerAt(gameState().getOwner(id), connectionID);
}

void CGameHandler::throwNotAllowedAction(GameConnectionID connectionID)
{
	playerMessages->sendSystemMessage(connectionID, MetaString::createFromTextID("vcmi.server.errors.notAllowed"));

	logNetwork->error("Player is not allowed to perform this action!");
	throw ExceptionNotAllowedAction();
}

void CGameHandler::wrongPlayerMessage(GameConnectionID connectionID, const CPackForServer * pack, PlayerColor expectedplayer)
{
	auto str = MetaString::createFromTextID("vcmi.server.errors.wrongIdentified");
	str.replaceName(pack->player);
	str.replaceName(expectedplayer);
	logNetwork->error("Expected player %s but got player %s!", expectedplayer.toString(), pack->player.toString());

	playerMessages->sendSystemMessage(connectionID, str);
}

void CGameHandler::throwIfWrongOwner(GameConnectionID connectionID, const CPackForServer * pack, ObjectInstanceID id)
{
	if(!isPlayerOwns(connectionID, pack, id))
	{
		wrongPlayerMessage(connectionID, pack, gameState().getOwner(id));
		throwNotAllowedAction(connectionID);
	}
}

void CGameHandler::throwIfPlayerNotActive(GameConnectionID connectionID, const CPackForServer * pack)
{
	if (!vstd::contains(gs->actingPlayers, pack->player))
		throwNotAllowedAction(connectionID);
}

void CGameHandler::throwIfWrongPlayer(GameConnectionID connectionID, const CPackForServer * pack)
{
	throwIfWrongPlayer(connectionID, pack, pack->player);
}

void CGameHandler::throwIfWrongPlayer(GameConnectionID connectionID, const CPackForServer * pack, PlayerColor player)
{
	if(!hasPlayerAt(player, connectionID) || pack->player != player)
	{
		wrongPlayerMessage(connectionID, pack, player);
		throwNotAllowedAction(connectionID);
	}
}

void CGameHandler::throwAndComplain(GameConnectionID connectionID, const std::string & txt)
{
	complain(txt);
	throwNotAllowedAction(connectionID);
}

bool CGameHandler::responseStatistic(PlayerColor player)
{
	ResponseStatistic rs;
	rs.statistic = *statistics;
	rs.player = player;

	const TeamState * team = gameState().getPlayerTeam(player);
	rs.statistic.filterByTeam(team);

	sendAndApply(rs);

	return true;
}

namespace
{
struct AutosaveFile
{
	ResourcePath path;
	std::time_t lastWrite;
};

std::string getDirectoryName(const std::string & path)
{
	const size_t separator = path.find_last_of("/\\");
	return separator == std::string::npos ? std::string() : path.substr(0, separator);
}

void pruneAutosaves(const ResourcePath & currentAutosave, int countLimit)
{
	if(countLimit <= 0 || !SavegamePath::isAutosaveName(currentAutosave.getOriginalName()))
		return;

	const std::string gameDirectory = getDirectoryName(currentAutosave.getName());
	auto * filesystem = CResourceHandler::get("local");
	std::vector<AutosaveFile> autosaves;
	const auto resources = filesystem->getFilteredFiles([&gameDirectory](const ResourcePath & resource)
	{
		return resource.getType() == EResType::SAVEGAME
			&& getDirectoryName(resource.getName()) == gameDirectory
			&& SavegamePath::isAutosaveName(resource.getOriginalName());
	});

	for(const auto & resource : resources)
	{
		try
		{
			autosaves.push_back({resource, filesystem->getLastWriteTime(resource)});
		}
		catch(const boost::filesystem::filesystem_error & e)
		{
			logGlobal->warn("Failed to get modification time of autosave %s: %s",
				resource.getOriginalName(), e.what());
		}
	}

	if(autosaves.size() <= static_cast<size_t>(countLimit))
		return;

	std::ranges::sort(autosaves, [&currentAutosave](const AutosaveFile & left, const AutosaveFile & right)
	{
		if(left.lastWrite != right.lastWrite)
			return left.lastWrite < right.lastWrite;
		if(left.path == currentAutosave)
			return false;
		if(right.path == currentAutosave)
			return true;
		return left.path < right.path;
	});

	const size_t filesToRemove = autosaves.size() - static_cast<size_t>(countLimit);
	for(size_t index = 0; index < filesToRemove; ++index)
	{
		if(filesystem->removeResource(autosaves[index].path))
			logGlobal->info("Removed old autosave %s", autosaves[index].path.getOriginalName());
		else
			logGlobal->warn("Failed to remove old autosave %s", autosaves[index].path.getOriginalName());
	}
}

}

void CGameHandler::save(const std::string & filename, PlayerColor playerToNotifyOnSuccess, int autosaveCountLimit)
{
	logGlobal->info("Saving to %s", filename);
	ResourcePath savePath(filename, EResType::SAVEGAME);
	const auto savefname = savePath.getOriginalName() + ".vsgm1";
	CResourceHandler::get("local")->createResource(savefname);

	std::string filenameWithoutPath;
	auto pos = filename.find_last_of("/\\");
	if (pos != std::string::npos)
		filenameWithoutPath = filename.substr(pos + 1);
	else
		filenameWithoutPath = filename;
	InfoWindow iw;
	iw.player = playerToNotifyOnSuccess;

	try
	{
		CSaveFile save;
		gameState().saveGame(save);
		logGlobal->info("Saving server state");
		save.save(*this);
		const auto saveFile = *CResourceHandler::get("local")->getResourceName(savePath);
		save.write(saveFile);

		pruneAutosaves(savePath, autosaveCountLimit);

		if(playerToNotifyOnSuccess.isValidPlayer())
		{
			iw.text = MetaString::createFromTextID("core.genrltxt.350");
			iw.text.replaceRawString(filenameWithoutPath);
			sendAndApply(iw);
		}
		logGlobal->info("Game has been successfully saved!");
	}
	catch(std::exception &e)
	{
		if(playerToNotifyOnSuccess.isValidPlayer())
		{
			iw.text = MetaString::createFromTextID("core.genrltxt.9");
			iw.text.replaceRawString(filenameWithoutPath);
			sendAndApply(iw);
		}
		logGlobal->error("Failed to save game: %s", e.what());
	}
}

void CGameHandler::load(const StartInfo &info)
{
	logGlobal->info("Loading from %s", info.mapname);

	CLoadFile lf(*CResourceHandler::get()->getResourceName(ResourcePath(info.mapname, EResType::SAVEGAME)), gs.get());
	gs = std::make_shared<CGameState>();
	randomizer = std::make_unique<GameRandomizer>(*gs);
	gs->loadGame(lf);
	logGlobal->info("Loading server state");
	lf.load(*this);
	logGlobal->info("Game has been successfully loaded!");

	gs->preInit(LIBRARY);
	gs->updateOnLoad(info);
	auto * startInfo = gs->getStartInfo();
	if(startInfo->campState && startInfo->campState->getStartTime() == 0)
		startInfo->campState->setStartTime(startInfo->startTime);
	gs->setSaveDirectory(SavegamePath::generateGameDirectoryName(*startInfo, *gs->getMapHeader()));

	configureReplayLog(false);
}

void CGameHandler::configureReplayLog(bool gameIsNew)
{
	const int roundsKept = std::max(0, static_cast<int>(settings["server"]["replayRoundsKept"].Integer()));

	// a loaded game keeps the recording mode it was started with
	if(gameIsNew)
		gs->replayLog.configure(gs->getStartInfo()->extraOptionsInfo.recordGame, roundsKept);
	else
		gs->replayLog.reconfigureOnLoad(roundsKept);
}

bool CGameHandler::bulkSplitStack(SlotID slotSrc, ObjectInstanceID srcOwner, si32 howMany)
{
	if(!slotSrc.validSlot() && complain(complainInvalidSlot))
		return false;

	const auto * army = dynamic_cast<const CArmedInstance*>(gameInfo().getObjInstance(srcOwner));
	const CCreatureSet & creatureSet = *army;

	if((!vstd::contains(creatureSet.stacks, slotSrc) && complain(complainNoCreatures))
		|| (howMany < 1 && complain("Invalid split parameter!")))
	{
		return false;
	}
	auto actualAmount = army->getStackCount(slotSrc);

	if(actualAmount <= howMany && complain(complainNotEnoughCreatures)) // '<=' because it's not intended just for moving a stack
		return false;

	auto freeSlots = creatureSet.getFreeSlots();

	if(freeSlots.empty() && complain("No empty stacks"))
		return false;
	if(!validateLeadershipStack(army, creatureSet.getCreature(slotSrc)->getId(), howMany))
		return false;

	BulkRebalanceStacks bulkRS;

	for(auto slot : freeSlots)
	{
		RebalanceStacks rs;
		rs.srcArmy = army->id;
		rs.dstArmy = army->id;
		rs.srcSlot = slotSrc;
		rs.dstSlot = slot;
		rs.count = howMany;

		bulkRS.moves.push_back(rs);
		actualAmount -= howMany;

		if(actualAmount <= howMany)
			break;
	}
	sendAndApply(bulkRS);
	return true;
}

bool CGameHandler::bulkMergeStacks(SlotID slotSrc, ObjectInstanceID srcOwner)
{
	if(!slotSrc.validSlot() && complain(complainInvalidSlot))
		return false;

	const auto * army = dynamic_cast<const CArmedInstance*>(gameInfo().getObjInstance(srcOwner));
	if(!army && complain("Cannot merge stacks in a non-existing army!"))
		return false;
	const CCreatureSet & creatureSet = *army;

	if(!vstd::contains(creatureSet.stacks, slotSrc) && complain(complainNoCreatures))
		return false;

	auto actualAmount = creatureSet.getStackCount(slotSrc);

	if(actualAmount < 1 && complain(complainNoCreatures))
		return false;

	const auto * currentCreature = creatureSet.getCreature(slotSrc);

	if(!currentCreature && complain(complainNoCreatures))
		return false;

	auto creatureSlots = creatureSet.getCreatureSlots(currentCreature, slotSrc);

	if(creatureSlots.empty())
		return false;

	int64_t maximumCount = std::numeric_limits<TQuantity>::max();
	if(const auto * hero = dynamic_cast<const CGHeroInstance *>(army))
	{
		const auto capacity = hero->getLeadershipSlotCapacity(currentCreature->getId());
		if(capacity)
			maximumCount = std::min<int64_t>(maximumCount, capacity->maximum);
	}
	const int64_t targetCount = creatureSet.getStackCount(slotSrc);
	int64_t remainingCapacity = std::max<int64_t>(0, maximumCount - targetCount);
	if(remainingCapacity == 0)
	{
		if(!validateLeadershipStack(army, currentCreature->getId(), targetCount + 1))
			return false;
		complain("Cannot exceed the maximum stack size!");
		return false;
	}

	BulkRebalanceStacks bulkRS;

	for(auto slot : creatureSlots)
	{
		const int64_t transfer = std::min<int64_t>(creatureSet.getStackCount(slot), remainingCapacity);
		if(transfer <= 0)
			break;

		RebalanceStacks rs;
		rs.srcArmy = army->id;
		rs.dstArmy = army->id;
		rs.srcSlot = slot;
		rs.dstSlot = slotSrc;
		rs.count = static_cast<TQuantity>(transfer);
		bulkRS.moves.push_back(rs);
		remainingCapacity -= transfer;
	}
	if(bulkRS.moves.empty())
		return false;

	sendAndApply(bulkRS);
	return true;
}

bool CGameHandler::bulkMoveArmy(ObjectInstanceID srcArmy, ObjectInstanceID destArmy, SlotID srcSlot)
{
	if(!srcSlot.validSlot() && complain(complainInvalidSlot))
		return false;

	if(!isAllowedExchange(srcArmy, destArmy))
		COMPLAIN_RET("That heroes cannot make any exchange!");

	const auto * armySrc = dynamic_cast<const CArmedInstance*>(gameInfo().getObjInstance(srcArmy));
	const auto * armyDest = dynamic_cast<const CArmedInstance*>(gameInfo().getObjInstance(destArmy));

	if(!vstd::contains(armySrc->stacks, srcSlot) && complain(complainNoCreatures))
		return false;

	auto freeSlots = armyDest->getFreeSlots();
	bool allTroopsMoved = true;
	std::map<SlotID, TQuantity> plannedDestinationCounts;
	for(const auto & slot : armyDest->Slots())
		plannedDestinationCounts[slot.first] = slot.second->getCount();

	BulkRebalanceStacks bulkRS;

	for (const auto & slot : armySrc->Slots())
	{
		auto targetSlot = armyDest->getSlotFor(slot.second->getCreature());

		if (armyDest->slotEmpty(targetSlot))
		{
			if (freeSlots.empty())
			{
				allTroopsMoved = false;
				continue; // no more free slots, but we might still have units that are present in both armies
			}

			targetSlot = freeSlots.front();
			freeSlots.erase(freeSlots.begin());
		}

		RebalanceStacks rs;
		rs.srcArmy = armySrc->id;
		rs.dstArmy = armyDest->id;
		rs.srcSlot = slot.first;
		rs.dstSlot = targetSlot;
		rs.count = slot.second->getCount();
		bulkRS.moves.push_back(rs);
	}

	// all troops were moved, but we can't leave source hero without troops - undo movement of 1 unit from srcSlot
	if (allTroopsMoved)
	{
		if (armySrc->getStack(srcSlot).getCount() == 1)
		{
			// slot only had 1 unit - remove this move completely
			vstd::erase_if(bulkRS.moves, [srcSlot](const RebalanceStacks & move)
			{
				return move.srcSlot == srcSlot;
			});
		}
		else
		{
			// slot has multiple units - move all but one
			for (auto & move : bulkRS.moves)
			{
				if (move.srcSlot == srcSlot)
					move.count -= 1;
			}
		}
	}
	for(const auto & move : bulkRS.moves)
	{
		const auto creature = armySrc->getCreature(move.srcSlot)->getId();
		plannedDestinationCounts[move.dstSlot] += move.count;
		if(!validateLeadershipStack(armyDest, creature, plannedDestinationCounts[move.dstSlot]))
			return false;
	}

	sendAndApply(bulkRS);
	return true;
}

bool CGameHandler::bulkSplitAndRebalanceStack(SlotID slotSrc, ObjectInstanceID srcOwner)
{
	if(!slotSrc.validSlot() && complain(complainInvalidSlot))
		return false;

	const auto * army = dynamic_cast<const CArmedInstance*>(gameInfo().getObjInstance(srcOwner));
	const CCreatureSet & creatureSet = *army;

	if(!vstd::contains(creatureSet.stacks, slotSrc) && complain(complainNoCreatures))
		return false;

	auto actualAmount = creatureSet.getStackCount(slotSrc);

	if(actualAmount <= 1 && complain(complainNoCreatures))
		return false;

	auto freeSlot = creatureSet.getFreeSlot();
	const auto * currentCreature = creatureSet.getCreature(slotSrc);

	if(freeSlot == SlotID() && creatureSet.isCreatureBalanced(currentCreature))
		return true;

	auto creatureSlots = creatureSet.getCreatureSlots(currentCreature, slotSrc, 1); // Ignore slots where's only 1 creature
	TQuantity totalCreatures = creatureSet.getStackCount(slotSrc);

	for(auto slot : creatureSlots)
		totalCreatures += creatureSet.getStackCount(slot);

	if(totalCreatures <= 1 && complain("Total creatures number is invalid"))
		return false;

	BulkRebalanceStacks bulkSRS;

	// 1) merge all but one creatures back into source slot
	// single creature needs to be kept, to avoid stack artifact dropping to hero backpack
	for(auto slot : creatureSlots)
	{
		RebalanceStacks rs;
		rs.srcArmy = army->id;
		rs.dstArmy = army->id;
		rs.srcSlot = slot;
		rs.dstSlot = slotSrc;
		rs.count = creatureSet.getStackCount(slot) - 1;

		if (rs.count > 0)
			bulkSRS.moves.push_back(rs);
	}

	// 2) split off single creature into new slot, if any
	// strictly speaking, not needed, but more convenient
	if(freeSlot != SlotID())
	{
		RebalanceStacks rs;
		rs.srcArmy = army->id;
		rs.dstArmy = army->id;
		rs.srcSlot = slotSrc;
		rs.dstSlot = freeSlot;
		rs.count = 1;
		bulkSRS.moves.push_back(rs);

		creatureSlots.push_back(freeSlot);
	}

	if(creatureSlots.empty() && complain("No available slots for smart rebalancing"))
		return false;

	int slotsLeft = creatureSlots.size() + 1; // + srcSlot
	TQuantity unitsToMove = totalCreatures - slotsLeft;
	const TQuantity largestFinalStack = vstd::divideAndCeil(totalCreatures, slotsLeft);
	if(!validateLeadershipStack(army, currentCreature->getId(), largestFinalStack))
		return false;

	// 3) re-split creatures in a balanced way
	for(auto slot : creatureSlots)
	{
		RebalanceStacks rs;

		rs.srcArmy = army->id;
		rs.dstArmy = army->id;
		rs.srcSlot = slotSrc;
		rs.dstSlot = slot;
		rs.count = vstd::divideAndCeil(unitsToMove, slotsLeft);
		bulkSRS.moves.push_back(rs);

		unitsToMove -= rs.count;
		slotsLeft -= 1;
	}

	sendAndApply(bulkSRS);
	return true;
}

bool CGameHandler::arrangeStacks(ObjectInstanceID id1, ObjectInstanceID id2, ui8 what, SlotID p1, SlotID p2, si32 val, PlayerColor player)
{
	const auto * s1 = dynamic_cast<const CArmedInstance *>(gameInfo().getObj(id1));
	const auto * s2 = dynamic_cast<const CArmedInstance *>(gameInfo().getObj(id2));

	if (s1 == nullptr || s2 == nullptr)
	{
		complain("Cannot exchange stacks between non-existing objects!!\n");
		return false;
	}

	const CCreatureSet & S1 = *s1;
	const CCreatureSet & S2 = *s2;
	StackLocation sl1(s1->id, p1);
	StackLocation sl2(s2->id, p2);

	if (!sl1.slot.validSlot()  ||  !sl2.slot.validSlot())
	{
		complain(complainInvalidSlot);
		return false;
	}

	if (!isAllowedExchange(id1,id2))
	{
		complain("Cannot exchange stacks between these two objects!\n");
		return false;
	}

	// We can always put stacks into locked garrison, but not take them out of it
	auto notRemovable = [&](const CArmedInstance * army)
	{
		if (id1 != id2) // Stack arrangement inside locked garrison is allowed
		{
			const auto * g = dynamic_cast<const CGGarrison *>(army);
			if (g && !g->removableUnits)
			{
				complain("Stacks in this garrison are not removable!\n");
				return true;
			}
		}
		return false;
	};

	if (what==1) //swap
	{
		if (((s1->tempOwner != player && s1->tempOwner != PlayerColor::UNFLAGGABLE) && s1->getStackCount(p1))
		  || ((s2->tempOwner != player && s2->tempOwner != PlayerColor::UNFLAGGABLE) && s2->getStackCount(p2)))
		{
			complain("Can't take troops from another player!");
			return false;
		}

		if (sl1.army == sl2.army && sl1.slot == sl2.slot)
		{
			complain("Cannot swap stacks - slots are the same!");
			return false;
		}

		if (!s1->slotEmpty(p1) && !s2->slotEmpty(p2))
		{
			if (notRemovable(s1) || notRemovable(s2))
				return false;
		}
		if (s1->slotEmpty(p1) && notRemovable(s2))
			return false;
		else if (s2->slotEmpty(p2) && notRemovable(s1))
			return false;

		return swapStacks(sl1, sl2);
	}
	else if (what==2)//merge
	{
		if (sl1.army == sl2.army && sl1.slot == sl2.slot)
		{
			complain("Cannot merge a stack with itself!");
			return false;
		}

		if ((s1->getCreature(p1) != s2->getCreature(p2) && complain("Cannot merge different creatures stacks!"))
		|| (((s1->tempOwner != player && s1->tempOwner != PlayerColor::UNFLAGGABLE) && s2->getStackCount(p2)) && complain("Can't take troops from another player!")))
			return false;

		if (s1->slotEmpty(p1) || s2->slotEmpty(p2))
		{
			complain("Cannot merge empty stack!");
			return false;
		}
		else if (notRemovable(s1))
			return false;

		const TQuantity sourceCount = s1->getStackCount(p1);
		const TQuantity destinationCount = s2->getStackCount(p2);
		int64_t maximumCount = std::numeric_limits<TQuantity>::max();
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(s2))
		{
			const auto capacity = hero->getLeadershipSlotCapacity(s1->getCreature(p1)->getId());
			if(capacity)
				maximumCount = std::min<int64_t>(maximumCount, capacity->maximum);
		}
		const int64_t availableCapacity = std::max<int64_t>(0, maximumCount - destinationCount);
		int64_t transferCount = std::min<int64_t>(sourceCount, availableCapacity);
		const bool mustKeepLastSourceCreature = id1 != id2 && s1->needsLastStack() && s1->stacksCount() == 1;
		if(mustKeepLastSourceCreature)
			transferCount = std::min<int64_t>(transferCount, sourceCount - 1);

		if(transferCount == 0)
		{
			if(const auto * hero = dynamic_cast<const CGHeroInstance *>(s2))
			{
				const auto capacity = hero->getLeadershipSlotCapacity(s1->getCreature(p1)->getId());
				if(capacity && destinationCount >= capacity->maximum)
					return validateLeadershipStack(s2, s1->getCreature(p1)->getId(), static_cast<int64_t>(destinationCount) + 1);
			}
			if(destinationCount == std::numeric_limits<TQuantity>::max())
				complain("Cannot exceed the maximum stack size!");
			else if(mustKeepLastSourceCreature)
				complain("Cannot move away the last creature!");
			else
				complain("Cannot merge these stacks!");
			return false;
		}

		return moveStack(sl1, sl2, static_cast<TQuantity>(transferCount));
	}
	else if (what==3) //split
	{
		if(!vstd::contains(S1.stacks, p1) && complain(complainNoCreatures))
			return false;
		if(val < 1 && complain(complainNoCreatures))
			return false;

		const int64_t sourceCount = s1->getStackCount(p1);
		const int64_t destinationCount = s2->getStackCount(p2);
		const int64_t countToMove = static_cast<int64_t>(val) - destinationCount;
		if(vstd::contains(S2.stacks, p2) && countToMove < 0)
		{
			complain("Cannot reduce the destination stack with a split request!");
			return false;
		}
		if(vstd::contains(S2.stacks, p2) && countToMove == 0)
			return true;
		if(countToMove < 0 || countToMove > sourceCount)
		{
			complain("Cannot split that stack, not enough creatures!");
			return false;
		}
		const int64_t countLeftOnSrc = sourceCount - countToMove;

		if (  (s1->tempOwner != player && countLeftOnSrc < s1->getStackCount(p1))
			|| (s2->tempOwner != player && val < s2->getStackCount(p2)))
		{
			complain("Can't move troops of another player!");
			return false;
		}

		if (vstd::contains(S2.stacks,p2))	 //dest. slot not free - it must be "rebalancing"...
		{
			const int64_t total = sourceCount + destinationCount;
			if ((total < val   &&   complain("Cannot split that stack, not enough creatures!"))
				|| (s1->getCreature(p1) != s2->getCreature(p2) && complain("Cannot rebalance different creatures stacks!"))
			)
			{
				return false;
			}

			if (notRemovable(s1))
			{
				if (s1->getStackCount(p1) > countLeftOnSrc)
					return false;
			}
			else if (notRemovable(s2))
			{
				if (s2->getStackCount(p1) < countLeftOnSrc)
					return false;
			}

			return moveStack(sl1, sl2, static_cast<TQuantity>(countToMove));
			//S2.slots[p2]->count = val;
			//S1.slots[p1]->count = total - val;
		}
		else //split one stack to the two
		{
			if (s1->getStackCount(p1) < val)//not enough creatures
			{
				complain(complainNotEnoughCreatures);
				return false;
			}

			if (notRemovable(s1))
				return false;

			return moveStack(sl1, sl2, val);
		}

	}
	return true;
}

bool CGameHandler::hasPlayerAt(PlayerColor player,  GameConnectionID connectionID) const
{
	return gameServer().hasPlayerAt(player, connectionID);
}

bool CGameHandler::hasBothPlayersAtSameConnection(PlayerColor left, PlayerColor right) const
{
	return gameServer().hasBothPlayersAtSameConnection(left, right);
}

bool CGameHandler::disbandCreature(ObjectInstanceID id, SlotID pos)
{
	const auto * s1 = dynamic_cast<const CArmedInstance *>(gameInfo().getObjInstance(id));
	if (!vstd::contains(s1->stacks,pos))
	{
		complain("Illegal call to disbandCreature - no such stack in army!");
		return false;
	}

	eraseStack(StackLocation(s1->id, pos));
	return true;
}

void CGameHandler::buildStructureForced(ObjectInstanceID townID, BuildingID building)
{
	buildStructure(townID, building, true);
}

bool CGameHandler::buildStructure(ObjectInstanceID tid, BuildingID requestedID, bool force)
{
	const CGTownInstance * t = gameInfo().getTown(tid);
	if(!t)
		COMPLAIN_RETF("No such town (ID=%s)!", tid);
	if(!t->getTown()->buildings.count(requestedID))
		COMPLAIN_RETF("Town of faction %s does not have info about building ID=%s!", t->getFaction()->getNameTranslated() % requestedID);
	if(t->hasBuilt(requestedID))
		COMPLAIN_RETF("Building %s is already built in %s", t->getTown()->buildings.at(requestedID)->getNameTranslated() % t->getNameTextID());

	const auto & requestedBuilding = t->getTown()->buildings.at(requestedID);

	//Vector with future list of built building and buildings in auto-mode that are not yet built.
	std::vector<const CBuilding*> remainingAutoBuildings;
	std::set<BuildingID> buildingsThatWillBe;

	//Check validity of request
	if(!force)
	{
		switch(requestedBuilding->mode)
		{
		case CBuilding::BUILD_NORMAL :
			if (gameState().canBuildStructure(t, requestedID) != EBuildingState::ALLOWED)
				COMPLAIN_RET("Cannot build that building!");
			break;

		case CBuilding::BUILD_AUTO   :
		case CBuilding::BUILD_SPECIAL:
			COMPLAIN_RET("This building can not be constructed normally!");

		case CBuilding::BUILD_GRAIL  :
			if(requestedBuilding->mode == CBuilding::BUILD_GRAIL) //needs grail
			{
				if(!t->getVisitingHero() || !t->getVisitingHero()->hasArt(ArtifactID::GRAIL))
					COMPLAIN_RET("Cannot build this without grail!")
				else
					removeArtifact(ArtifactLocation(t->getVisitingHero()->id, t->getVisitingHero()->getArtPos(ArtifactID::GRAIL, false)));
			}
			break;
		}
	}

	//Performs stuff that has to be done before new building is built
	auto processBeforeBuiltStructure = [t, this](const BuildingID buildingID)
	{
		if(buildingID.isDwelling())
		{
			int level = BuildingID::getLevelIndexFromDwelling(buildingID);
			int upgradeNumber = BuildingID::getUpgradeNoFromDwelling(buildingID);

			if(upgradeNumber >= t->getTown()->creatures.at(level).size())
			{
				complain(boost::str(boost::format("Error encountered when building dwelling (bid=%s):"
													"no creature found (upgrade number %d, level %d!")
												% buildingID % upgradeNumber % level));
				return;
			}

			const CCreature * crea = t->getTown()->creatures.at(level).at(upgradeNumber).toCreature();

			SetAvailableCreatures ssi;
			ssi.tid = t->id;
			ssi.creatures = t->creatures;
			if (ssi.creatures[level].second.empty()) // first creature in a dwelling
				ssi.creatures[level].first = gameInfo().getCreatureBaseGrowth(crea->getId());
			ssi.creatures[level].second.push_back(crea->getId());
			sendAndApply(ssi);
		}
		if(t->getTown()->buildings.at(buildingID)->subId == BuildingSubID::PORTAL_OF_SUMMONING)
		{
			setPortalDwelling(t);
		}
	};

	//Checks if all requirements will be met with expected building list "buildingsThatWillBe"
	auto areRequirementsFulfilled = [&buildingsThatWillBe](const BuildingID & buildID)
	{
		return buildingsThatWillBe.count(buildID);
	};

	//Init the vectors
	for(const auto & build : t->getTown()->buildings)
	{
		if(t->hasBuilt(build.first))
		{
			buildingsThatWillBe.insert(build.first);
		}
		else
		{
			if(build.second->mode == CBuilding::BUILD_AUTO) //not built auto building
				remainingAutoBuildings.push_back(build.second.get());
		}
	}

	//Prepare structure (list of building ids will be filled later)
	NewStructures ns;
	ns.tid = tid;
	ns.built = force ? t->built : (t->built+1);

	std::queue<const CBuilding*> buildingsToAdd;
	buildingsToAdd.push(requestedBuilding.get());

	while(!buildingsToAdd.empty())
	{
		const auto * b = buildingsToAdd.front();
		buildingsToAdd.pop();

		ns.bid.insert(b->bid);
		buildingsThatWillBe.insert(b->bid);
		remainingAutoBuildings -= b;

		for(const auto * autoBuilding : remainingAutoBuildings)
		{
			auto actualRequirements = t->genBuildingRequirements(autoBuilding->bid);

			if(actualRequirements.test(areRequirementsFulfilled))
				buildingsToAdd.push(autoBuilding);
		}
	}

	// FIXME: it's done before NewStructures applied because otherwise town window wont be properly updated on client. That should be actually fixed on client and not on server.
	for(auto builtID : ns.bid)
		processBeforeBuiltStructure(builtID);

	//Take cost
	if(!force)
	{
		const PlayerColor ownerBeforePay = t->tempOwner;
		giveResources(ownerBeforePay, -requestedBuilding->resources);

		// Only record statistics if the player is still a valid, in-game player.
		// If they were eliminated during the resource deduction (rare but possible via custom
		// map triggers), we skip the statistics update because the PlayerState no longer exists.
		if(ownerBeforePay.isValidPlayer() && t->tempOwner == ownerBeforePay)
			statistics->getPlayerAccumulator(ownerBeforePay).spentResourcesForBuildings += requestedBuilding->resources;
	}

	//We know what has been built, apply changes. Do this as final step to properly update town window
	sendAndApply(ns);

	//Other post-built events. To some logic like giving spells to work gamestate changes for new building must be already in place!
	for(auto buildingID : ns.bid)
	{
		bool isMageGuild = buildingID <= BuildingID::MAGES_GUILD_5 && buildingID >= BuildingID::MAGES_GUILD_1;
		bool isLibrary = t->getTown()->buildings.at(buildingID)->subId == BuildingSubID::LIBRARY;
		bool isAurora = t->getTown()->buildings.at(buildingID)->subId == BuildingSubID::AURORA_BOREALIS;

		if(isMageGuild || isLibrary || isAurora)
		{
			if(t->getVisitingHero())
				giveSpells(t, t->getVisitingHero(), true);
			if(t->getGarrisonHero())
				giveSpells(t, t->getGarrisonHero(), false);
		}
	};

	// now when everything is built - reveal tiles for lookout tower
	changeFogOfWar(t->getSightCenter(), t->getSightRadius(), t->getOwner(), ETileVisibility::REVEALED);

	if (!force)
	{
		//garrison hero first - consistent with original H3 Mana Vortex and Battle Scholar Academy levelup windows order
		std::vector<const CGHeroInstance *> visitors;
		if (t->getGarrisonHero())
			visitors.push_back(t->getGarrisonHero());
		if (t->getVisitingHero())
			visitors.push_back(t->getVisitingHero());

		if (!visitors.empty())
			visitCastleObjects(t, visitors);
	}

	checkVictoryLossConditionsForPlayer(t->tempOwner);
	return true;
}

bool CGameHandler::visitTownBuilding(ObjectInstanceID tid, BuildingID bid)
{
	const CGTownInstance * t = gameInfo().getTown(tid);

	if(!t->hasBuilt(bid))
		return false;

	auto subID = t->getTown()->buildings.at(bid)->subId;

	if(subID == BuildingSubID::EBuildingSubID::BANK)
	{
		TResources res;
		res[EGameResID::GOLD] = 2500;
		giveResources(t->getOwner(), res);

		setObjPropertyValue(t->id, ObjProperty::BONUS_VALUE_SECOND, 2500);
		return true;
	}

	if (t->rewardableBuildings.count(bid) && t->getVisitingHero() && t->getTown()->buildings.at(bid)->manualHeroVisit)
	{
		std::vector<BuildingID> buildingsToVisit;
		std::vector<const CGHeroInstance*> visitors;
		buildingsToVisit.push_back(bid);
		visitors.push_back(t->getVisitingHero());
		auto visitQuery = std::make_shared<TownBuildingVisitQuery>(this, t, visitors, buildingsToVisit);
		queries->addQuery(visitQuery);
		return true;
	}

	return true;
}

bool CGameHandler::razeStructure (ObjectInstanceID tid, BuildingID bid)
{
///incomplete, simply erases target building
	const CGTownInstance * t = gameInfo().getTown(tid);
	if(!t->hasBuilt(bid))
		return false;
	RazeStructures rs;
	rs.tid = tid;
	rs.bid.insert(bid);
	rs.destroyed = t->destroyed + 1;
	sendAndApply(rs);
	return true;
}

bool CGameHandler::spellResearch(ObjectInstanceID tid, SpellID spellAtSlot, bool accepted)
{
	const CGTownInstance * t = gameState().getTown(tid);
	if(!t && complain("Town for spell research not found!"))
		return false;
	if(newHorizonsMagic::mageGuildGenerationActive(gameInfo().getMagicRules())
		&& complain("Spell research is unavailable with fixed New Horizons Mage Guilds!"))
		return false;

	if(!gameInfo().getSettings().getBoolean(EGameSettings::TOWNS_SPELL_RESEARCH) && complain("Spell research not allowed!"))
		return false;
	if (!t->spellResearchAllowed && complain("Spell research not allowed in this town!"))
		return false;

	int level = -1;
	int visibleIndex = -1;
	for(int i = 0; i < t->spells.size(); i++)
	{
		const int position = vstd::find_pos(t->spells[i], spellAtSlot);
		if(position >= 0 && position < t->spellsAtLevel(i + 1, false))
		{
			level = i;
			visibleIndex = position;
		}
	}

	if(level == -1 && complain("Spell for replacement not found!"))
		return false;

	auto spells = t->spells.at(level);
	const int candidateIndex = t->spellResearchCandidateIndex(level + 1, visibleIndex);
	if(candidateIndex < 0 && complain("No eligible replacement spell remains for this slot!"))
		return false;

	bool researchLimitExceeded = t->spellResearchCounterDay >= gameInfo().getSettings().getValue(EGameSettings::TOWNS_SPELL_RESEARCH_PER_DAY).Vector()[level].Float();
	if(researchLimitExceeded && complain("Already researched today!"))
		return false;

	ResourceSet costBase;
	costBase.resolveFromJson(gameInfo().getSettings().getValue(EGameSettings::TOWNS_SPELL_RESEARCH_COST).Vector()[level]);
	double pastResearchesCostMultiplier = gameInfo().getSettings().getValue(EGameSettings::TOWNS_SPELL_RESEARCH_COST_MULTIPLIER_PER_RESEARCH).Vector()[level].Float();
	double pastRerollsCostMultiplier = gameInfo().getSettings().getValue(EGameSettings::TOWNS_SPELL_RESEARCH_COST_MULTIPLIER_PER_REROLL).Vector()[level].Float();
	double pastResearchesCurrentMultiplier = std::pow(pastResearchesCostMultiplier, t->spellResearchAcceptedCounter);
	double pastRerollsCurrentMultiplier = std::pow(pastRerollsCostMultiplier, t->spellResearchPendingRerollsCounters[level]);
	ResourceSet cost = costBase.multipliedBy(pastResearchesCurrentMultiplier * pastRerollsCurrentMultiplier);

	if(!gameInfo().getPlayerState(t->getOwner())->resources.canAfford(cost) && complain("Spell replacement cannot be afforded!"))
		return false;

	giveResources(t->getOwner(), -cost);

	if(accepted)
		std::swap(spells.at(candidateIndex), spells.at(visibleIndex));

	auto it = spells.begin() + candidateIndex;
	std::rotate(it, it + 1, spells.end()); // move to end
	setResearchedSpells(t, level, spells, accepted);

	if(accepted)
	{
		if(t->getVisitingHero())
			giveSpells(t, t->getVisitingHero(), true);
		if(t->getGarrisonHero())
			giveSpells(t, t->getGarrisonHero(), false);
	}

	return true;
}

bool CGameHandler::unlockNewHorizonsAdventureSpell(ObjectInstanceID townId, int32_t guildLevel)
{
	const auto * town = gameState().getTown(townId);
	COMPLAIN_RET_FALSE_IF(!town, "Town for Adventure Spell unlock not found!");
	COMPLAIN_RET_FALSE_IF(!town->getOwner().isValidPlayer(), "Neutral towns cannot unlock Adventure Spells!");
	COMPLAIN_RET_FALSE_IF(guildLevel < 1 || guildLevel > 5, "Invalid Adventure Spell Guild tier!");
	COMPLAIN_RET_FALSE_IF(town->mageGuildLevel() < guildLevel, "Build the matching Mage Guild tier first!");
	COMPLAIN_RET_FALSE_IF(!newHorizonsMagic::adventureSpellRulesActive(gameInfo().getMagicRules()),
		"New Horizons Adventure Spells are not enabled in this game!");
	COMPLAIN_RET_FALSE_IF(town->hasNewHorizonsAdventureSpellUnlocked(guildLevel), "That Adventure Spell is already unlocked in this town!");

	const auto & magicRules = gameInfo().getMagicRules();
	const auto spellID = newHorizonsMagic::adventureSpellForGuildLevel(magicRules, guildLevel);
	const auto * spell = spellID.toSpell();
	const auto savedGuildLevel = newHorizonsMagic::adventureSpellGuildLevel(magicRules, spellID);
	COMPLAIN_RET_FALSE_IF(!spell || !spell->isCommonHeroSpell() || !spell->isAdventure()
		|| !savedGuildLevel || *savedGuildLevel != guildLevel || !gameInfo().isAllowed(spellID),
		"The Adventure Spell for this Guild tier is not eligible in this game!");

	ResourceSet unlockCost;
	try
	{
		unlockCost = newHorizonsMagic::adventureSpellUnlockCost(magicRules, spellID);
	}
	catch(const std::exception &)
	{
		logGlobal->error("Saved New Horizons rules have no valid unlock price for Adventure Spell %s", spell->getJsonKey().c_str());
		COMPLAIN_RET_FALSE_IF(true, "This Adventure Spell has no valid town unlock price!");
	}

	const auto * owner = gameInfo().getPlayerState(town->getOwner());
	COMPLAIN_RET_FALSE_IF(!owner || !owner->resources.canAfford(unlockCost), "Your town cannot afford to unlock this Adventure Spell!");

	SetNewHorizonsAdventureSpellUnlock unlock;
	unlock.townId = townId;
	unlock.guildLevel = guildLevel;
	giveResources(town->getOwner(), -unlockCost);
	sendAndApply(unlock);

	if(town->getVisitingHero())
		giveSpells(town, town->getVisitingHero(), true);

	return true;
}

bool CGameHandler::selectPortalDwelling(ObjectInstanceID townId, ObjectInstanceID sourceDwellingId, PlayerColor player)
{
	const auto * town = gameState().getTown(townId);
	const auto * source = dynamic_cast<const CGDwelling *>(gameInfo().getObj(sourceDwellingId));

	COMPLAIN_RET_FALSE_IF(!newHorizonsMagic::rulesActive(gameInfo().getMagicRules()),
		"New Horizons Portal recruitment is not enabled in this game!");
	COMPLAIN_RET_FALSE_IF(!town || town->getOwner() != player
		|| !town->hasBuilt(BuildingSubID::PORTAL_OF_SUMMONING),
		"Cannot select a source without an owned Portal of Summoning!");
	COMPLAIN_RET_FALSE_IF(!source || (source->ID != Obj::CREATURE_GENERATOR1 && source->ID != Obj::CREATURE_GENERATOR4)
		|| source->getOwner() != player,
		"Portal source must be an owned external creature dwelling!");

	const int week = newHorizonsMuster::absoluteWeek(gameInfo().getCalendar().getCurrentDay(),
		gameInfo().getCalendar().getDaysInWeek());
	COMPLAIN_RET_FALSE_IF(town->portalLastSelectionWeek >= week,
		"This Portal has already changed its source this week!");

	SetPortalDwellingSource update;
	update.townId = townId;
	update.sourceDwellingId = sourceDwellingId;
	update.lastSelectionWeek = week;
	sendAndApply(update);
	return true;
}

bool CGameHandler::recruitCreatures(ObjectInstanceID objid, ObjectInstanceID dstid, CreatureID crid, int32_t cram,
	int32_t fromLvl, PlayerColor player, ObjectInstanceID portalTownId)
{
	const auto * dwelling = dynamic_cast<const CGDwelling *>(gameInfo().getObj(objid));
	const auto * town = dynamic_cast<const CGTownInstance *>(gameInfo().getObj(objid));
	const auto * army = dynamic_cast<const CArmedInstance *>(gameInfo().getObj(dstid));
	const auto * hero = dynamic_cast<const CGHeroInstance *>(gameInfo().getObj(dstid));
	const auto * portalTown = portalTownId == ObjectInstanceID::NONE ? nullptr : gameInfo().getTown(portalTownId);
	const auto * c = crid.toCreature();

	COMPLAIN_RET_FALSE_IF(!c, "Cannot recruit: invalid creature!");
	const bool warMachine = c->warMachine != ArtifactID::NONE;

	//TODO: check if hero is actually visiting object

	COMPLAIN_RET_FALSE_IF(!dwelling || !army, "Cannot recruit: invalid object!");
	COMPLAIN_RET_FALSE_IF(dwelling->getOwner() != player && dwelling->getOwner() != PlayerColor::UNFLAGGABLE, "Cannot recruit: dwelling not owned!");

	if (portalTownId != ObjectInstanceID::NONE)
	{
		const bool externalDwelling = dwelling->ID == Obj::CREATURE_GENERATOR1 || dwelling->ID == Obj::CREATURE_GENERATOR4;
		const bool destinationBelongsToPortal = portalTown
			&& (army == portalTown
				|| (hero && (hero == portalTown->getVisitingHero() || hero == portalTown->getGarrisonHero())));
		COMPLAIN_RET_FALSE_IF(!newHorizonsMagic::rulesActive(gameInfo().getMagicRules())
			|| !portalTown || portalTown->getOwner() != player
			|| !portalTown->hasBuilt(BuildingSubID::PORTAL_OF_SUMMONING)
			|| portalTown->portalSourceDwellingId != objid || dwelling->getOwner() != player,
			"Portal source is not selected by an owned Portal of Summoning!");
		COMPLAIN_RET_FALSE_IF(!externalDwelling,
			"Portal recruitment requires an external creature dwelling!");
		COMPLAIN_RET_FALSE_IF(warMachine,
			"Portal recruitment cannot recruit war machines!");
		COMPLAIN_RET_FALSE_IF(!destinationBelongsToPortal || army->getOwner() != player
			|| (hero && hero->getOwner() != player),
			"Portal recruitment destination must be the town or its visiting/garrisoned hero!");
	}
	else if (town)
	{
		COMPLAIN_RET_FALSE_IF(town != army && !hero, "Cannot recruit: invalid destination!");
		COMPLAIN_RET_FALSE_IF(hero != town->getGarrisonHero() && hero != town->getVisitingHero(), "Cannot recruit: can only recruit to town or hero in town!!");
	}
	else
	{
		COMPLAIN_RET_FALSE_IF(getVisitingHero(dwelling) != hero, "Cannot recruit: can only recruit by visiting hero!");
		COMPLAIN_RET_FALSE_IF(!hero || hero->getOwner() != player, "Cannot recruit: can only recruit to owned hero!");
	}

	//verify
	bool found = false;
	int level = 0;

	for (; level < dwelling->creatures.size(); level++) //iterate through all levels
	{
		if ((fromLvl != -1) && (level !=fromLvl))
			continue;
		const auto &cur = dwelling->creatures.at(level); //current level info <amount, list of cr. ids>
		int i = 0;
		for (; i < cur.second.size(); i++) //look for crid among available creatures list on current level
			if (cur.second.at(i) == crid)
				break;

		if (i < cur.second.size())
		{
			found = true;
			cram = std::min<int32_t>(cram, cur.first); //reduce recruited amount up to available amount
			break;
		}
	}
	SlotID slot = army->getSlotFor(crid);
	const auto costPerCreature = dwelling->getRecruitmentCost(crid);
	const auto & resources = gameInfo().getPlayerState(army->tempOwner)->resources;
	int32_t maxAffordableAmount = std::numeric_limits<int32_t>::max();
	for(size_t resource = 0; resource < std::min(resources.size(), costPerCreature.size()); ++resource)
	{
		if(costPerCreature[resource] > 0)
			maxAffordableAmount = std::min(maxAffordableAmount, resources[resource] / costPerCreature[resource]);
	}

	if((!found && complain("Cannot recruit: no such creatures!"))
		|| (cram > maxAffordableAmount && complain("Cannot recruit: lack of resources!"))
		|| (cram <= 0 && complain("Cannot recruit: cram <= 0!"))
		|| (!slot.validSlot() && !warMachine && complain("Cannot recruit: no available slot!")))
	{
		return false;
	}
	if(!warMachine)
	{
		const int recruitedStackSize = army->hasStackAtSlot(slot) ? army->getStackCount(slot) + cram : cram;
		if(!validateLeadershipStack(army, crid, recruitedStackSize))
			return false;
	}

	//recruit
	TResources cost = costPerCreature * cram;
	giveResources(army->tempOwner, -cost);
	statistics->getPlayerAccumulator(army->tempOwner).spentResourcesForArmy += cost;

	SetAvailableCreatures sac;
	sac.tid = objid;
	sac.creatures = dwelling->creatures;
	sac.creatures[level].first -= cram;
	sendAndApply(sac);

	if (warMachine)
	{
		ArtifactID artId = c->warMachine;
		const CArtifact * art = artId.toArtifact();

		COMPLAIN_RET_FALSE_IF(!hero, "Only hero can buy war machines");
		COMPLAIN_RET_FALSE_IF(artId == ArtifactID::CATAPULT, "Catapult cannot be recruited!");
		COMPLAIN_RET_FALSE_IF(nullptr == art, "Invalid war machine artifact");
		COMPLAIN_RET_FALSE_IF(hero->hasArt(artId),"Hero already has this machine!");

		bool hasFreeSlot = false;
		for(auto possibleSlot : art->getPossibleSlots().at(ArtBearer::HERO))
			if (hero->getArt(possibleSlot) == nullptr)
				hasFreeSlot = true;

		if (!hasFreeSlot)
		{
			auto possibleSlot = art->getPossibleSlots().at(ArtBearer::HERO).front();
			removeArtifact(ArtifactLocation(hero->id, possibleSlot));
		}
		return giveHeroNewArtifact(hero, artId, ArtifactPosition::FIRST_AVAILABLE);
	}
	else
	{
		addToSlot(StackLocation(army->id, slot), c, cram);
	}
	return true;
}

bool CGameHandler::musterCreatures(ObjectInstanceID heroId, ObjectInstanceID targetId, CreatureID creatureId,
	PlayerColor player, CreatureID secondCreatureId, int32_t firstAmount)
{
	const auto * hero = gameInfo().getHero(heroId);
	const auto * dwelling = dynamic_cast<const CGDwelling *>(gameInfo().getObj(targetId));
	const auto * town = dynamic_cast<const CGTownInstance *>(dwelling);

	COMPLAIN_RET_FALSE_IF(!hero || !dwelling, "Cannot Muster: invalid hero or dwelling!");
	const bool externalDwelling = dwelling->ID == Obj::CREATURE_GENERATOR1 || dwelling->ID == Obj::CREATURE_GENERATOR4;
	COMPLAIN_RET_FALSE_IF(!town && !externalDwelling, "Cannot Muster: target must be a town or external creature dwelling!");
	COMPLAIN_RET_FALSE_IF(hero->getOwner() != player || dwelling->getOwner() != player,
		"Cannot Muster: hero and target dwelling must belong to the requesting player!");
	if(town)
	{
		COMPLAIN_RET_FALSE_IF(hero != town->getVisitingHero() && hero != town->getGarrisonHero(),
			"Cannot Muster: hero must be visiting or garrisoned in the town!");
	}
	else
	{
		COMPLAIN_RET_FALSE_IF(getVisitingObject(hero) != dwelling,
			"Cannot Muster: hero must be actively visiting the target dwelling!");
	}

	const int rank = hero->getPerkSkillRank(std::string(newHorizonsMuster::RECRUITMENT_SKILL));
	COMPLAIN_RET_FALSE_IF(rank <= 0, "Cannot Muster: hero does not have Recruitment!");
	newHorizonsMuster::PerkModifiers modifiers;
	modifiers.volunteerNetwork = hero->hasActivePerk(std::string(newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(newHorizonsMuster::VOLUNTEER_NETWORK_PERK));
	modifiers.eliteDraft = hero->hasActivePerk(std::string(newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(newHorizonsMuster::ELITE_DRAFT_PERK));
	modifiers.championsCall = hero->hasActivePerk(std::string(newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(newHorizonsMuster::CHAMPIONS_CALL_PERK));
	modifiers.masterRecruiter = hero->hasActivePerk(std::string(newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(newHorizonsMuster::MASTER_RECRUITER_PERK));
	const bool externalRecruiterActive = hero->hasActivePerk(std::string(newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(newHorizonsMuster::EXTERNAL_RECRUITER_PERK));

	const auto category = gameInfo().getCreatureCategory(creatureId);
	COMPLAIN_RET_FALSE_IF(!category, "Cannot Muster: creature has no saved New Horizons category!");
	const auto amount = town
		? newHorizonsMuster::amountForCategory(rank, category->category, modifiers)
		: newHorizonsMuster::amountForExternalCategory(rank, category->category, externalRecruiterActive);
	COMPLAIN_RET_FALSE_IF(!amount, "Cannot Muster: Recruitment rank cannot Muster this creature category!");

	const bool splitRequest = secondCreatureId != CreatureID::NONE;
	COMPLAIN_RET_FALSE_IF(!splitRequest && firstAmount != 0,
		"Cannot Muster: first-row amount is only valid for a split allocation!");
	COMPLAIN_RET_FALSE_IF(splitRequest && (!town
		|| !hero->hasActivePerk(std::string(newHorizonsMuster::RECRUITMENT_SKILL),
			std::string(newHorizonsMuster::BROAD_MUSTER_PERK))),
		"Cannot Muster: splitting requires Broad Muster in a town!");
	COMPLAIN_RET_FALSE_IF(splitRequest && category->category != newHorizonsCreatures::CreatureCategory::CORE,
		"Cannot Muster: Broad Muster can only split Core recruits!");

	const int week = newHorizonsMuster::absoluteWeek(gameInfo().getCalendar().getCurrentDay(),
		gameInfo().getCalendar().getDaysInWeek());
	const int usesThisWeek = hero->getNewHorizonsMusterUsesThisWeek(week);
	COMPLAIN_RET_FALSE_IF(usesThisWeek >= newHorizonsMuster::maximumUsesPerWeek(modifiers),
		"Cannot Muster: this hero has no Muster uses remaining this week!");
	COMPLAIN_RET_FALSE_IF(dwelling->getNewHorizonsMusterLastWeek() == week,
		"Cannot Muster: this dwelling has already received Muster this week!");

	int firstRow = -1;
	for(size_t index = 0; index < dwelling->creatures.size(); ++index)
	{
		const auto & entry = dwelling->creatures[index];
		if(vstd::contains(entry.second, creatureId))
		{
			firstRow = static_cast<int>(index);
			break;
		}
	}
	COMPLAIN_RET_FALSE_IF(firstRow < 0, "Cannot Muster: creature is not available from this dwelling!");

	int secondRow = -1;
	int secondAmount = 0;
	if(splitRequest)
	{
		const auto secondCategory = gameInfo().getCreatureCategory(secondCreatureId);
		COMPLAIN_RET_FALSE_IF(!secondCategory || secondCategory->category != newHorizonsCreatures::CreatureCategory::CORE,
			"Cannot Muster: second split target is not a saved Core creature!");

		for(size_t index = 0; index < dwelling->creatures.size(); ++index)
		{
			if(static_cast<int>(index) != firstRow
				&& vstd::contains(dwelling->creatures[index].second, secondCreatureId))
			{
				secondRow = static_cast<int>(index);
				break;
			}
		}
		COMPLAIN_RET_FALSE_IF(secondRow < 0,
			"Cannot Muster: split targets must be two distinct Core dwelling rows in the same town!");
		COMPLAIN_RET_FALSE_IF(firstAmount <= 0 || firstAmount >= *amount,
			"Cannot Muster: split amounts must be positive and add up to the generated total!");
		secondAmount = *amount - firstAmount;
	}

	// All checks are complete before either authoritative mutation is emitted.
	// SetAvailableCreatures carries the actual stock change, while the marker
	// packet mirrors the once-per-week use on both hero and target dwelling.
	SetAvailableCreatures stock;
	stock.tid = dwelling->id;
	stock.creatures = dwelling->creatures;
	const ui32 firstRecruits = static_cast<ui32>(splitRequest ? firstAmount : *amount);
	const ui32 secondRecruits = static_cast<ui32>(secondAmount);
	if(std::numeric_limits<ui32>::max() - stock.creatures.at(firstRow).first < firstRecruits
		|| (splitRequest && std::numeric_limits<ui32>::max() - stock.creatures.at(secondRow).first < secondRecruits))
	{
		complain("Cannot Muster: dwelling recruitment stock overflow!");
		return false;
	}
	SetNewHorizonsMusterState state;
	state.heroId = hero->id;
	state.targetId = dwelling->id;
	state.lastUseWeek = week;
	state.usesThisWeek = usesThisWeek + 1;
	sendAndApply(state);

	// Replicate the marker before the stock event. Client recruitment windows
	// refresh in response to SetAvailableCreatures and must observe the final
	// used-this-week state when they update the action button.
	stock.creatures.at(firstRow).first += firstRecruits;
	if(splitRequest)
		stock.creatures.at(secondRow).first += secondRecruits;
	sendAndApply(stock);
	return true;
}

bool CGameHandler::arrangeDemonicReserve(ObjectInstanceID heroId, SlotID activeSlot,
	CreatureID creatureId, TQuantity amount, bool toReserve, PlayerColor player)
{
	const auto * hero = gameInfo().getHero(heroId);
	const auto * creature = creatureId.toCreature();
	COMPLAIN_RET_FALSE_IF(!hero || !creature, "Cannot arrange Demonic Reserve: invalid hero or creature!");
	COMPLAIN_RET_FALSE_IF(hero->getOwner() != player,
		"Cannot arrange Demonic Reserve: hero does not belong to the requesting player!");
	COMPLAIN_RET_FALSE_IF(hero->getPerkSkillRank("new-horizons:demonicGating") <= 0,
		"Cannot arrange Demonic Reserve: hero does not have Demonic Gating!");
	COMPLAIN_RET_FALSE_IF(creature->getFactionID() != FactionID::INFERNO,
		"Cannot arrange Demonic Reserve: only Inferno creatures are eligible!");
	COMPLAIN_RET_FALSE_IF(amount <= 0, "Cannot arrange Demonic Reserve: amount must be positive!");

	auto reserve = hero->getDemonicReserve();
	if(toReserve)
	{
		COMPLAIN_RET_FALSE_IF(!hero->hasStackAtSlot(activeSlot),
			"Cannot arrange Demonic Reserve: active source slot is empty!");
		COMPLAIN_RET_FALSE_IF(hero->getCreature(activeSlot)->getId() != creatureId,
			"Cannot arrange Demonic Reserve: source creature does not match the request!");
		const TQuantity sourceCount = hero->getStackCount(activeSlot);
		COMPLAIN_RET_FALSE_IF(amount > sourceCount,
			"Cannot arrange Demonic Reserve: requested more creatures than the source stack contains!");
		COMPLAIN_RET_FALSE_IF(amount == sourceCount && hero->stacksCount() == 1 && hero->needsLastStack(),
			"Cannot arrange Demonic Reserve: a hero must retain one active stack!");
		COMPLAIN_RET_FALSE_IF(reserve[creatureId] > std::numeric_limits<TQuantity>::max() - amount,
			"Cannot arrange Demonic Reserve: creature count overflow!");

		reserve[creatureId] += amount;
		if(!changeStackCount(StackLocation(heroId, activeSlot), -amount, ChangeValueMode::RELATIVE))
			return false;
	}
	else
	{
		const auto found = reserve.find(creatureId);
		COMPLAIN_RET_FALSE_IF(found == reserve.end() || found->second < amount,
			"Cannot arrange Demonic Reserve: reserve does not contain the requested creatures!");
		const SlotID destination = hero->getSlotFor(creatureId);
		COMPLAIN_RET_FALSE_IF(destination == SlotID(),
			"Cannot arrange Demonic Reserve: active army has no compatible slot!");
		const TQuantity destinationCount = hero->hasStackAtSlot(destination)
			? hero->getStackCount(destination)
			: 0;
		COMPLAIN_RET_FALSE_IF(destinationCount > std::numeric_limits<TQuantity>::max() - amount,
			"Cannot arrange Demonic Reserve: active stack count overflow!");
		if(!validateLeadershipStack(hero, creatureId, destinationCount + amount))
			return false;

		found->second -= amount;
		if(found->second == 0)
			reserve.erase(found);
		if(!addToSlot(StackLocation(heroId, destination), creature, amount))
			return false;
	}

	SetNewHorizonsDemonicReserve update;
	update.heroId = heroId;
	update.reserve = std::move(reserve);
	sendAndApply(update);
	return true;
}

bool CGameHandler::upgradeCreature(ObjectInstanceID objid, SlotID pos, CreatureID upgID)
{
	const auto * obj = dynamic_cast<const CArmedInstance *>(gameInfo().getObjInstance(objid));
	if (!obj->hasStackAtSlot(pos))
	{
		COMPLAIN_RET("Cannot upgrade, no stack at slot " + std::to_string(pos));
	}
	UpgradeInfo upgradeInfo(obj->getStackPtr(pos)->getId());
	gameState().fillUpgradeInfo(obj, pos, upgradeInfo);
	PlayerColor player = obj->tempOwner;
	const PlayerState *p = gameInfo().getPlayerState(player);
	int crQuantity = obj->stacks.at(pos)->getCount();

	//check if upgrade is possible
	if (!upgradeInfo.hasUpgrades() && complain("That upgrade is not possible!"))
	{
		return false;
	}
	if(!validateLeadershipStack(obj, upgID, crQuantity))
		return false;
	TResources totalCost = upgradeInfo.getUpgradeCostsFor(upgID) * crQuantity;

	//check if player has enough resources
	if (!p->resources.canAfford(totalCost))
		COMPLAIN_RET("Cannot upgrade, not enough resources!");

	//take resources
	giveResources(player, -totalCost);
	statistics->getPlayerAccumulator(player).spentResourcesForArmy += totalCost;

	//upgrade creature
	changeStackType(StackLocation(obj->id, pos), upgID.toCreature());
	return true;
}

bool CGameHandler::changeStackType(const StackLocation &sl, const CCreature *c)
{
	const auto * obj = dynamic_cast<const CArmedInstance *>(gameInfo().getObjInstance(sl.army));

	if (!obj->hasStackAtSlot(sl.slot))
		COMPLAIN_RET("Cannot find a stack to change type");
	if(!validateLeadershipStack(obj, c->getId(), obj->getStackCount(sl.slot)))
		return false;

	SetStackType sst;
	sst.army = obj->id;
	sst.slot = sl.slot;
	sst.type = c->getId();
	sendAndApply(sst);
	return true;
}

bool CGameHandler::moveArmy(const CArmedInstance *src, const CArmedInstance *dst, bool allowMerging)
{
	assert(src->canBeMergedWith(*dst, allowMerging));
	struct ProjectedSlot
	{
		CreatureID creature;
		TQuantity count = 0;
		bool occupied = false;
	};
	std::array<ProjectedSlot, GameConstants::ARMY_SIZE> projected;
	struct PlannedMove
	{
		bool withinDestination = false;
		SlotID source;
		SlotID destination;
	};
	std::vector<PlannedMove> plan;
	for(const auto & [slot, stack] : dst->Slots())
		projected[slot.getNum()] = {stack->getCreatureID(), stack->getCount(), true};
	for(const auto & [sourceSlot, sourceStack] : src->Slots())
	{
		int target = -1;
		for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
			if(projected[i].occupied && projected[i].creature == sourceStack->getCreatureID())
			{
				target = i;
				break;
			}
		if(target < 0)
			for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
				if(!projected[i].occupied)
				{
					target = i;
					break;
				}
		if(target < 0 && allowMerging)
		{
			int mergeSource = -1;
			int mergeDestination = -1;
			const int preferred = sourceSlot.getNum();
			if(sourceSlot.validSlot() && projected[preferred].occupied)
				for(int j = 0; j < GameConstants::ARMY_SIZE; ++j)
					if(j != preferred && projected[j].occupied
						&& projected[j].creature == projected[preferred].creature)
					{
						mergeSource = preferred;
						mergeDestination = j;
						break;
					}
			for(int i = 0; i < GameConstants::ARMY_SIZE && mergeSource < 0; ++i)
				for(int j = 0; j < GameConstants::ARMY_SIZE; ++j)
					if(i != j && projected[i].occupied && projected[j].occupied
						&& projected[i].creature == projected[j].creature)
					{
						mergeSource = i;
						mergeDestination = j;
						break;
					}
			if(mergeSource >= 0)
			{
				projected[mergeDestination].count += projected[mergeSource].count;
				if(!validateLeadershipStack(dst, projected[mergeDestination].creature,
					projected[mergeDestination].count))
					return false;
				projected[mergeSource] = {};
				plan.push_back({true, SlotID(mergeSource), SlotID(mergeDestination)});
				target = mergeSource;
			}
		}
		if(target < 0)
			return false;
		if(!projected[target].occupied)
			projected[target] = {sourceStack->getCreatureID(), 0, true};
		projected[target].count += sourceStack->getCount();
		if(!validateLeadershipStack(dst, projected[target].creature, projected[target].count))
			return false;
		plan.push_back({false, sourceSlot, SlotID(target)});
	}
	for(const auto & move : plan)
	{
		const auto * sourceArmy = move.withinDestination ? dst : src;
		if(!moveStack(StackLocation(sourceArmy->id, move.source), StackLocation(dst->id, move.destination)))
			return false;
	}
	return true;
}

bool CGameHandler::garrisonSwap(ObjectInstanceID tid)
{
	const CGTownInstance * town = gameInfo().getTown(tid);
	if (!town->getGarrisonHero() && town->getVisitingHero()) //visiting => garrison, merge armies: town army => hero army
	{

		if (!town->getVisitingHero()->canBeMergedWith(*town))
		{
			complain("Cannot make garrison swap, not enough free slots!");
			return false;
		}

		if(!moveArmy(town, town->getVisitingHero(), true))
			return false;

		SetHeroesInTown intown;
		intown.tid = tid;
		intown.visiting = ObjectInstanceID();
		intown.garrison = town->getVisitingHero()->id;
		sendAndApply(intown);
		return true;
	}
	else if (town->getGarrisonHero() && !town->getVisitingHero()) //move hero out of the garrison
	{
		int mapCap = gameInfo().getSettings().getInteger(EGameSettings::HEROES_PER_PLAYER_ON_MAP_CAP);
		//check if moving hero out of town will break wandering heroes limit
		if (gameInfo().getHeroCount(town->getGarrisonHero()->tempOwner,false) >= mapCap)
		{
			complain("Cannot move hero out of the garrison, there are already " + std::to_string(mapCap) + " wandering heroes!");
			return false;
		}

		SetHeroesInTown intown;
		intown.tid = tid;
		intown.garrison = ObjectInstanceID();
		intown.visiting =  town->getGarrisonHero()->id;
		sendAndApply(intown);
		return true;
	}
	else if (!!town->getGarrisonHero() && town->getVisitingHero()) //swap visiting and garrison hero
	{
		SetHeroesInTown intown;
		intown.tid = tid;
		intown.garrison = town->getVisitingHero()->id;
		intown.visiting =  town->getGarrisonHero()->id;
		sendAndApply(intown);
		return true;
	}
	else
	{
		complain("Cannot swap garrison hero!");
		return false;
	}
}

// With the amount of changes done to the function, it's more like transferArtifacts.
// Function moves artifact from src to dst. If dst is not a backpack and is already occupied, old dst art goes to backpack and is replaced.
bool CGameHandler::moveArtifact(const PlayerColor & player, const ArtifactLocation & src, const ArtifactLocation & dst)
{
	const auto * srcArtSet = gameState().getArtSet(src);
	const auto * dstArtSet = gameState().getArtSet(dst);
	assert(srcArtSet);
	assert(dstArtSet);

	// Make sure exchange is even possible between the two heroes.
	if(!isAllowedExchange(src.artHolder, dst.artHolder))
		COMPLAIN_RET("That heroes cannot make any exchange!");

	COMPLAIN_RET_FALSE_IF(!ArtifactUtils::checkIfSlotValid(*srcArtSet, src.slot), "moveArtifact: wrong artifact source slot");
	const auto * srcArtifact = srcArtSet->getArt(src.slot);
	auto dstSlot = dst.slot;
	if(dstSlot == ArtifactPosition::FIRST_AVAILABLE)
		dstSlot = ArtifactUtils::getArtAnyPosition(dstArtSet, srcArtifact->getTypeId());
	if(!ArtifactUtils::checkIfSlotValid(*dstArtSet, dstSlot))
		return true;
	const auto * dstArtifact = dstArtSet->getArt(dstSlot);
	const bool isDstSlotOccupied = dstArtSet->bearerType() == ArtBearer::ALTAR ? false : dstArtifact != nullptr;
	const bool isDstSlotBackpack = dstArtSet->bearerType() == ArtBearer::HERO ? ArtifactUtils::isSlotBackpack(dstSlot) : false;

	if(srcArtifact == nullptr)
		COMPLAIN_RET("No artifact to move!");
	if(isDstSlotOccupied && gameState().getOwner(src.artHolder) != gameState().getOwner(dst.artHolder) && !isDstSlotBackpack)
		COMPLAIN_RET("Can't touch artifact on hero of another player!");

	// Check if src/dest slots are appropriate for the artifacts exchanged.
	// Moving to the backpack is always allowed.
	if((!srcArtifact || !isDstSlotBackpack) && !srcArtifact->canBePutAt(dstArtSet, dstSlot, true))
		COMPLAIN_RET("Cannot move artifact!");

	const auto * srcSlotInfo = srcArtSet->getSlot(src.slot);
	const auto * dstSlotInfo = dstArtSet->getSlot(dstSlot);

	if((srcSlotInfo && srcSlotInfo->locked) || (dstSlotInfo && dstSlotInfo->locked))
		COMPLAIN_RET("Cannot move artifact locks.");

	if(isDstSlotBackpack && srcArtifact->getType()->isBig())
		COMPLAIN_RET("Cannot put big artifacts in backpack!");
	if(src.slot == ArtifactPosition::MACH4 || dstSlot == ArtifactPosition::MACH4)
		COMPLAIN_RET("Cannot move catapult!");
	if(isDstSlotBackpack && !ArtifactUtils::isBackpackFreeSlots(dstArtSet))
		COMPLAIN_RET("Backpack is full!");

	dstSlot = std::min(dstSlot, ArtifactPosition(ArtifactPosition::BACKPACK_START + dstArtSet->artifactsInBackpack.size()));

	if(src.slot == dstSlot && src.artHolder == dst.artHolder)
		COMPLAIN_RET("Won't move artifact: Dest same as source!");

	BulkMoveArtifacts ma(player, src.artHolder, dst.artHolder, false);
	ma.srcCreature = src.creature;
	ma.dstCreature = dst.creature;

	// Check if dst slot is occupied
	if(!isDstSlotBackpack && isDstSlotOccupied)
	{
		// Previous artifact must be swapped
		COMPLAIN_RET_FALSE_IF(!dstArtifact->canBePutAt(srcArtSet, src.slot, true), "Cannot swap artifacts!");
		ma.artsPack1.emplace_back(dstSlot, src.slot);
	}

	const auto * hero = gameInfo().getHero(dst.artHolder);
	if(ArtifactUtils::checkSpellbookIsNeeded(hero, srcArtifact->getTypeId(), dstSlot))
		giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);

	ma.artsPack0.emplace_back(src.slot, dstSlot);
	if(src.artHolder != dst.artHolder && !isDstSlotBackpack)
		ma.artsPack0.back().askAssemble = true;
	sendAndApply(ma);
	return true;
}

bool CGameHandler::bulkMoveArtifacts(const PlayerColor & player, ObjectInstanceID srcId, ObjectInstanceID dstId, bool swap, bool equipped, bool backpack)
{
	// Make sure exchange is even possible between the two heroes.
	if(!isAllowedExchange(srcId, dstId))
		COMPLAIN_RET("That heroes cannot make any exchange!");

	const auto * psrcSet = gameState().getArtSet(srcId);
	const auto * pdstSet = gameState().getArtSet(dstId);
	if((!psrcSet) || (!pdstSet))
		COMPLAIN_RET("bulkMoveArtifacts: wrong hero's ID");

	BulkMoveArtifacts ma(player, srcId, dstId, swap);
	auto & slotsSrcDst = ma.artsPack0;
	auto & slotsDstSrc = ma.artsPack1;

	// Temporary fitting set for artifacts. Used to select available slots before sending data.
	CArtifactFittingSet artFittingSet(&gameInfo(), pdstSet->bearerType());

	auto moveArtifact = [this, &artFittingSet, dstId](const CArtifactInstance * artifact,
		ArtifactPosition srcSlot, std::vector<MoveArtifactInfo> & slots) -> void
	{
		assert(artifact);
		auto dstSlot = ArtifactUtils::getArtAnyPosition(&artFittingSet, artifact->getTypeId());
		if(dstSlot != ArtifactPosition::PRE_FIRST)
		{
			artFittingSet.putArtifact(dstSlot, artifact);
			slots.emplace_back(srcSlot, dstSlot);

			// TODO Shouldn't be here. Possibly in callback after equipping the artifact
			if(const auto * dstHero = gameInfo().getHero(dstId))
			{
				if(ArtifactUtils::checkSpellbookIsNeeded(dstHero, artifact->getTypeId(), dstSlot))
					giveHeroNewArtifact(dstHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			}
		}
	};

	if(swap)
	{
		auto moveArtsWorn = [moveArtifact](const CArtifactSet * srcArtSet, std::vector<MoveArtifactInfo> & slots)
		{
			for(const auto & artifact : srcArtSet->artifactsWorn)
			{
				if(ArtifactUtils::isArtRemovable(artifact))
					moveArtifact(artifact.second.getArt(), artifact.first, slots);
			}
		};
		auto moveArtsInBackpack = [](const CArtifactSet * artSet,
			std::vector<MoveArtifactInfo> & slots) -> void
		{
			for(const auto & slotInfo : artSet->artifactsInBackpack)
			{
				auto slot = artSet->getArtPos(slotInfo.getArt());
				slots.emplace_back(slot, slot);
			}
		};
		if(equipped)
		{
			// Move over artifacts that are worn srcHero -> dstHero
			moveArtsWorn(psrcSet, slotsSrcDst);
			artFittingSet.artifactsWorn.clear();
			// Move over artifacts that are worn dstHero -> srcHero
			moveArtsWorn(pdstSet, slotsDstSrc);
		}
		if(backpack)
		{
			// Move over artifacts that are in backpack srcHero -> dstHero
			moveArtsInBackpack(psrcSet, slotsSrcDst);
			// Move over artifacts that are in backpack dstHero -> srcHero
			moveArtsInBackpack(pdstSet, slotsDstSrc);
		}
	}
	else
	{
		artFittingSet.artifactsInBackpack = pdstSet->artifactsInBackpack;
		artFittingSet.artifactsWorn = pdstSet->artifactsWorn;
		if(equipped)
		{
			// Move over artifacts that are worn
			for(const auto & artInfo : psrcSet->artifactsWorn)
			{
				if(ArtifactUtils::isArtRemovable(artInfo))
				{
					moveArtifact(psrcSet->getArt(artInfo.first), artInfo.first, slotsSrcDst);
				}
			}
		}
		if(backpack)
		{
			// Move over artifacts that are in backpack
			for(const auto & slotInfo : psrcSet->artifactsInBackpack)
			{
				moveArtifact(psrcSet->getArt(psrcSet->getArtPos(slotInfo.getArt())),
					psrcSet->getArtPos(slotInfo.getArt()), slotsSrcDst);
			}
		}
	}
	sendAndApply(ma);
	return true;
}

bool CGameHandler::manageBackpackArtifacts(const PlayerColor & player, const ObjectInstanceID & heroID, const ManageBackpackArtifacts::ManageCmd & sortType)
{
	const auto * artSet = gameState().getArtSet(heroID);
	COMPLAIN_RET_FALSE_IF(artSet == nullptr, "manageBackpackArtifacts: wrong hero's ID");

	BulkMoveArtifacts bma(player, heroID, heroID, false);

	const auto sortPack = [artSet](std::vector<MoveArtifactInfo> & pack)
	{
		// Each pack of artifacts is also sorted by ArtifactID. Scrolls by SpellID
		std::sort(pack.begin(), pack.end(), [artSet](const auto & slots0, const auto & slots1) -> bool
		{
			const auto art0 = artSet->getArt(slots0.srcPos);
			const auto art1 = artSet->getArt(slots1.srcPos);
			if(art0->isScroll() && art1->isScroll())
				return art0->getScrollSpellID() > art1->getScrollSpellID();
			return art0->getTypeId().num > art1->getTypeId().num;
		});
	};

	const auto buildAscendingOrder = [artSet, &sortPack](auto && getSortId)
	{
		std::map<int32_t, std::vector<MoveArtifactInfo>> packsSorted;
		ArtifactPosition backpackSlot = ArtifactPosition::BACKPACK_START;

		for(const auto & backpackSlotInfo : artSet->artifactsInBackpack)
			packsSorted.try_emplace(getSortId(backpackSlotInfo)).first->second.emplace_back(backpackSlot++, ArtifactPosition::PRE_FIRST);

		std::vector<MoveArtifactInfo> orderAsc;
		for(auto & entry : packsSorted)
		{
		    auto & pack = entry.second;
		    sortPack(pack);
		    orderAsc.insert(orderAsc.end(), pack.begin(), pack.end());
		}

		return orderAsc;
	};

	const auto isAlreadyAscending = [artSet](const std::vector<MoveArtifactInfo> & orderAsc)
	{
		std::vector<ArtifactInstanceID> curIds;
		curIds.reserve(artSet->artifactsInBackpack.size());
		for(const auto & slotInfo : artSet->artifactsInBackpack)
			curIds.push_back(slotInfo.getArt()->getId());

		std::vector<ArtifactInstanceID> ascIds;
		ascIds.reserve(orderAsc.size());
		for(const auto & mi : orderAsc)
			ascIds.push_back(artSet->getArt(mi.srcPos)->getId());

		return curIds == ascIds;
	};

	const auto buildRequestFromOrder = [&bma](const std::vector<MoveArtifactInfo> & order, bool reverseAll)
	{
		if(!reverseAll)
			bma.artsPack0.insert(bma.artsPack0.end(), order.begin(), order.end());
		else
			bma.artsPack0.insert(bma.artsPack0.end(), order.rbegin(), order.rend());

		ArtifactPosition backpackSlot = ArtifactPosition::BACKPACK_START;
		for(auto & slots : bma.artsPack0)
			slots.dstPos = backpackSlot++;
	};

	const auto makeSortBackpackRequest = [&](auto && getSortId)
	{
		auto orderAsc = buildAscendingOrder(getSortId);
		const bool reverseAll = isAlreadyAscending(orderAsc);
		buildRequestFromOrder(orderAsc, reverseAll);
	};

	if(sortType == ManageBackpackArtifacts::ManageCmd::SORT_BY_SLOT)
	{
		makeSortBackpackRequest([](const ArtSlotInfo & inf) -> int32_t
			{
				auto possibleSlots = inf.getArt()->getType()->getPossibleSlots();
				if (possibleSlots.find(ArtBearer::CREATURE) != possibleSlots.end() && !possibleSlots.at(ArtBearer::CREATURE).empty())
				{
					return -2;
				}
				else if (possibleSlots.find(ArtBearer::COMMANDER) != possibleSlots.end() && !possibleSlots.at(ArtBearer::COMMANDER).empty())
				{
					return -1;
				}
				else if (possibleSlots.find(ArtBearer::HERO) != possibleSlots.end() && !possibleSlots.at(ArtBearer::HERO).empty())
				{
					return inf.getArt()->getType()->getPossibleSlots().at(ArtBearer::HERO).front().num;
				}
				else
				{
					// for grail
					return -3;
				}
			});
	}
	else if(sortType == ManageBackpackArtifacts::ManageCmd::SORT_BY_COST)
	{
		makeSortBackpackRequest([](const ArtSlotInfo & inf) -> int32_t
			{
				return inf.getArt()->getType()->getPrice();
			});
	}
	else if(sortType == ManageBackpackArtifacts::ManageCmd::SORT_BY_CLASS)
	{
		makeSortBackpackRequest([](const ArtSlotInfo & inf) -> int32_t
			{
				return static_cast<int32_t>(inf.getArt()->getType()->aClass);
			});
	}
	else
	{
		const auto backpackEnd = ArtifactPosition(ArtifactPosition::BACKPACK_START + artSet->artifactsInBackpack.size() - 1);
		if(backpackEnd > ArtifactPosition::BACKPACK_START)
		{
			if(sortType == ManageBackpackArtifacts::ManageCmd::SCROLL_LEFT)
				bma.artsPack0.emplace_back(backpackEnd, ArtifactPosition::BACKPACK_START);
			else
				bma.artsPack0.emplace_back(ArtifactPosition::BACKPACK_START, backpackEnd);
		}
	}
	sendAndApply(bma);
	return true;
}

bool CGameHandler::saveArtifactsCostume(const PlayerColor & player, const ObjectInstanceID & heroID, uint32_t costumeIdx)
{
	const auto * artSet = gameState().getArtSet(heroID);
	COMPLAIN_RET_FALSE_IF(artSet == nullptr, "saveArtifactsCostume: wrong hero's ID");

	ChangeArtifactsCostume costume(player, costumeIdx);
	for(const auto & slot : ArtifactUtils::commonWornSlots())
	{
		if(const auto slotInfo = artSet->getSlot(slot); slotInfo != nullptr && !slotInfo->locked)
			costume.costumeSet.emplace(slot, slotInfo->getArt()->getTypeId());
	}

	sendAndApply(costume);
	return true;
}

bool CGameHandler::switchArtifactsCostume(const PlayerColor & player, const ObjectInstanceID & heroID, uint32_t costumeIdx)
{
	const auto * artSet = gameState().getArtSet(heroID);
	COMPLAIN_RET_FALSE_IF(artSet == nullptr, "switchArtifactsCostume: wrong hero's ID");
	const auto * playerState = gameInfo().getPlayerState(player);
	COMPLAIN_RET_FALSE_IF(playerState == nullptr, "switchArtifactsCostume: wrong player");

	if(auto costume = playerState->costumesArtifacts.find(costumeIdx); costume != playerState->costumesArtifacts.end())
	{
		CArtifactFittingSet artFittingSet(*artSet);
		BulkMoveArtifacts bma(player, heroID, heroID, false);
		auto costumeArtMap = costume->second;
		auto estimateBackpackSize = artSet->artifactsInBackpack.size();

		// First, find those artifacts that are already in place
		for(const auto & slot : ArtifactUtils::commonWornSlots())
		{
			if(const auto * slotInfo = artFittingSet.getSlot(slot); slotInfo != nullptr && !slotInfo->locked)
				if(const auto artPos = costumeArtMap.find(slot); artPos != costumeArtMap.end() && artPos->second == slotInfo->getArt()->getTypeId())
				{
					costumeArtMap.erase(artPos);
					artFittingSet.removeArtifact(slot);
				}
		}

		// Second, find the necessary artifacts for the costume
		for(const auto & artPos : costumeArtMap)
		{
			if(const auto slot = artFittingSet.getArtPos(artPos.second, false, false); slot != ArtifactPosition::PRE_FIRST)
			{
				bma.artsPack0.emplace_back(artSet->getArtPos(artFittingSet.getArt(slot)), artPos.first);
				artFittingSet.removeArtifact(slot);
				if(ArtifactUtils::isSlotBackpack(slot))
					estimateBackpackSize--;
			}
		}

		// Third, put unnecessary artifacts into backpack
		for(const auto & slot : ArtifactUtils::commonWornSlots())
			if(artFittingSet.getArt(slot))
			{
				bma.artsPack0.emplace_back(slot, ArtifactPosition::BACKPACK_START);
				estimateBackpackSize++;
			}

		const auto backpackCap = gameInfo().getSettings().getInteger(EGameSettings::HEROES_BACKPACK_CAP);
		if((backpackCap < 0 || estimateBackpackSize <= backpackCap) && !bma.artsPack0.empty())
			sendAndApply(bma);
	}
	return true;
}

/**
 * Assembles or disassembles a combination artifact.
 * @param heroID ID of hero holding the artifact(s).
 * @param artifactSlot The worn slot ID of the combination- or constituent artifact.
 * @param assemble True for assembly operation, false for disassembly.
 * @param assembleTo If assemble is true, this represents the artifact ID of the combination
 * artifact to assemble to. Otherwise it's not used.
 */
bool CGameHandler::assembleArtifacts(ObjectInstanceID heroID, ArtifactPosition artifactSlot, bool assemble, ArtifactID assembleTo)
{
	const CGHeroInstance * hero = gameInfo().getHero(heroID);
	const CArtifactInstance * destArtifact = hero->getArt(artifactSlot);

	if(!destArtifact)
		COMPLAIN_RET("assembleArtifacts: there is no such artifact instance!");

	const auto dstLoc = ArtifactLocation(hero->id, artifactSlot);
	if(assemble)
	{
		const CArtifact * combinedArt = assembleTo.toArtifact();
		if(!combinedArt->isCombined())
			COMPLAIN_RET("assembleArtifacts: Artifact being attempted to assemble is not a combined artifacts!");
		if(!vstd::contains(ArtifactUtils::assemblyPossibilities(hero, destArtifact->getTypeId()), combinedArt))
		{
			COMPLAIN_RET("assembleArtifacts: It's impossible to assemble requested artifact!");
		}
		if(!destArtifact->canBePutAt(hero, artifactSlot, true)
			&& !destArtifact->canBePutAt(hero, ArtifactPosition::BACKPACK_START, true))
		{
			COMPLAIN_RET("assembleArtifacts: It's impossible to give the artholder requested artifact!");
		}

		if(ArtifactUtils::checkSpellbookIsNeeded(hero, assembleTo, artifactSlot))
			giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);

		AssembledArtifact aa;
		aa.al = dstLoc;
		aa.artId = assembleTo;
		sendAndApply(aa);
	}
	else
	{
		if(!destArtifact->isCombined())
			COMPLAIN_RET("assembleArtifacts: Artifact being attempted to disassemble is not a combined artifact!");

		if(!destArtifact->hasParts())
			COMPLAIN_RET("assembleArtifacts: Artifact being attempted to disassemble is fused combined artifact!");

		if(ArtifactUtils::isSlotBackpack(artifactSlot)
			&& !ArtifactUtils::isBackpackFreeSlots(hero, destArtifact->getType()->getConstituents().size() - 1))
			COMPLAIN_RET("assembleArtifacts: Artifact being attempted to disassemble but backpack is full!");

		DisassembledArtifact da;
		da.al = dstLoc;
		sendAndApply(da);
	}

	return true;
}

bool CGameHandler::eraseArtifactByClient(const ArtifactLocation & al)
{
	const auto * hero = gameInfo().getHero(al.artHolder);
	if(hero == nullptr)
		COMPLAIN_RET("eraseArtifactByClient: wrong hero's ID");

	const auto * art = hero->getArt(al.slot);
	if(art == nullptr)
		COMPLAIN_RET("Cannot remove artifact!");

	if(art->canBePutAt(hero) || al.slot != ArtifactPosition::TRANSITION_POS)
		COMPLAIN_RET("Illegal artifact removal request");

	removeArtifact(al);
	return true;
}

bool CGameHandler::buyArtifact(ObjectInstanceID hid, ArtifactID aid)
{
	const CGHeroInstance * hero = gameInfo().getHero(hid);
	COMPLAIN_RET_FALSE_IF(nullptr == hero, "Invalid hero index");
	const CGTownInstance * town = hero->getVisitedTown();
	COMPLAIN_RET_FALSE_IF(nullptr == town, "Hero not in town");

	if (aid==ArtifactID::SPELLBOOK)
	{
		if ((!town->hasBuilt(BuildingID::MAGES_GUILD_1) && complain("Cannot buy a spellbook, no mage guild in the town!"))
			|| (gameInfo().getResource(hero->getOwner(), EGameResID::GOLD) < GameConstants::SPELLBOOK_GOLD_COST && complain("Cannot buy a spellbook, not enough gold!"))
		    || (hero->getArt(ArtifactPosition::SPELLBOOK) && complain("Cannot buy a spellbook, hero already has a one!"))
		   )
			return false;

		giveResource(hero->getOwner(),EGameResID::GOLD,-GameConstants::SPELLBOOK_GOLD_COST);
		giveHeroNewArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		assert(hero->getArt(ArtifactPosition::SPELLBOOK));
		giveSpells(town, hero, hero == town->getVisitingHero());
		return true;
	}
	else
	{
		const CArtifact * art = aid.toArtifact();
		COMPLAIN_RET_FALSE_IF(nullptr == art, "Invalid artifact index to buy");
		COMPLAIN_RET_FALSE_IF(art->getWarMachine() == CreatureID::NONE, "War machine artifact required");
		COMPLAIN_RET_FALSE_IF(aid == ArtifactID::CATAPULT, "Catapult cannot be purchased as an ordinary war machine!");
		COMPLAIN_RET_FALSE_IF(hero->hasArt(aid),"Hero already has this machine!");
		const auto offers = town->getWarMachineShopOffers();
		const auto offer = std::find_if(offers.begin(), offers.end(), [aid](const auto & entry)
			{ return entry.artifact == aid; });
		COMPLAIN_RET_FALSE_IF(offer == offers.end(), "This machine is unavailable here!");
		const int price = offer->price;
		COMPLAIN_RET_FALSE_IF(gameInfo().getPlayerState(hero->getOwner())->resources[EGameResID::GOLD] < price, "Not enough gold!");

		bool hasFreeSlot = false;
		for(auto slot : art->getPossibleSlots().at(ArtBearer::HERO))
			if (hero->getArt(slot) == nullptr)
				hasFreeSlot = true;

		if (!hasFreeSlot)
		{
			auto slot = art->getPossibleSlots().at(ArtBearer::HERO).front();
			removeArtifact(ArtifactLocation(hero->id, slot));
		}

		giveResource(hero->getOwner(),EGameResID::GOLD,-price);
		return giveHeroNewArtifact(hero, aid, ArtifactPosition::FIRST_AVAILABLE);
	}
}

bool CGameHandler::buyArtifact(const IMarket *m, const CGHeroInstance *h, GameResID rid, ArtifactID aid)
{
	if(!h)
		COMPLAIN_RET("Only hero can buy artifacts!");

	if (!vstd::contains(m->availableItemsIds(EMarketMode::RESOURCE_ARTIFACT), aid))
		COMPLAIN_RET("That artifact is unavailable!");

	int b1;
	int b2;
	m->getOffer(rid, aid, b1, b2, EMarketMode::RESOURCE_ARTIFACT);

	if (gameInfo().getResource(h->tempOwner, rid) < b1)
		COMPLAIN_RET("You can't afford to buy this artifact!");

	giveResource(h->tempOwner, rid, -b1);

	SetAvailableArtifacts saa;
	if(dynamic_cast<const CGTownInstance *>(m))
	{
		saa.id = ObjectInstanceID::NONE;
		saa.arts = gameState().getMap().townMerchantArtifacts;
	}
	else if(const auto *bm = dynamic_cast<const CGBlackMarket *>(m)) //black market
	{
		saa.id = bm->id;
		saa.arts = bm->artifacts;
	}
	else
		COMPLAIN_RET("Wrong marktet...");

	bool found = false;
	for (ArtifactID & art : saa.arts)
	{
		if (art == aid)
		{
			art = ArtifactID();
			found = true;
			break;
		}
	}

	if (!found)
		COMPLAIN_RET("Cannot find selected artifact on the list");

	sendAndApply(saa);
	giveHeroNewArtifact(h, aid, ArtifactPosition::FIRST_AVAILABLE);
	return true;
}

bool CGameHandler::sellArtifact(const IMarket *m, const CGHeroInstance *h, ArtifactInstanceID aid, GameResID rid)
{
	COMPLAIN_RET_FALSE_IF((!h), "Only hero can sell artifacts!");
	const CArtifactInstance *art = h->getArtByInstanceId(aid);
	COMPLAIN_RET_FALSE_IF((!art), "There is no artifact to sell!");
	COMPLAIN_RET_FALSE_IF((!art->getType()->isTradable()), "Cannot sell a war machine or spellbook!");

	int resVal = 0;
	int dump = 1;
	m->getOffer(art->getType()->getId(), rid, dump, resVal, EMarketMode::ARTIFACT_RESOURCE);

	removeArtifact(ArtifactLocation(h->id, h->getArtPos(art)));
	giveResource(h->tempOwner, rid, resVal);
	return true;
}

bool CGameHandler::buySecSkill(const IMarket *m, const CGHeroInstance *h, SecondarySkill skill)
{
	if (!h)
		COMPLAIN_RET("You need hero to buy a skill!");

	if (h->getSecSkillLevel(SecondarySkill(skill)))
		COMPLAIN_RET("Hero already know this skill");

	if (!h->canLearnSkill())
		COMPLAIN_RET("Hero can't learn any more skills");

	if (!h->canLearnSkill(skill))
		COMPLAIN_RET("The hero can't learn this skill!");

	if (!vstd::contains(m->availableItemsIds(EMarketMode::RESOURCE_SKILL), skill))
		COMPLAIN_RET("That skill is unavailable!");

	const auto tuition = newHorizonsUniversity::tuition(m, gameInfo().getMagicRules(), gameInfo().getSettings());
	const auto & resources = gameInfo().getPlayerState(h->tempOwner)->resources;

	if (!resources.canAfford(tuition))
		COMPLAIN_RET("You can't afford to buy this skill");

	// Validate the complete basket before applying a single relative resource
	// pack. This prevents partial payment when one rare resource is missing.
	giveResources(h->tempOwner, -tuition);

	changeSecSkill(h, skill, 1, ChangeValueMode::ABSOLUTE);
	return true;
}

bool CGameHandler::buyHouseOfWisdomScroll(const IMarket *m, const CGHeroInstance *h, SpellID spell)
{
	COMPLAIN_RET_FALSE_IF(!h, "You need a hero to buy a spell scroll!");
	COMPLAIN_RET_FALSE_IF(!newHorizonsHouseOfWisdom::active(m, gameInfo().getMagicRules()), "This market does not sell spell scrolls!");

	const auto * town = dynamic_cast<const CGTownInstance *>(m);
	COMPLAIN_RET_FALSE_IF(!town, "Wrong House of Wisdom market!");
	COMPLAIN_RET_FALSE_IF(!vstd::contains(m->availableItemsIds(EMarketMode::RESOURCE_SKILL), spell), "That spell scroll is unavailable!");

	const auto * definition = spell.toSpell();
	COMPLAIN_RET_FALSE_IF(!definition || !definition->isCommonHeroSpell() || definition->isAdventure(), "That spell cannot be sold as a scroll!");
	COMPLAIN_RET_FALSE_IF(!newHorizonsMagic::spellAvailableForOrdinaryAcquisition(gameInfo().getMagicRules(), spell),
		"That spell is not available for ordinary acquisition!");

	const auto price = newHorizonsHouseOfWisdom::price(spell);
	const auto & resources = gameInfo().getPlayerState(h->tempOwner)->resources;
	COMPLAIN_RET_FALSE_IF(!resources.canAfford(price), "You can't afford to buy this spell scroll!");

	const auto * scroll = ArtifactID(ArtifactID::SPELL_SCROLL).toArtifact();
	const auto scrollPosition = ArtifactUtils::getArtAnyPosition(h, scroll->getId());
	COMPLAIN_RET_FALSE_IF(!scroll->canBePutAt(h, scrollPosition), "The hero has no room for this spell scroll!");

	// Remove exactly the purchased offer from the authoritative town stock.
	// The packet is applied on the server and replicated to clients, so a
	// client cannot refill or otherwise alter the storefront locally.
	auto remaining = town->getHouseOfWisdomScrolls();
	std::vector<SpellID> updatedStock(remaining.begin(), remaining.end());
	const auto it = std::find(updatedStock.begin(), updatedStock.end(), spell);
	COMPLAIN_RET_FALSE_IF(it == updatedStock.end(), "That spell scroll is unavailable!");
	updatedStock.erase(it);

	SetHouseOfWisdomScrolls stock;
	stock.townId = town->id;
	stock.scrolls = std::move(updatedStock);
	sendAndApply(stock);
	giveResources(h->tempOwner, -price);
	return giveHeroNewScroll(h, spell, ArtifactPosition::FIRST_AVAILABLE);
}

bool CGameHandler::tradeResources(const IMarket *market, ui32 amountToSell, PlayerColor player, GameResID toSell, GameResID toBuy)
{
	const auto & tradeableResources = market->availableItemsIds(EMarketMode::RESOURCE_RESOURCE);
	if(!vstd::contains(tradeableResources, TradeItemBuy(toSell)) || !vstd::contains(tradeableResources, TradeItemBuy(toBuy)))
		COMPLAIN_RET("Market does not trade this resource!");

	TResourceCap haveToSell = gameInfo().getPlayerState(player)->resources[toSell];

	vstd::amin(amountToSell, haveToSell); //can't trade more resources than have

	int b1; //base quantities for trade
	int b2;
	market->getOffer(toSell, toBuy, b1, b2, EMarketMode::RESOURCE_RESOURCE);
	int amountToBuy = amountToSell / b1; //how many base quantities we trade

	if (amountToSell % b1 != 0) //all offered units of resource should be used, if not -> somewhere in calculations must be an error
	{
		COMPLAIN_RET("Invalid deal, not all offered units of resource were used.");
	}

	giveResource(player, toSell, -b1 * amountToBuy);
	giveResource(player, toBuy, b2 * amountToBuy);

	statistics->getPlayerAccumulator(player).tradeVolume[toSell] += -b1 * amountToBuy;
	statistics->getPlayerAccumulator(player).tradeVolume[toBuy] += b2 * amountToBuy;

	return true;
}

bool CGameHandler::sellCreatures(ui32 count, const IMarket *market, const CGHeroInstance * hero, SlotID slot, GameResID resourceID)
{
	if(!hero)
		COMPLAIN_RET("Only hero can sell creatures!");
	if (!vstd::contains(hero->Slots(), slot))
		COMPLAIN_RET("Hero doesn't have any creature in that slot!");

	const CStackInstance &s = hero->getStack(slot);

	if (s.getCount() < static_cast<TQuantity>(count) //can't sell more creatures than have
		|| (hero->stacksCount() == 1 && hero->needsLastStack() && s.getCount() == count)) //can't sell last stack
	{
		COMPLAIN_RET("Not enough creatures in army!");
	}

	int b1; //base quantities for trade
	int b2;
	market->getOffer(s.getId(), resourceID, b1, b2, EMarketMode::CREATURE_RESOURCE);
	int units = count / b1; //how many base quantities we trade

	if (count%b1) //all offered units of resource should be used, if not -> somewhere in calculations must be an error
	{
		//TODO: complain?
		assert(0);
	}

	changeStackCount(StackLocation(hero->id, slot), -static_cast<int>(count), ChangeValueMode::RELATIVE);

	giveResource(hero->tempOwner, resourceID, b2 * units);

	return true;
}

bool CGameHandler::transformInUndead(const IMarket *market, const CGHeroInstance * hero, SlotID slot)
{
	const CArmedInstance *army = nullptr;
	if (hero)
		army = hero;
	else
		army = dynamic_cast<const CGTownInstance *>(market);

	if (!army)
		COMPLAIN_RET("Incorrect call to transform in undead!");
	if (!army->hasStackAtSlot(slot))
		COMPLAIN_RET("Army doesn't have any creature in that slot!");


	const CStackInstance &s = army->getStack(slot);

	//resulting creature - bone dragons or skeletons
	CreatureID resCreature = CreatureID::SKELETON;

	auto customTargerBonus = s.getBonusesOfType(BonusType::SKELETON_TRANSFORMER_TARGET);
	if (!customTargerBonus->empty())
		resCreature = customTargerBonus->front()->subtype.as<CreatureID>();

	changeStackType(StackLocation(army->id, slot), resCreature.toCreature());
	return true;
}

bool CGameHandler::sendResources(ui32 val, PlayerColor player, GameResID r1, PlayerColor r2)
{
	const PlayerState *p2 = gameInfo().getPlayerState(r2, false);
	if (!p2  ||  p2->status != EPlayerStatus::INGAME)
	{
		complain("Dest player must be in game!");
		return false;
	}

	TResourceCap curRes1 = gameInfo().getPlayerState(player)->resources[r1];

	vstd::amin(val, curRes1);

	giveResource(player, r1, -static_cast<int>(val));
	giveResource(r2, r1, val);

	return true;
}

void CGameHandler::informPlayerAboutSentResources(PlayerColor player, PlayerColor playerReceiver, const ResourceSet & resources)
{
	InfoWindow iw;
	iw.player = playerReceiver;
	iw.text = MetaString::createFromTextID("core.genrltxt.358");
	iw.text.replaceName(player);
	for(auto it = ResourceSet::nziterator(resources); it.valid(); it++)
	{
		if(it->resVal > 0)
			iw.components.emplace_back(ComponentType::RESOURCE, it->resType, it->resVal);
	}
	sendAndApply(iw);
}

bool CGameHandler::setFormation(ObjectInstanceID hid, EArmyFormation formation)
{
	const CGHeroInstance *h = gameInfo().getHero(hid);
	if (!h)
	{
		logGlobal->error("Hero doesn't exist!");
		return false;
	}

	ChangeFormation cf;
	cf.hid = hid;
	cf.formation = formation;
	sendAndApply(cf);

	return true;
}

bool CGameHandler::setTactics(ObjectInstanceID hid, bool enabled)
{
	const CGHeroInstance *h = gameInfo().getHero(hid);
	if (!h)
	{
		logGlobal->error("Hero doesn't exist!");
		return false;
	}

	ChangeTactics ct;
	ct.hid = hid;
	ct.enabled = enabled;
	sendAndApply(ct);

	return true;
}

bool CGameHandler::setTownName(ObjectInstanceID tid, std::string & name)
{
	const CGTownInstance *t = gameInfo().getTown(tid);
	if (!t)
	{
		logGlobal->error("Town doesn't exist!");
		return false;
	}

	ChangeTownName ctn;
	ctn.tid = tid;
	ctn.name = name;
	sendAndApply(ctn);

	return true;
}

bool CGameHandler::heroMasteryReply(QueryID qid, ObjectInstanceID heroId, uint64_t sequence, int32_t choice, PlayerColor player)
{
	using namespace newHorizonsHeroes;
	COMPLAIN_RET_FALSE_IF(!player.isValidPlayer(), "Invalid mastery reply player");
	const auto top = queries->topQuery(player);
	COMPLAIN_RET_FALSE_IF(!top || top->queryID != qid || top->getType() != CHeroMasteryDialogQuery::TYPE,
		"Mastery reply does not match the current query");
	auto & query = static_cast<CHeroMasteryDialogQuery &>(*top);
	COMPLAIN_RET_FALSE_IF(query.accepted || query.heroId != heroId, "Stale or wrong hero mastery query");
	const auto * hero = gameInfo().getHero(heroId);
	COMPLAIN_RET_FALSE_IF(!hero || hero->getOwner() != player || !hero->getMasteryState().pending,
		"Mastery reply does not belong to the pending hero");
	const auto & offer = *hero->getMasteryState().pending;
	const auto error = validateMasteryReply(offer, heroId, player, sequence, hero->level,
		hero->getSecSkillLevel(offer.skill), hero->getMasteryState().hasChoice(offer.skill), choice);
	COMPLAIN_RET_FALSE_IF(error != MasteryReplyError::NONE, "Invalid mastery selection");
	HeroMasteryChosen chosen;
	chosen.hero = heroId;
	chosen.sequence = sequence;
	chosen.choice = choice;
	sendAndApply(chosen); // State and effects precede QueryResolved and the request ACK.
	query.accepted = true;
	queries->popQuery(top);
	return true;
}

bool CGameHandler::queryReply(QueryID qid, std::optional<int32_t> answer, PlayerColor player)
{
	logGlobal->trace("Player %s attempts answering query %d with answer:", player, qid);
	if (answer)
		logGlobal->trace("%d", *answer);

	auto topQuery = queries->topQuery(player);

	COMPLAIN_RET_FALSE_IF(!topQuery, "This player doesn't have any queries!");

	if(topQuery->queryID != qid)
	{
		auto currentQuery = queries->getQuery(qid);

		if(currentQuery != nullptr && vstd::contains(currentQuery->players, player)
			&& currentQuery->getType() != CHeroMasteryDialogQuery::TYPE
			&& currentQuery->endsByPlayerAnswer() && currentQuery->isValidReply(answer))
		{
			currentQuery->setReply(answer);
			// Necromancy can be answered while a post-battle level-up query is above it.
			// Keep it pending until exposure pops it and runs the choice callback.
			if(currentQuery->getType() == CNecromancyQuery::TYPE)
				return true;
		}

		COMPLAIN_RET("This player top query has different ID!"); //topQuery->queryID != qid
	}
	COMPLAIN_RET_FALSE_IF(topQuery->getType() == CHeroMasteryDialogQuery::TYPE, "Mastery requires a dedicated validated reply");
	COMPLAIN_RET_FALSE_IF(!topQuery->endsByPlayerAnswer(), "This query cannot be ended by player's answer!");
	COMPLAIN_RET_FALSE_IF(!topQuery->isValidReply(answer), "Invalid query reply");

	topQuery->setReply(answer);
	queries->popQuery(topQuery);
	return true;
}

bool CGameHandler::complain(const std::string &problem)
{
#ifndef ENABLE_GOLDMASTER
	MetaString str;
	str.appendTextID("vcmi.broadcast.serverProblem");
	str.appendRawString(": ");
	str.appendRawString(problem);
	playerMessages->broadcastSystemMessage(str);
#endif
	logGlobal->error(problem);
	return true;
}

void CGameHandler::showGarrisonDialog(ObjectInstanceID upobj, ObjectInstanceID hid, bool removableUnits, const MetaString & customTitle)
{
	const auto * upperArmy = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(upobj));
	const auto * lowerArmy = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(hid));

	assert(lowerArmy);
	assert(upperArmy);

	auto garrisonQuery = std::make_shared<CGarrisonDialogQuery>(this, upperArmy, lowerArmy);
	queries->addQuery(garrisonQuery);

	GarrisonDialog gd;
	gd.hid = hid;
	gd.objid = upobj;
	gd.removableUnits = removableUnits;
	gd.customTitle = customTitle;
	gd.queryID = garrisonQuery->queryID;
	sendAndApply(gd);
}

void CGameHandler::showObjectWindow(const CGObjectInstance * object, EOpenWindowMode window, const CGHeroInstance * visitor, bool addQuery)
{
	OpenWindow pack;
	pack.window = window;
	pack.object = object->id;
	pack.visitor = visitor->id;

	if (addQuery)
	{
		auto windowQuery = std::make_shared<OpenWindowQuery>(this, visitor, window);
		pack.queryID = windowQuery->queryID;
		queries->addQuery(windowQuery);
	}
	sendAndApply(pack);
}

bool CGameHandler::isAllowedExchange(ObjectInstanceID id1, ObjectInstanceID id2)
{
	if (id1 == id2)
		return true;

	for(const auto & query : queries->allQueries())
	{
		const auto * garrisonQuery = dynamic_cast<const CGarrisonDialogQuery *>(query.get());
		if(garrisonQuery == nullptr)
			continue;

		const bool matchesForward = garrisonQuery->exchangingArmies[0]->id == id1 && garrisonQuery->exchangingArmies[1]->id == id2;
		const bool matchesBackward = garrisonQuery->exchangingArmies[0]->id == id2 && garrisonQuery->exchangingArmies[1]->id == id1;
		if(matchesForward || matchesBackward)
			return true;
	}

	const CGObjectInstance *o1 = gameInfo().getObj(id1);
	const CGObjectInstance *o2 = gameInfo().getObj(id2);
	if (!o1 || !o2)
		return true; //arranging stacks within an object should be always allowed

	if (o1 && o2)
	{
		if (o1->ID == Obj::TOWN)
		{
			const auto *t = dynamic_cast<const CGTownInstance*>(o1);
			if (t->getVisitingHero() == o2  ||  t->getGarrisonHero() == o2)
				return true;
		}
		if (o2->ID == Obj::TOWN)
		{
			const auto *t = dynamic_cast<const CGTownInstance*>(o2);
			if (t->getVisitingHero() == o1  ||  t->getGarrisonHero() == o1)
				return true;
		}

		const auto * market = gameState().getMarket(id1);
		if(market == nullptr)
			market = gameState().getMarket(id2);
		if(market)
			return market->allowsTrade(EMarketMode::ARTIFACT_EXP);

		if (o1->ID == Obj::HERO && o2->ID == Obj::HERO)
		{
			const auto *h1 = dynamic_cast<const CGHeroInstance*>(o1);
			const auto *h2 = dynamic_cast<const CGHeroInstance*>(o2);

			// two heroes in same town (garrisoned and visiting)
			if (h1->getVisitedTown() != nullptr && h2->getVisitedTown() != nullptr && h1->getVisitedTown() == h2->getVisitedTown())
				return true;
		}

		// Ongoing garrison exchange
		const auto * dialog = queries->findQuery<CGarrisonDialogQuery>(
			[o1, o2](const CGarrisonDialogQuery & query)
			{
				const auto * topArmy = query.exchangingArmies.at(0);
				const auto * bottomArmy = query.exchangingArmies.at(1);

				return (topArmy == o1 && bottomArmy == o2) || (topArmy == o2 && bottomArmy == o1);
			});

		if(dialog)
			return true;
	}

	return false;
}

void CGameHandler::objectVisited(const CGObjectInstance * visitedObject, const CGHeroInstance * h)
{
	using events::ObjectVisitStarted;

	logGlobal->debug("%s visits %s (%d)", h->nodeName(), visitedObject->getObjectNameTextID(), visitedObject->ID);

	if (getVisitingHero(visitedObject) != nullptr)
	{
		logGlobal->error("Attempt to visit object that is being visited by another hero!");
		throw std::runtime_error("Can not visit object that is being visited");
	}

	std::shared_ptr<MapObjectVisitQuery> visitQuery;

	if(visitedObject->ID == Obj::HERO)
	{
		const auto * visitedHero = dynamic_cast<const CGHeroInstance *>(visitedObject);
		const auto * visitedTown = visitedHero->getVisitedTown();

		if(visitedTown)
		{
			const bool isEnemy = visitedHero->getOwner() != h->getOwner();

			if(isEnemy && !visitedTown->isBattleOutsideTown(visitedHero))
				visitedObject = visitedTown;
		}
	}
	visitQuery = std::make_shared<MapObjectVisitQuery>(this, visitedObject, h);
	queries->addQuery(visitQuery); //TODO real visit pos

	HeroVisit hv;
	hv.objId = visitedObject->id;
	hv.heroId = h->id;
	hv.player = h->tempOwner;
	hv.starting = true;
	sendAndApply(hv);

	std::string scriptHandler = visitedObject->getVisitScriptHandler();
	auto * dispatcher = gameState().getMapEventDispatcher();
	if(!scriptHandler.empty() && dispatcher)
		runScriptedEvent(*dispatcher, h->getOwner(), h->id,
			[&](scripting::MapEventDispatcher & d){ return d.onObjectVisit(*this, scriptHandler, visitedObject, h); });
	else
		visitedObject->onHeroVisit(*this, h);

	if(visitQuery)
		queries->popIfTop(visitQuery); //visit ends here if no queries were created
}

void CGameHandler::objectVisitEnded(const ObjectInstanceID & heroObjectID, PlayerColor player)
{
	HeroVisit hv;
	hv.player = player;
	hv.heroId = heroObjectID;
	hv.starting = false;
	sendAndApply(hv);
}

bool CGameHandler::buildBoat(ObjectInstanceID objid, PlayerColor playerID)
{
	const auto *obj = dynamic_cast<const IShipyard *>(gameInfo().getObj(objid));

	if (obj->shipyardStatus() != IBoatGenerator::GOOD)
	{
		complain("Cannot build boat in this shipyard!");
		return false;
	}

	TResources boatCost;
	obj->getBoatCost(boatCost);
	TResources available = gameInfo().getPlayerState(playerID)->resources;

	if (!available.canAfford(boatCost))
	{
		complain("Not enough resources to build a boat!");
		return false;
	}

	int3 tile = obj->bestLocation();
	if (!gameState().getMap().isInTheMap(tile))
	{
		complain("Cannot find appropriate tile for a boat!");
		return false;
	}

	giveResources(playerID, -boatCost);
	createBoat(tile, obj->getBoatType(), playerID);
	return true;
}

void CGameHandler::checkVictoryLossConditions(const std::set<PlayerColor> & playerColors)
{
	for (auto playerColor : playerColors)
	{
		if (gameInfo().getPlayerState(playerColor, false))
			checkVictoryLossConditionsForPlayer(playerColor);
	}
}

void CGameHandler::checkVictoryLossConditionsForAll()
{
	std::set<PlayerColor> playerColors;
	for (int i = 0; i < PlayerColor::PLAYER_LIMIT_I; ++i)
	{
		playerColors.insert(PlayerColor(i));
	}
	checkVictoryLossConditions(playerColors);
}

void CGameHandler::checkVictoryLossConditionsForPlayer(PlayerColor player)
{
	const PlayerState * p = gameInfo().getPlayerState(player);

	if(!p || p->status != EPlayerStatus::INGAME) return;

	if(gameState().getMap().battleOnly)
	{
		for(const auto & playerIt : gameState().players)
		{
			PlayerEndsGame peg;
			peg.player = playerIt.first;
			peg.silentEnd = true;
			sendAndApply(peg);
		}
		gameServer().setState(EServerState::SHUTDOWN);
		return;
	}

	auto victoryLossCheckResult = gameState().checkForVictoryAndLoss(player);

	if (victoryLossCheckResult.victory() || victoryLossCheckResult.loss())
	{
		InfoWindow iw;
		getVictoryLossMessage(player, victoryLossCheckResult, iw);
		sendAndApply(iw);

		PlayerEndsGame peg;
		peg.player = player;
		peg.victoryLossCheckResult = victoryLossCheckResult;
		peg.statistic = *statistics;
		addStatistics(peg.statistic); // add last turn befor win / loss
		sendAndApply(peg);

		turnOrder->removePlayer(player);

		if (victoryLossCheckResult.victory())
		{
			//one player won -> all enemies lost
			for (const auto & playerIt : gameState().players)
			{
				if (playerIt.first != player && gameInfo().getPlayerState(playerIt.first)->status == EPlayerStatus::INGAME)
				{
					peg.player = playerIt.first;
					peg.victoryLossCheckResult = gameInfo().getPlayerRelations(player, playerIt.first) == PlayerRelations::ALLIES ?
								victoryLossCheckResult : victoryLossCheckResult.invert(); // ally of winner

					InfoWindow iwOthers;
					getVictoryLossMessage(player, peg.victoryLossCheckResult, iwOthers);
					iwOthers.player = playerIt.first;

					sendAndApply(iwOthers);
					sendAndApply(peg);
				}
			}

			if(p->human)
			{
				gameServer().setState(EServerState::SHUTDOWN);
			}
		}
		else
		{
			//copy heroes vector to avoid iterator invalidation as removal change PlayerState
			auto hlp = p->getHeroes();
			for (const auto * h : hlp) //eliminate heroes
			{
				if (h)
					removeObject(h, player);
			}

			//player lost -> all his objects become unflagged (neutral)
			for (const auto * obj : gameState().getMap().getObjects()) //unflag objs
			{
				if (obj && obj->tempOwner == player)
					setOwner(obj, PlayerColor::NEUTRAL);
			}

			//eliminating one player may cause victory of another:
			std::set<PlayerColor> playerColors;

			//do not copy player state (CBonusSystemNode) by value
			for (const auto &playerState : gameState().players) //players may have different colors, iterate over players and not integers
			{
				if (playerState.first != player)
					playerColors.insert(playerState.first);
			}

			//notify all players
			for (auto pc : playerColors)
			{
				if (gameInfo().getPlayerState(pc)->status == EPlayerStatus::INGAME)
				{
					InfoWindow iwOthers;
					getVictoryLossMessage(player, victoryLossCheckResult.invert(), iwOthers);
					iwOthers.player = pc;
					sendAndApply(iwOthers);
				}
			}
			checkVictoryLossConditions(playerColors);

			bool hasAlivePlayers = false;
			for (auto pc : playerColors)
				if (gameInfo().getPlayerState(pc)->status == EPlayerStatus::INGAME)
					hasAlivePlayers = true;

			// everyone lost (e.g. time runs out)
			if (!hasAlivePlayers)
				gameServer().setState(EServerState::SHUTDOWN);

			// give turn to next player(s)
			// FIXME: this may cause multiple calls to resumeTurnOrder if multiple players lose in chain reaction
			if(gameServer().getState() != EServerState::SHUTDOWN)
				turnOrder->resumeTurnOrder();
		}
	}
}

void CGameHandler::getVictoryLossMessage(PlayerColor player, const EVictoryLossCheckResult & victoryLossCheckResult, InfoWindow & out) const
{
	out.player = player;
	out.text = victoryLossCheckResult.messageToSelf;
	out.text.replaceName(player);
	out.components.emplace_back(ComponentType::FLAG, player);
}

bool CGameHandler::dig(const CGHeroInstance *h)
{
	if (h->diggingStatus() != EDiggingStatus::CAN_DIG) //checks for terrain and movement
		COMPLAIN_RETF("Hero cannot dig (error code %d)!", static_cast<int>(h->diggingStatus()));

	createHole(h->visitablePos(), h->getOwner());

	//take MPs
	SetMovePoints smp;
	smp.hid = h->id;
	smp.val = 0;
	sendAndApply(smp);

	InfoWindow iw;
	iw.type = EInfoWindowMode::AUTO;
	iw.player = h->tempOwner;
	if (gameState().getMap().grailPos == h->visitablePos())
	{
		ArtifactID grail = ArtifactID::GRAIL;

		iw.text.appendTextID("core.genrltxt.58"); //"Congratulations! After spending many hours digging here, your hero has uncovered the " ...
		iw.text.appendName(grail); // ... " The Grail"
		iw.soundID = soundBase::ULTIMATEARTIFACT;
		giveHeroNewArtifact(h, grail, ArtifactPosition::FIRST_AVAILABLE); //give grail
		sendAndApply(iw);

		iw.soundID = soundBase::invalid;
		iw.components.emplace_back(ComponentType::ARTIFACT, grail);
		iw.text.clear();
		iw.text.appendTextID(grail.toArtifact()->getDescriptionTextID());
		sendAndApply(iw);
	}
	else
	{
		iw.text.appendTextID("core.genrltxt.59"); //"Nothing here. \n Where could it be?"
		iw.soundID = soundBase::Dig;
		sendAndApply(iw);
	}

	return true;
}

void CGameHandler::visitObjectOnTile(const TerrainTile &t, const CGHeroInstance * h)
{
	if (!t.visitableObjects.empty())
	{
		//to prevent self-visiting heroes on space press
		if (t.visitableObjects.back() != h->id)
			objectVisited(gameState().getObjInstance(t.visitableObjects.back()), h);
		else if (t.visitableObjects.size() > 1)
			objectVisited(gameState().getObjInstance(*(t.visitableObjects.end()-2)),h);
	}
}

bool CGameHandler::sacrificeCreatures(const IMarket * market, const CGHeroInstance * hero, const std::vector<SlotID> & slot, const std::vector<ui32> & count)
{
	if (!hero)
		COMPLAIN_RET("You need hero to sacrifice creature!");

	int expSum = 0;
	auto finish = [this, &hero, &expSum]()
	{
		giveExperience(hero, hero->calculateXp(expSum));
	};

	for(int i = 0; i < slot.size(); ++i)
	{
		int oldCount = hero->getStackCount(slot[i]);

		if(oldCount < static_cast<int>(count[i]))
		{
			finish();
			COMPLAIN_RET("Not enough creatures to sacrifice!")
		}
		else if(oldCount == count[i] && hero->stacksCount() == 1 && hero->needsLastStack())
		{
			finish();
			COMPLAIN_RET("Cannot sacrifice last creature!");
		}

		int crid = hero->getStack(slot[i]).getId();

		changeStackCount(StackLocation(hero->id, slot[i]), -(TQuantity)count[i], ChangeValueMode::RELATIVE);

		int dump;
		int exp;
		market->getOffer(crid, 0, dump, exp, EMarketMode::CREATURE_EXP);
		exp *= count[i];
		expSum += exp;
	}

	finish();

	return true;
}

bool CGameHandler::sacrificeArtifact(const IMarket * market, const CGHeroInstance * hero, const std::vector<ArtifactInstanceID> & arts)
{
	if (!hero)
		COMPLAIN_RET("You need hero to sacrifice artifact!");
	if(hero->getAlignment() == EAlignment::EVIL)
		COMPLAIN_RET("Evil hero can't sacrifice artifact!");

	assert(market);
	const auto * artSet = market->getArtifactsStorage();

	int expSum = 0;
	std::vector<ArtifactPosition> artPack;
	auto finish = [this, &hero, &expSum, &artPack, market]()
	{
		removeArtifact(market->getObjInstanceID(), artPack);
		giveExperience(hero, hero->calculateXp(expSum));
	};

	for(const auto & artInstId : arts)
	{
		if(const auto * art = artSet->getArtByInstanceId(artInstId))
		{
			if(art->getType()->isTradable())
			{
				int dmp;
				int expToGive;
				market->getOffer(art->getTypeId(), 0, dmp, expToGive, EMarketMode::ARTIFACT_EXP);
				expSum += expToGive;
				artPack.push_back(artSet->getArtPos(art));
			}
			else
			{
				COMPLAIN_RET("Cannot sacrifice not tradable artifact!");
			}
		}
		else
		{
			finish();
			COMPLAIN_RET("Cannot find artifact to sacrifice!");
		}
	}

	finish();

	return true;
}

bool CGameHandler::insertNewStack(const StackLocation &sl, const CCreature *c, TQuantity count)
{
	const auto * army = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(sl.army));

	if (army->hasStackAtSlot(sl.slot))
		COMPLAIN_RET("Slot is already taken!");

	if (!sl.slot.validSlot())
		COMPLAIN_RET("Cannot insert stack to that slot!");
	if(!validateLeadershipStack(army, c->getId(), count))
		return false;

	InsertNewStack ins;
	ins.army = army->id;
	ins.slot = sl.slot;
	ins.type = c->getId();
	ins.count = count;
	sendAndApply(ins);
	return true;
}

bool CGameHandler::eraseStack(const StackLocation &sl, bool forceRemoval)
{
	const auto * army = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(sl.army));

	if (!army->hasStackAtSlot(sl.slot))
		COMPLAIN_RET("Cannot find a stack to erase");

	if (army->stacksCount() == 1 //from the last stack
		&& army->needsLastStack() //that must be left
		&& !forceRemoval) //ignore above conditions if we are forcing removal
	{
		COMPLAIN_RET("Cannot erase the last stack!");
	}

	EraseStack es;
	es.army = army->id;
	es.slot = sl.slot;
	sendAndApply(es);
	return true;
}

bool CGameHandler::changeStackCount(const StackLocation &sl, TQuantity count, ChangeValueMode mode)
{
	const auto * army = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(sl.army));

	TQuantity currentCount = army->getStackCount(sl.slot);
	const TQuantity resultingCount = mode == ChangeValueMode::ABSOLUTE ? count : currentCount + count;
	if(resultingCount > currentCount && !validateLeadershipStack(
		army, army->getCreature(sl.slot)->getId(), resultingCount))
		return false;
	if ((mode == ChangeValueMode::ABSOLUTE && count < 0)
		|| (mode == ChangeValueMode::RELATIVE && -count > currentCount))
	{
		COMPLAIN_RET("Cannot take more stacks than present!");
	}

	if ((currentCount == -count  &&  mode == ChangeValueMode::RELATIVE)
	   || (count == 0 && mode == ChangeValueMode::ABSOLUTE))
	{
		eraseStack(sl);
	}
	else
	{
		ChangeStackCount csc;
		csc.army = army->id;
		csc.slot = sl.slot;
		csc.count = count;
		csc.mode = mode;
		sendAndApply(csc);
	}
	return true;
}

bool CGameHandler::addToSlot(const StackLocation &sl, const CCreature *c, TQuantity count)
{
	const auto * army = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(sl.army));

	const CCreature *slotC = army->getCreature(sl.slot);
	if (!slotC) //slot is empty
		insertNewStack(sl, c, count);
	else if (c == slotC)
		changeStackCount(sl, count, ChangeValueMode::RELATIVE);
	else
	{
		COMPLAIN_RET("Cannot add " + c->getNamePluralTranslated() + " to slot " + std::to_string(sl.slot.getNum()) + "!");
	}
	return true;
}

void CGameHandler::tryJoiningArmy(const CArmedInstance *src, const CArmedInstance *dst, bool removeObjWhenFinished, bool allowMerging)
{
	if (removeObjWhenFinished)
		removeAfterVisit(src->id);

	// A New Horizons hero may accept more joining creatures than one of their
	// Leadership-limited slots can hold.  Treat this like any other partial
	// exchange: move the legal amount now and leave the remainder in the source
	// army for the garrison dialog.  Calling moveStack with the full amount would
	// correctly fail validation, but would also turn an ordinary join decision
	// into a server error for both human players and AI.
	if(const auto * hero = dynamic_cast<const CGHeroInstance *>(dst))
	{
		struct PlannedJoin
		{
			SlotID source;
			SlotID destination;
			TQuantity count;
		};
		struct ProjectedSlot
		{
			CreatureID creature;
			TQuantity count = 0;
			bool occupied = false;
		};
		std::array<ProjectedSlot, GameConstants::ARMY_SIZE> projected;
		for(const auto & [slot, stack] : dst->Slots())
			projected[slot.getNum()] = {stack->getCreatureID(), stack->getCount(), true};

		std::vector<PlannedJoin> plan;
		bool leadershipLimited = false;
		for(const auto & [sourceSlot, sourceStack] : src->Slots())
		{
			const auto creature = sourceStack->getCreatureID();
			int destinationIndex = -1;
			for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
				if(projected[i].occupied && projected[i].creature == creature)
				{
					destinationIndex = i;
					break;
				}
			if(destinationIndex < 0)
				for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
					if(!projected[i].occupied)
					{
						destinationIndex = i;
						projected[i] = {creature, 0, true};
						break;
					}
			if(destinationIndex < 0)
			{
				// A full destination may need a manual duplicate-stack merge
				// before this creature has a slot.  Keep the authoritative
				// exchange dialog, but do not run the legacy all-or-nothing
				// mover when Leadership would constrain the eventual stack.
				leadershipLimited |= hero->getLeadershipSlotCapacity(creature).has_value();
				continue;
			}

			auto legalTransfer = sourceStack->getCount();
			if(const auto capacity = hero->getLeadershipSlotCapacity(creature))
			{
				legalTransfer = std::max<TQuantity>(0,
					std::min<TQuantity>(legalTransfer, capacity->maximum - projected[destinationIndex].count));
				leadershipLimited |= legalTransfer < sourceStack->getCount();
			}
			projected[destinationIndex].count += legalTransfer;
			plan.push_back({sourceSlot, SlotID(destinationIndex), legalTransfer});
		}

		if(leadershipLimited)
		{
			for(const auto & move : plan)
				if(move.count > 0)
					moveStack(StackLocation(src->id, move.source), StackLocation(dst->id, move.destination), move.count);
			if(src->stacksCount() > 0)
				showGarrisonDialog(src->id, dst->id, true, MetaString());
			return;
		}
	}

	if (!src->canBeMergedWith(*dst, allowMerging))
	{
		if (allowMerging) //do that, add all matching creatures.
		{
			bool cont = true;
			while (cont)
			{
				cont = false;
				for (auto i = src->stacks.begin(); i != src->stacks.end(); i++)//while there are unmoved creatures
				{
					SlotID pos = dst->getSlotFor(i->second->getCreature());
					if (pos.validSlot())
					{
						cont = moveStack(StackLocation(src->id, i->first), StackLocation(dst->id, pos));
						break; //or iterator crashes
					}
				}
			}
		}
		showGarrisonDialog(src->id, dst->id, true, MetaString()); //show garrison window and optionally remove ourselves from map when player ends
	}
	else //merge
	{
		if(!moveArmy(src, dst, allowMerging))
			showGarrisonDialog(src->id, dst->id, true, MetaString());
	}
}

bool CGameHandler::moveStack(const StackLocation &src, const StackLocation &dst, TQuantity count)
{
	const auto * srcArmy = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(src.army));
	const auto * dstArmy = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(dst.army));
	if(!srcArmy || !dstArmy)
		COMPLAIN_RET("Cannot move stacks between non-existing armies!");

	if(src.army == dst.army && src.slot == dst.slot)
		COMPLAIN_RET("Cannot move a stack to itself!");

	if (!srcArmy->hasStackAtSlot(src.slot))
		COMPLAIN_RET("No stack to move!");

	if (dstArmy->hasStackAtSlot(dst.slot) && dstArmy->getCreature(dst.slot) != srcArmy->getCreature(src.slot))
		COMPLAIN_RET("Cannot move: stack of different type at destination pos!");

	if (!dst.slot.validSlot())
		COMPLAIN_RET("Cannot move stack to that slot!");

	if (count == -1)
	{
		count = srcArmy->getStackCount(src.slot);
	}
	if(count < 1 || count > srcArmy->getStackCount(src.slot))
		COMPLAIN_RET("Invalid stack transfer amount!");

	const int64_t destinationCount = dstArmy->hasStackAtSlot(dst.slot)
		? static_cast<int64_t>(dstArmy->getStackCount(dst.slot)) + count : count;
	if(destinationCount > std::numeric_limits<TQuantity>::max())
		COMPLAIN_RET("Cannot exceed the maximum stack size!");
	if((srcArmy != dstArmy || src.slot != dst.slot)
		&& !validateLeadershipStack(dstArmy, srcArmy->getCreature(src.slot)->getId(), destinationCount))
		return false;

	if (srcArmy != dstArmy  //moving away
		&&  count == srcArmy->getStackCount(src.slot) //all creatures
		&& srcArmy->stacksCount() == 1 //from the last stack
		&& srcArmy->needsLastStack()) //that must be left
	{
		COMPLAIN_RET("Cannot move away the last creature!");
	}

	RebalanceStacks rs;
	rs.srcArmy = srcArmy->id;
	rs.dstArmy = dstArmy->id;
	rs.srcSlot = src.slot;
	rs.dstSlot = dst.slot;
	rs.count = count;
	sendAndApply(rs);
	return true;
}

void CGameHandler::castSpell(const spells::Caster * caster, SpellID spellID, const int3 &pos)
{
	if (!spellID.hasValue())
		return;

	AdventureSpellCastParameters p;
	p.caster = caster;
	p.pos = pos;

	const CSpell * s = spellID.toSpell();
	s->adventureCast(spellEnv.get(), p);
}

bool CGameHandler::swapStacks(const StackLocation & sl1, const StackLocation & sl2)
{
	const auto * army1 = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(sl1.army));
	const auto * army2 = dynamic_cast<const CArmedInstance*>(gameInfo().getObj(sl2.army));
	if(!army1 || !army2)
		COMPLAIN_RET("Cannot swap stacks between non-existing armies!");
	auto moveIntoEmptySlot = [this](const CArmedInstance * source, const CArmedInstance * destination,
		const StackLocation & sourceLocation, const StackLocation & destinationLocation)
	{
		if(!source->hasStackAtSlot(sourceLocation.slot))
			return moveStack(sourceLocation, destinationLocation);
		const int64_t sourceCount = source->getStackCount(sourceLocation.slot);
		const auto * creature = source->getCreature(sourceLocation.slot);
		const bool mustKeepLastSourceCreature = source->id != destination->id
			&& source->needsLastStack() && source->stacksCount() == 1;
		// Whole-stack drags are move intents: reserve a required last creature,
		// then clamp the transfer to the destination hero's current capacity.
		int64_t transferCount = sourceCount - (mustKeepLastSourceCreature ? 1 : 0);
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(destination))
		{
			if(const auto capacity = hero->getLeadershipSlotCapacity(creature->getId()))
			{
				if(capacity->maximum <= 0)
					return validateLeadershipStack(destination, creature->getId(), 1);
				transferCount = std::min<int64_t>(transferCount, capacity->maximum);
			}
		}
		if(transferCount <= 0 && mustKeepLastSourceCreature && sourceCount > 0)
			COMPLAIN_RET("Cannot move away the last creature!");
		return moveStack(sourceLocation, destinationLocation, static_cast<TQuantity>(transferCount));
	};

	if(!army1->hasStackAtSlot(sl1.slot))
	{
		return moveIntoEmptySlot(army2, army1, sl2, sl1);
	}
	else if(!army2->hasStackAtSlot(sl2.slot))
	{
		return moveIntoEmptySlot(army1, army2, sl1, sl2);
	}
	else
	{
		if(!validateLeadershipStack(army1, army2->getCreature(sl2.slot)->getId(), army2->getStackCount(sl2.slot))
			|| !validateLeadershipStack(army2, army1->getCreature(sl1.slot)->getId(), army1->getStackCount(sl1.slot)))
			return false;
		SwapStacks ss;
		ss.srcArmy = army1->id;
		ss.dstArmy = army2->id;
		ss.srcSlot = sl1.slot;
		ss.dstSlot = sl2.slot;
		sendAndApply(ss);
		return true;
	}
}

bool CGameHandler::validateLeadershipStack(const CArmedInstance * destination, CreatureID creature, int64_t resultingCount)
{
	const auto * hero = dynamic_cast<const CGHeroInstance *>(destination);
	if(!hero)
		return true;
	const auto capacity = hero->getLeadershipSlotCapacity(creature);
	if(!capacity || (resultingCount >= 0 && resultingCount <= capacity->maximum))
		return true;
	complain("Leadership limit exceeded: this hero can command at most " + std::to_string(capacity->maximum)
		+ " creatures of this type (" + std::to_string(capacity->requirement)
		+ " Leadership each; hero Leadership " + std::to_string(capacity->leadership) + ").");
	return false;
}

bool CGameHandler::validateLeadershipArmyAddition(const CGHeroInstance * destination, const CCreatureSet & incoming)
{
	struct ProjectedSlot
	{
		CreatureID creature;
		TQuantity count = 0;
		bool occupied = false;
	};
	std::array<ProjectedSlot, GameConstants::ARMY_SIZE> projected;
	for(const auto & [slot, stack] : destination->Slots())
		projected[slot.getNum()] = {stack->getCreatureID(), stack->getCount(), true};
	for(const auto & entry : incoming.Slots())
	{
		const auto & stack = entry.second;
		int target = -1;
		for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
			if(projected[i].occupied && projected[i].creature == stack->getCreatureID())
			{
				target = i;
				break;
			}
		if(target < 0)
			for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
				if(!projected[i].occupied)
				{
					target = i;
					break;
				}
		if(target < 0)
		{
			int mergeSource = -1;
			int mergeDestination = -1;
			for(int i = 0; i < GameConstants::ARMY_SIZE && mergeSource < 0; ++i)
				for(int j = 0; j < GameConstants::ARMY_SIZE; ++j)
					if(i != j && projected[i].occupied && projected[j].occupied
						&& projected[i].creature == projected[j].creature)
					{
						mergeSource = i;
						mergeDestination = j;
						break;
					}
			if(mergeSource < 0)
				return false;
			projected[mergeDestination].count += projected[mergeSource].count;
			if(!validateLeadershipStack(destination, projected[mergeDestination].creature,
				projected[mergeDestination].count))
				return false;
			projected[mergeSource] = {};
			target = mergeSource;
		}
		if(!projected[target].occupied)
			projected[target] = {stack->getCreatureID(), 0, true};
		projected[target].count += stack->getCount();
		if(!validateLeadershipStack(destination, projected[target].creature, projected[target].count))
			return false;
	}
	return true;
}

bool CGameHandler::putArtifact(const ArtifactLocation & al, const ArtifactInstanceID & id, std::optional<bool> askAssemble)
{
	const auto * artInst = gameInfo().getArtInstance(id);
	assert(artInst && artInst->getType());
	ArtifactLocation dst(al.artHolder, ArtifactPosition::PRE_FIRST);
	dst.creature = al.creature;
	const auto * putTo = gameState().getArtSet(al);
	assert(putTo);

	if(al.slot == ArtifactPosition::FIRST_AVAILABLE)
	{
		dst.slot = ArtifactUtils::getArtAnyPosition(putTo, artInst->getTypeId());
	}
	else if(ArtifactUtils::isSlotBackpack(al.slot) && !al.creature.has_value())
	{
		dst.slot = ArtifactUtils::getArtBackpackPosition(putTo, artInst->getTypeId());
	}
	else
	{
		dst.slot = al.slot;
	}

	if(!askAssemble.has_value())
	{
		if(!dst.creature.has_value() && ArtifactUtils::isSlotEquipment(dst.slot))
			askAssemble = true;
		else
			askAssemble = false;
	}

	if(artInst->canBePutAt(putTo, dst.slot))
	{
		PutArtifact pa(id, dst, askAssemble.value());
		sendAndApply(pa);
		return true;
	}
	else
	{
		return false;
	}
}

bool CGameHandler::giveHeroNewArtifact(
	const CGHeroInstance * h, const CArtifact * artType, const SpellID & spellId, const ArtifactPosition & pos)
{
	assert(artType);

	NewArtifact na;
	na.artHolder = h->id;
	na.artId = artType->getId();
	na.spellId = spellId;
	na.pos = pos;

	if(pos == ArtifactPosition::FIRST_AVAILABLE)
	{
		na.pos = ArtifactUtils::getArtAnyPosition(h, artType->getId());
		if(!artType->canBePutAt(h, na.pos))
			COMPLAIN_RET("Cannot put artifact in that slot!");
	}
	else if(ArtifactUtils::isSlotBackpack(pos))
	{
		if(!artType->canBePutAt(h, ArtifactUtils::getArtBackpackPosition(h, artType->getId())))
			COMPLAIN_RET("Cannot put artifact in that slot!");
	}
	else
	{
		COMPLAIN_RET_FALSE_IF(!artType->canBePutAt(h, pos, false), "Cannot put artifact in that slot!");
	}
	sendAndApply(na);
	return true;
}

bool CGameHandler::giveHeroNewArtifact(const CGHeroInstance * h, const ArtifactID & artId, const ArtifactPosition & pos)
{
	return giveHeroNewArtifact(h, artId.toArtifact(), SpellID::NONE, pos);
}

bool CGameHandler::giveHeroNewScroll(const CGHeroInstance * h, const SpellID & spellId, const ArtifactPosition & pos)
{
	return giveHeroNewArtifact(h, ArtifactID(ArtifactID::SPELL_SCROLL).toArtifact(), spellId, pos);
}

void CGameHandler::spawnWanderingMonsters(CreatureID creatureID)
{
	std::vector<int3>::iterator tile;
	std::vector<int3> tiles;
	gameState().getFreeTiles(tiles, true);
	ui32 amount = tiles.size() / 200; //Chance is 0.5% for each tile

	RandomGeneratorUtil::randomShuffle(tiles, getRandomGenerator());
	logGlobal->trace("Spawning wandering monsters. Found %d free tiles. Creature type: %d", tiles.size(), creatureID.num);
	const CCreature *cre = creatureID.toCreature();
	for (int i = 0; i < amount; ++i)
	{
		tile = tiles.begin();
		logGlobal->trace("\tSpawning monster at %s", tile->toString());
		auto count = cre->getRandomAmount(getRandomGenerator());
		createWanderingMonster(*tile, creatureID, count);
		tiles.erase(tile); //not use it again
	}
}

bool CGameHandler::isBlockedByQueries(const CPackForServer *pack, PlayerColor player)
{
	if(!player.isValidPlayer())
		return false;

	if (dynamic_cast<const PlayerMessage *>(pack) != nullptr)
		return false;

	if (dynamic_cast<const SaveLocalState *>(pack) != nullptr)
		return false;

	if(dynamic_cast<const AdvInterfaceReady *>(pack) != nullptr)
		return false;

	auto query = queries->topQuery(player);
	if (query && query->blocksPack(pack))
	{
		complain(boost::str(boost::format(
			"\r\n| Player \"%s\" has to answer queries before attempting any further actions.\r\n| Top Query: \"%s\"\r\n")
			% boost::to_upper_copy<std::string>(player.toString())
			% query->toString()
		));
		return true;
	}

	return false;
}

void CGameHandler::removeAfterVisit(const ObjectInstanceID & id)
{
	//If the object is being visited, there must be a matching query
	for (const auto &query : queries->allQueries())
	{
		auto * someVisitQuery = queries->queryAs<MapObjectVisitQuery>(query);

		if(!someVisitQuery)
			continue;

		if(someVisitQuery->visitedObject == id)
		{
			someVisitQuery->removeObjectAfterVisit = true;
			return;
		}
	}

	//If we haven't returned so far, there is no query and no visit, call was wrong
	throw std::runtime_error("This function needs to be called during the object visit!");
}

void CGameHandler::changeFogOfWar(int3 center, ui32 radius, PlayerColor player, ETileVisibility mode)
{
	FowTilesType tiles;

	if (mode == ETileVisibility::HIDDEN)
	{
		gameInfo().getTilesInRange(tiles, center, radius, ETileVisibility::REVEALED, player);
	}
	else
	{
		gameInfo().getTilesInRange(tiles, center, radius, ETileVisibility::HIDDEN, player);
	}
	changeFogOfWar(tiles, player, mode);
}

void CGameHandler::changeFogOfWar(const FowTilesType &tiles, PlayerColor player, ETileVisibility mode)
{
	if (tiles.empty())
		return;

	FoWChange fow;
	fow.tiles = tiles;
	fow.player = player;
	fow.mode = mode;

	if (mode == ETileVisibility::HIDDEN)
	{
		// do not hide tiles observed by owned objects. May lead to disastrous AI problems
		FowTilesType observedTiles;
		const auto * p = gameInfo().getPlayerState(player);
		for (const auto * obj : p->getOwnedObjects())
			gameInfo().getTilesInRange(observedTiles, obj->getSightCenter(), obj->getSightRadius(), ETileVisibility::REVEALED, obj->getOwner());

		for (auto tile : observedTiles)
			vstd::erase_if_present (fow.tiles, tile);
	}

	if (!fow.tiles.empty())
		sendAndApply(fow);
}

const CGHeroInstance * CGameHandler::getVisitingHero(const CGObjectInstance *obj)
{
	assert(obj);

	for(const auto & query : queries->allQueries())
	{
		const auto * visit = dynamic_cast<VisitQuery *>(query.get());
		if(!visit)
			continue;

		if(visit->visitedObject == obj->id)
			return gameInfo().getHero(visit->visitingHero);
	}
	return nullptr;
}

const CGObjectInstance * CGameHandler::getVisitingObject(const CGHeroInstance *hero)
{
	assert(hero);

	for(const auto & query : queries->allQueries())
	{
		const auto * visit = dynamic_cast<VisitQuery *>(query.get());
		if(!visit)
			continue;

		if(visit->visitingHero == hero->id)
			return gameInfo().getObjInstance(visit->visitedObject);
	}
	return nullptr;
}

bool CGameHandler::isVisitCoveredByAnotherQuery(const CGObjectInstance *obj, const CGHeroInstance *hero)
{
	assert(obj);
	assert(hero);
	assert(getVisitingHero(obj) == hero);
	// Check top query of targeted player:
	// If top query is NOT visit to targeted object then we assume that
	// visitation query is covered by other query that must be answered first

	if(const auto & topQuery = queries->topQuery(hero->getOwner()))
		if(const auto * visit =  dynamic_cast<VisitQuery *>(topQuery.get()))
			return !(visit->visitedObject == obj->id && visit->visitingHero == hero->id);

	return true;
}

void CGameHandler::setObjPropertyValue(ObjectInstanceID objid, ObjProperty prop, int32_t value)
{
	SetObjectProperty sob;
	sob.id = objid;
	sob.what = prop;
	sob.identifier = NumericID(value);
	sendAndApply(sob);
}

void CGameHandler::setObjPropertyID(ObjectInstanceID objid, ObjProperty prop, ObjPropertyID identifier)
{
	SetObjectProperty sob;
	sob.id = objid;
	sob.what = prop;
	sob.identifier = identifier;
	sendAndApply(sob);
}

void CGameHandler::setRewardableObjectConfiguration(ObjectInstanceID objid, const Rewardable::Configuration & configuration)
{
	SetRewardableConfiguration srb;
	srb.objectID = objid;
	srb.configuration = configuration;
	sendAndApply(srb);
}

void CGameHandler::setRewardableObjectConfiguration(ObjectInstanceID townInstanceID, BuildingID buildingID, const Rewardable::Configuration & configuration)
{
	SetRewardableConfiguration srb;
	srb.objectID = townInstanceID;
	srb.buildingID = buildingID;
	srb.configuration = configuration;
	sendAndApply(srb);
}

void CGameHandler::showInfoDialog(InfoWindow * iw)
{
	sendAndApply(*iw);
}

vstd::RNG & CGameHandler::getRandomGenerator()
{
	return randomizer->getDefault();
}

std::shared_ptr<CGObjectInstance> CGameHandler::createNewObject(const int3 & visitablePosition, MapObjectID objectID, MapObjectSubID subID)
{
	TerrainId terrainType = ETerrainId::NONE;

	if (!gameState().isInTheMap(visitablePosition))
		throw std::runtime_error("Attempt to create object outside map at " + visitablePosition.toString());

	const TerrainTile & t = gameState().getMap().getTile(visitablePosition);
	terrainType = t.getTerrainID();

	auto handler = LIBRARY->objtypeh->getHandlerFor(objectID, subID);

	auto o = handler->create(&gameInfo(), nullptr);
	handler->configureObject(o.get(), *randomizer);
	assert(o->ID == objectID);
	gs->getMap().generateUniqueInstanceName(o.get());

	assert(!handler->getTemplates(terrainType).empty());
	if (handler->getTemplates().empty())
		throw std::runtime_error("Attempt to create object (" + std::to_string(objectID) + ", " + std::to_string(subID.getNum()) + ") with no templates!");

	if (!handler->getTemplates(terrainType).empty())
		o->appearance = handler->getTemplates(terrainType).front();
	else
		o->appearance = handler->getTemplates().front();

	if (o->isVisitable())
		o->setAnchorPos(visitablePosition + o->getVisitableOffset());
	else
		o->setAnchorPos(visitablePosition);

	return o;
}

void CGameHandler::createWanderingMonster(const int3 & visitablePosition, CreatureID creature, int unitSize)
{
	auto createdObject = createNewObject(visitablePosition, Obj::MONSTER, creature);

	auto cre = std::dynamic_pointer_cast<CGCreature>(createdObject);
	assert(cre);
	cre->notGrowingTeam = cre->neverFlees = false;
	cre->initialCharacter = CGCreature::Character::AGGRESSIVE;
	cre->gainedArtifact = ArtifactID::NONE;
	cre->temppower = static_cast<int64_t>(unitSize) * 1000;
	cre->addToSlot(SlotID(0), std::make_unique<CStackInstance>(&gameInfo(), creature, unitSize));

	newObject(createdObject, PlayerColor::NEUTRAL);
}

void CGameHandler::createBoat(const int3 & visitablePosition, BoatId type, PlayerColor initiator)
{
	auto createdObject = createNewObject(visitablePosition, Obj::BOAT, type);
	newObject(createdObject, initiator);
}

void CGameHandler::createHole(const int3 & visitablePosition, PlayerColor initiator)
{
	auto createdObject = createNewObject(visitablePosition, Obj::HOLE, 0);
	newObject(createdObject, initiator);
}

void CGameHandler::newObject(std::shared_ptr<CGObjectInstance> object, PlayerColor initiator)
{
	object->initObj(*randomizer);

	NewObject no;
	no.newObject = object;
	no.initiator = initiator;
	sendAndApply(no);
}

void CGameHandler::startBattle(const CArmedInstance *army1, const CArmedInstance *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, const BattleLayout & layout, const CGTownInstance *town)
{
	battles->startBattle(army1, army2, tile, hero1, hero2, layout, town);
}

void CGameHandler::startBattle(const CArmedInstance *army1, const CArmedInstance *army2 )
{
	battles->startBattle(army1, army2);
}

void CGameHandler::useChargeBasedSpell(const ObjectInstanceID & heroObjectID, const SpellID & spellID)
{
	auto completeCast = prepareChargeBasedSpellCompletion(heroObjectID, spellID);
	if(completeCast)
		completeCast();
}

std::function<void()> CGameHandler::prepareChargeBasedSpellCompletion(const ObjectInstanceID & heroObjectID, const SpellID & spellID)
{
	const auto * hero = gameInfo().getHero(heroObjectID);
	assert(hero);
	assert(hero->canCastThisSpell(spellID.toSpell()));
	const bool arcaneMemoryActive = newHorizonsMagic::rulesActive(hero->getMagicRules())
		&& hero->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.arcaneMemory");

	// Preserve permanent spell sources ahead of scrolls. A reusable spell scroll
	// is a valid learning source even though it has no charge cost.
	std::optional<std::pair<ArtifactInstanceID, uint16_t>> chargedArtifact;
	std::optional<std::pair<ArtifactInstanceID, SpellID>> chargedScrollSource;
	std::optional<std::pair<ArtifactInstanceID, SpellID>> reusableScrollSource;
	bool hasInvalidChargedScroll = false;
	for(const auto & source : hero->getSourcesForSpell(spellID))
	{
		if(const auto * artInst = hero->getArtByInstanceId(source.as<ArtifactInstanceID>()))
		{
			const auto * artType = artInst->getType();
			const auto spellCost = artType->getChargeCost(spellID);
			if(artInst->getTypeId() == ArtifactID::SPELL_SCROLL)
			{
				const auto scrollSpell = artInst->getScrollSpellID();
				if(scrollSpell != spellID)
				{
					hasInvalidChargedScroll = true;
					continue;
				}

				if(!spellCost.has_value())
				{
					if(!reusableScrollSource)
						reusableScrollSource.emplace(artInst->getId(), scrollSpell);
					continue;
				}

				if(spellCost.value() <= artInst->getCharges()
					&& artType->getDischargeCondition() == DischargeArtifactCondition::SPELLCAST)
				{
					chargedArtifact.emplace(artInst->getId(), spellCost.value());
					chargedScrollSource.emplace(artInst->getId(), scrollSpell);
				}
				else
				{
					hasInvalidChargedScroll = true;
				}
				continue;
			}

			if(spellCost.has_value() && spellCost.value() <= artInst->getCharges() && artType->getDischargeCondition() == DischargeArtifactCondition::SPELLCAST)
			{
				chargedArtifact.emplace(artInst->getId(), spellCost.value());
				chargedScrollSource.reset();
			}
			else
			{
				return {};
			}
		}
		else
		{
			return {};
		}
	}

	const bool selectedReusableScroll = reusableScrollSource.has_value();
	if(!selectedReusableScroll && hasInvalidChargedScroll)
		return {};

	std::optional<ArtifactInstanceID> selectedArtifactId;
	std::optional<uint16_t> selectedChargeCost;
	std::optional<std::pair<ArtifactInstanceID, SpellID>> selectedScrollSource;
	if(selectedReusableScroll)
	{
		selectedArtifactId = reusableScrollSource->first;
		selectedScrollSource = reusableScrollSource;
	}
	else
	{
		assert(chargedArtifact.has_value());
		if(!chargedArtifact)
			return {};
		selectedArtifactId = chargedArtifact->first;
		selectedChargeCost = chargedArtifact->second;
		selectedScrollSource = chargedScrollSource;
	}

	if(selectedReusableScroll && !arcaneMemoryActive)
		return {};

	return [this, heroObjectID, spellID, selectedArtifactId, selectedChargeCost, arcaneMemoryActive, selectedScrollSource]()
	{
		const auto * currentHero = gameInfo().getHero(heroObjectID);
		if(!currentHero)
			return;

		// Resolve this exact source again; an effect may have moved or removed it.
		const auto * selectedArtifact = currentHero->getArtByInstanceId(*selectedArtifactId);
		if(!selectedArtifact)
			return;
		if(selectedScrollSource
			&& (selectedScrollSource->second != spellID
				|| selectedArtifact->getTypeId() != ArtifactID::SPELL_SCROLL
				|| selectedArtifact->getScrollSpellID() != spellID))
			return;

		bool sourceSettled = false;
		if(selectedChargeCost)
		{
			const auto * artifactType = selectedArtifact->getType();
			const auto currentChargeCost = artifactType->getChargeCost(spellID);
			if(!currentChargeCost || *currentChargeCost != *selectedChargeCost
				|| *selectedChargeCost > selectedArtifact->getCharges()
				|| artifactType->getDischargeCondition() != DischargeArtifactCondition::SPELLCAST)
				return;

			DischargeArtifact message(*selectedArtifactId, *selectedChargeCost);
			message.artLoc.emplace(heroObjectID, currentHero->getArtPos(selectedArtifact));
			sendAndApply(message);
			sourceSettled = true;
		}
		else if(selectedScrollSource && !selectedArtifact->getType()->getChargeCost(spellID))
		{
			// Ordinary scrolls are reusable sources in this design; do not invent
			// a depletion rule just to trigger Arcane Memory.
			sourceSettled = true;
		}

		if(!sourceSettled || !arcaneMemoryActive || !selectedScrollSource)
			return;

		const auto * learner = gameInfo().getHero(heroObjectID);
		if(learner && newHorizonsMagic::rulesActive(learner->getMagicRules())
			&& learner->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.arcaneMemory"))
		{
			const auto * spellToLearn = selectedScrollSource->second.toSpell();
			if(spellToLearn && learner->canLearnSpell(spellToLearn))
			{
				std::optional<BattleID> battleID;
				if(learner->battle)
					battleID = learner->battle->getBattleID();
				MetaString line = MetaString::createFromRawString("%s learns ");
				line.replaceTextID(learner->getNameTextID());
				line.appendName(selectedScrollSource->second);
				line.appendRawString(" from a scroll using Arcane Memory.");

				changeSpells(learner, true, std::set<SpellID>{selectedScrollSource->second});

				if(battleID)
				{
					BattleLogMessage message;
					message.battleID = *battleID;
					message.lines.push_back(std::move(line));
					sendAndApply(message);
				}
			}
		}
	};
}
