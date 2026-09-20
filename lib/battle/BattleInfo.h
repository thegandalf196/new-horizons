/*
 * BattleInfo.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "SideInBattle.h"
#include "SiegeInfo.h"

#include "../callback/GameCallbackHolder.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/CBonusSystemNode.h"
#include "../int3.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSorcery.h"

class CStack;
class CStackInstance;
class CStackBasicDescriptor;
class BattleField;
struct BattleLayout;

class DLL_LINKAGE BattleInfo : public CBonusSystemNode, public CBattleInfoCallback, public IBattleState, public GameCallbackHolder
{
	BattleSideArray<SideInBattle> sides; //sides[0] - attacker, sides[1] - defender
	std::unique_ptr<BattleLayout> layout;
	si32 round;
	si32 activationSerial = 0;
	JsonNode heroCommandRules;
	JsonNode magicRules;
	newHorizonsCreatures::CreatureCategoryRules creatureCategoryRules;

	void postDeserialize();
	void expireSeparatedHeroOrderProtect();
public:
	const JsonNode & getHeroCommandRules() const override { return heroCommandRules; }
	const JsonNode & getMagicRules() const override { return magicRules; }
	const newHorizonsCreatures::CreatureCategoryRules & getCreatureCategoryRules() const override { return creatureCategoryRules; }
	bool getHeroCommandUsed(BattleSide side) const override { return sides.at(side).heroCommandUsed; }
	int32_t getBloodrageDamagePercent(BattleSide side) const override { return sides.at(side).bloodrageDamagePercent; }
	int32_t getBloodrageRank(BattleSide side) const override { return sides.at(side).bloodrageRank; }
	SylvanLuckState getSylvanLuckState(BattleSide side) const override { return sides.at(side).sylvanLuck; }
	LuckRollRules getLuckRollRules() const override { return luckRollRules; }
	HeroCommand getActiveDoctrine(BattleSide side) const override { (void)side; return HeroCommand::NONE; }
	HeroCommand getActiveOrder(BattleSide side) const override
	{
		const auto command = sides.at(side).activeOrder;
		return heroCommands::isActive(command) ? command : HeroCommand::NONE;
	}
	std::optional<HeroOrderState> getHeroOrderState(BattleSide side) const override { return sides.at(side).orderState; }
	std::optional<FocusFireState> getFocusFireState(BattleSide side) const override { return sides.at(side).focusFire; }
	/// Drop decode-only legacy Doctrine state and its battle-long bonuses.
	/// Round Order bonuses are intentionally preserved.
	void normalizeLegacyHeroCommandState();
	void validateFocusFireStates() const;
	BattleID battleID = BattleID(0);

	si32 activeStack;
	ObjectInstanceID townID; //used during town siege, nullptr if this is not a siege (note that fortless town IS also a siege)
	int3 tile; //for background and bonuses
	bool replayAllowed;
	std::vector<std::unique_ptr<CStack>> stacks;
	std::vector<std::shared_ptr<CObstacleInstance> > obstacles;
	SiegeInfo si;

	BattleField battlefieldType; //like !!BA:B
	TerrainId terrainType; //used for some stack nativity checks (not the bonus limiters though that have their own copy)
	// When a real Hero Action leaves every surviving stack in Time Stop, the
	// normal creature queue still has to drain before the caster gets another
	// Hero Action.  Keep the caster side in the authoritative battle snapshot so
	// a round boundary (and a save/load in the middle of that boundary) cannot
	// accidentally hand control to the side whose marker merely happens to be
	// encountered last in the queue.
	BattleSide pendingTimeStopHeroActionSide = BattleSide::NONE;
	// A second side may cast Time Stop before the first origin reaches its next
	// Hero Action. Keep both origins pending instead of collapsing them into the
	// scalar compatibility field above.
	ui8 pendingTimeStopHeroActionSides = 0;

	BattleSide tacticsSide; //which side is requested to play tactics phase
	ui8 tacticDistance; //how many hexes we can go forward (1 = only hexes adjacent to margin line)
	// Keeping this at the end avoids shifting preceding offsets, but every facade
	// and consumer still requires a synchronized rebuild when BattleInfo changes.
	std::set<uint32_t> bloodrageDestroyedUnits;
	LuckRollRules luckRollRules;

	template <typename Handler> void serialize(Handler &h)
	{
		if(h.saving)
		{
			heroCommands::validateRules(heroCommandRules);
			validateFocusFireStates();
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_LUCK)
				&& (sides[BattleSide::ATTACKER].sylvanLuck != SylvanLuckState{}
					|| sides[BattleSide::DEFENDER].sylvanLuck != SylvanLuckState{}))
				throw std::runtime_error("Cannot discard Sylvan Luck battle state");
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS)
				&& heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE))
				throw std::runtime_error("Cannot discard New Horizons targeted combat rules");
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_BLOODRAGE)
				&& (sides[BattleSide::ATTACKER].bloodrageDamagePercent != 0
					|| sides[BattleSide::DEFENDER].bloodrageDamagePercent != 0
					|| sides[BattleSide::ATTACKER].bloodrageRank != 0
					|| sides[BattleSide::DEFENDER].bloodrageRank != 0
					|| !bloodrageDestroyedUnits.empty()))
				throw std::runtime_error("Cannot discard Bloodrage battle state");
		}
		h & battleID;
		h & sides;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_LUCK))
			h & luckRollRules;
		else if(!h.saving)
			luckRollRules = {};
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_BLOODRAGE))
		{
			h & sides[BattleSide::ATTACKER].bloodrageDamagePercent;
			h & sides[BattleSide::DEFENDER].bloodrageDamagePercent;
			h & sides[BattleSide::ATTACKER].bloodrageRank;
			h & sides[BattleSide::DEFENDER].bloodrageRank;
			h & bloodrageDestroyedUnits;
			if(!h.saving && (sides[BattleSide::ATTACKER].bloodrageDamagePercent < 0
				|| sides[BattleSide::ATTACKER].bloodrageDamagePercent > 60
				|| sides[BattleSide::DEFENDER].bloodrageDamagePercent < 0
				|| sides[BattleSide::DEFENDER].bloodrageDamagePercent > 60
				|| sides[BattleSide::ATTACKER].bloodrageRank < 0
				|| sides[BattleSide::ATTACKER].bloodrageRank > 3
				|| sides[BattleSide::DEFENDER].bloodrageRank < 0
				|| sides[BattleSide::DEFENDER].bloodrageRank > 3))
				throw std::runtime_error("Invalid saved Bloodrage battle state");
		}
		else if(!h.saving)
		{
			sides[BattleSide::ATTACKER].bloodrageDamagePercent = 0;
			sides[BattleSide::DEFENDER].bloodrageDamagePercent = 0;
			sides[BattleSide::ATTACKER].bloodrageRank = 0;
			sides[BattleSide::DEFENDER].bloodrageRank = 0;
			bloodrageDestroyedUnits.clear();
		}
		h & round;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_FIRE_WALL))
			h & activationSerial;
		else if(!h.saving)
			activationSerial = 0;
		h & activeStack;
		h & townID;
		h & tile;
		h & stacks;
		h & obstacles;
		h & si;
		h & battlefieldType;
		h & terrainType;
		h & tacticsSide;
		h & tacticDistance;
		if(h.saving && (pendingTimeStopHeroActionSide != BattleSide::NONE || pendingTimeStopHeroActionSides != 0)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_TIME_STOP))
			throw std::runtime_error("Cannot discard pending Time Stop Hero Action state");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_TIME_STOP_ORIGINS)
			&& (pendingTimeStopHeroActionSides & 3u) == 3u)
			throw std::runtime_error("Cannot discard simultaneous Time Stop origin state");
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_TIME_STOP))
			h & pendingTimeStopHeroActionSide;
		else if(!h.saving)
			pendingTimeStopHeroActionSide = BattleSide::NONE;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_TIME_STOP_ORIGINS))
			h & pendingTimeStopHeroActionSides;
		else if(!h.saving)
			pendingTimeStopHeroActionSides = 0;
		h & static_cast<CBonusSystemNode&>(*this);
		h & replayAllowed;
		if(h.hasFeature(Handler::Version::HERO_COMMANDS))
		{
			h & heroCommandRules;
			if(!h.saving)
			{
				heroCommands::validateRules(heroCommandRules);
				if(!h.hasFeature(Handler::Version::NEW_HORIZONS_TARGETED_COMMANDS)
					&& heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE))
					throw std::runtime_error("Targeted battle rules require the new save format");
			}
		}
		else if(!h.saving)
		{
			heroCommandRules = JsonNode();
		}

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MAGIC))
		{
			h & magicRules;
			if(!h.saving)
				newHorizonsMagic::validateRules(magicRules);
		}
		else if(!h.saving)
			magicRules = JsonNode();

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CREATURE_CATEGORIES))
		{
			if(h.saving)
				h & creatureCategoryRules;
			else
			{
				newHorizonsCreatures::CreatureCategoryRules candidate;
				h & candidate;
				newHorizonsCreatures::validateCreatureCategoryEntities(candidate);
				creatureCategoryRules = std::move(candidate);
			}
		}
		else if(!h.saving)
			creatureCategoryRules = newHorizonsCreatures::CreatureCategoryRules();

		if(!h.saving)
		{
			// Reject null/ambiguous unit references before postDeserialize dereferences
			// units and resolves their army bindings. Validation does not need those bindings.
			normalizeLegacyHeroCommandState();
			validateFocusFireStates();
			postDeserialize();
		}
	}

	//////////////////////////////////////////////////////////////////////////
	BattleInfo(IGameInfoCallback *cb, const BattleLayout & layout);
	BattleInfo(IGameInfoCallback *cb);
	virtual ~BattleInfo();
	
	const scripting::Pool & getScriptContextPool() const override;
	const IBattleInfo * getBattle() const override;
	std::optional<PlayerColor> getPlayerID() const override;

	//////////////////////////////////////////////////////////////////////////
	// IBattleInfo

	BattleID getBattleID() const override;

	int32_t getActiveStackID() const override;

	TStacks getStacksIf(const TStackFilter & predicate) const override;

	battle::Units getUnitsIf(const battle::UnitFilter & predicate) const override;

	BattleField getBattlefieldType() const override;
	TerrainId getTerrainType() const override;

	ObstacleCList getAllObstacles() const override;

	PlayerColor getSidePlayer(BattleSide side) const override;
	const CArmedInstance * getSideArmy(BattleSide side) const override;
	const CGHeroInstance * getSideHero(BattleSide side) const override;

	ui8 getTacticDist() const override;
	BattleSide getTacticsSide() const override;
	int32_t getRound() const override;
	int32_t getActivationSerial() const override { return activationSerial; }

	const CGTownInstance * getDefendedTown() const override;
	EWallState getWallState(EWallPart partOfWall) const override;
	EGateState getGateState() const override;

	int32_t getCastSpells(BattleSide side) const override;
	int32_t getEnchanterCounter(BattleSide side) const override;
	bool getTemporalFieldUsed(BattleSide side) const override;
	bool getCounterspellArmed(BattleSide side) const override;
	int32_t getMetamagicPendingCount(BattleSide side) const override;
	int32_t getMetamagicUsesConsumed(BattleSide side) const override;
	bool getMetamagicGrandUsed(BattleSide side) const override;
	bool getMetamagicFormulaReserveUsed(BattleSide side) const override;
	bool getMetamagicCountersequenceArmed(BattleSide side) const override;
	SpellID getMetamagicFirstSpell(BattleSide side) const override;
	uint32_t getMetamagicFirstTargetUnitId(BattleSide side) const override;
	const std::vector<SpellID> & getMetamagicSequenceSpells(BattleSide side) const override;
	bool getMetamagicFirstCounterspellNegated(BattleSide side) const override;

	const IBonusBearer * getBonusBearer() const override;

	uint32_t nextUnitId() const override;

	int64_t getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const override;

	int3 getLocation() const override;
	BattleLayout getLayout() const override;

	std::vector<SpellID> getUsedSpells(BattleSide side) const override;

	//////////////////////////////////////////////////////////////////////////
	// IBattleState

	void nextRound() override;
	void nextTurn(uint32_t unitId, BattleUnitTurnReason reason) override;
	/// Remove Time Stop state created by the given hero side.  This is called at
	/// the beginning of that side's next Hero Action, not at a round boundary.
	void expireTimeStops(BattleSide casterSide);
	BattleSide getPendingTimeStopHeroActionSide() const;
	ui8 getPendingTimeStopHeroActionSides() const;
	bool hasPendingTimeStopHeroAction(BattleSide side) const;
	void notePendingTimeStopHeroAction(BattleSide side);
	void clearPendingTimeStopHeroAction(BattleSide side);

	void addUnit(uint32_t id, const JsonNode & data) override;
	void moveUnit(uint32_t id, const BattleHex & destination) override;
	void updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta) override;
	void removeUnit(uint32_t id) override;

	void addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;
	void updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;
	void removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus) override;

	void setWallState(EWallPart partOfWall, EWallState state) override;

	void addObstacle(const ObstacleChanges & changes) override;
	void updateObstacle(const ObstacleChanges& changes) override;
	void removeObstacle(uint32_t id) override;
	void setHeroOrderState(BattleSide side, const std::optional<HeroOrderState> & state) override;

	static void addOrUpdateUnitBonus(CStack * sta, const Bonus & value, bool forceAdd);

	/// Server-side lifecycle updates for transient canonical Order triggers.  They
	/// operate on the authoritative battle snapshot; presentation packets may
	/// mirror the resulting state through the normal battle snapshot path.
	bool consumeHeroOrderUnit(BattleSide side, uint32_t unitId);
	bool triggerHeroOrderBrace(BattleSide side, uint32_t unitId);
	bool breakHeroOrderHold(uint32_t unitId);
	bool interceptHeroOrderProtect(BattleSide side);
	bool recordHeroOrderFlankSide(BattleSide side, uint32_t targetUnitId, uint8_t sideBit);
	bool setHeroOrderSecondWindActive(BattleSide side, bool active);
	void recordBloodrageStackDeath(uint32_t unitId);
	void clearBloodrageStackDeath(uint32_t unitId);

	//////////////////////////////////////////////////////////////////////////
	CStack * getStack(int stackID, bool onlyAlive = true);
	using CBattleInfoEssentials::battleGetArmyObject;
	CArmedInstance * battleGetArmyObject(BattleSide side) const;
	using CBattleInfoEssentials::battleGetFightingHero;
	CGHeroInstance * battleGetFightingHero(BattleSide side) const;

	void generateNewStack(uint32_t id, const CStackInstance & base, BattleSide side, const SlotID & slot, const BattleHex & position);
	void generateNewStack(uint32_t id, const CStackBasicDescriptor & base, BattleSide side, const SlotID & slot, const BattleHex & position);

	const SideInBattle & getSide(BattleSide side) const;
	SideInBattle & getSide(BattleSide side);

	const CGHeroInstance * getHero(const PlayerColor & player) const; //returns fighting hero that belongs to given player

	void localInit();
	static std::unique_ptr<BattleInfo> setupBattle(IGameInfoCallback *cb, const int3 & tile, TerrainId, const BattleField & battlefieldType, BattleSideArray<const CArmedInstance *> armies, BattleSideArray<const CGHeroInstance *> heroes, const BattleLayout & layout, const CGTownInstance * town);

	BattleSide whatSide(const PlayerColor & player) const;
};


class DLL_LINKAGE CMP_stack
{
	int phase; //rules of which phase will be used
	int turn;
	BattleSide side;
public:
	bool operator()(const battle::Unit * a, const battle::Unit * b) const;
	CMP_stack(int Phase = 1, int Turn = 0, BattleSide Side = BattleSide::ATTACKER);
};
