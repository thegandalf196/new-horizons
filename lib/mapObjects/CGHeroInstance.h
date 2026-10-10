/*
 * CGHeroInstance.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../entities/hero/NewHorizonsMasteryState.h"
#include "../entities/hero/NewHorizonsPerkState.h"
#include "../entities/hero/SpellPointState.h"
#include "../spells/NewHorizonsMagic.h"

#include <vcmi/spells/Caster.h>

#include "IOwnableObject.h"

#include "army/CArmedInstance.h"
#include "army/CCommanderInstance.h"

#include "../bonuses/BonusCache.h"
#include "../entities/hero/EHeroGender.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

class CHero;
class CGBoat;
class CGTownInstance;
class CMap;
class UpgradeInfo;
class TurnInfo;

struct TerrainTile;
struct TurnInfoCache;

class DLL_LINKAGE CGHeroPlaceholder : public CGObjectInstance
{
public:
	using CGObjectInstance::CGObjectInstance;

	/// if this is placeholder by power, then power rank of desired hero
	std::optional<ui8> powerRank;

	/// if this is placeholder by type, then hero type of desired hero
	std::optional<HeroTypeID> heroType;

	template <typename Handler> void serialize(Handler &h)
	{
		h & static_cast<CGObjectInstance&>(*this);
		h & powerRank;
		h & heroType;
	}
	
protected:
	void serializeJsonOptions(JsonSerializeFormat & handler) override;
};


class DLL_LINKAGE CGHeroInstance : public CArmedInstance, public IBoatGenerator, public CArtifactSet, public spells::Caster, public AFactionMember, public ICreatureUpgrader, public IOwnableObject, public scripting::ApiRawPointer<CGHeroInstance>
{
public:
	// Disambiguate the scripting tag: CGHeroInstance is ApiRawPointer both directly and via CGObjectInstance
	using ScriptingApiName = CGHeroInstance;

private:
	// We serialize heroes into JSON for crossover
	friend class CampaignState;
	friend class CMapLoaderH3M;
	friend class CMapFormatJson;

	PrimarySkillsCache primarySkills;
	MagicSchoolMasteryCache magicSchoolMastery;
	BonusValueCache manaPerKnowledgeCached;
	newHorizonsHeroes::SpellPointState spellPointState;
	bool spellPointsInitialized = false;
	// Derived reconciliation cache; never serialized. Bonus-tree changes invalidate it.
	std::optional<int32_t> spellPointCapacityRevision;
	std::unique_ptr<TurnInfoCache> turnInfoCache;
	std::unique_ptr<CCommanderInstance> commander;

	std::set<SpellID> spells; //known spells (spell IDs)
	ObjectInstanceID visitedTown; //set if hero is visiting town or in the town garrison
	ObjectInstanceID boardedBoat; //set to CGBoat when sailing

	ui32 movement; //remaining movement points
	bool inTownGarrison; // if hero is in town garrison

	IGameInfoCallback * getCallback() const final { return cb; }
	std::pair<int32_t, int32_t> getMoraleLimits() const override;
	TConstBonusListPtr getMoraleBonuses() const override;
	bool isSpellbinderHatGrantEligible(const SpellID & spell) const;
	void refreshCreatureLineSpecialtyBonuses(bool createIfMissing);
	bool canLearnSpellImpl(const spells::Spell * spell, bool allowBanned,
		bool ignoreSchoolProficiency, bool logWarnings, bool allowAdventureGuildUnlock) const;

public:
	//////////////////////////////////////////////////////////////////////////
	//format:   123
	//          8 4
	//          765
	ui8 moveDir;
	bool tacticFormationEnabled;

	//////////////////////////////////////////////////////////////////////////

	TExpType exp; //experience points
	ui32 level; //current level of hero

	/// If not NONE - then hero should use portrait from referenced hero type
	HeroTypeID customPortraitSource;
	std::vector<std::pair<SecondarySkill,ui8> > secSkills; //first - ID of skill, second - level of skill (1 - basic, 2 - adv., 3 - expert); if hero has ability (-1, -1) it meansthat it should have default secondary abilities
	EHeroGender gender;

	std::string nameCustomTextId;
	std::string biographyCustomTextId;

	static constexpr si32 UNINITIALIZED_MANA = -1;
	static constexpr ui32 UNINITIALIZED_MOVEMENT = -1;
	static constexpr auto UNINITIALIZED_EXPERIENCE = std::numeric_limits<TExpType>::max();
	static const ui32 NO_PATROLLING;

	std::set<ObjectInstanceID> visitedObjects;

	struct DLL_LINKAGE Patrol
	{
		bool patrolling{false};
		int3 initialPos;
		ui32 patrolRadius{NO_PATROLLING};
		template <typename Handler> void serialize(Handler &h)
		{
			h & patrolling;
			h & initialPos;
			h & patrolRadius;
		}
	} patrol;

	inline bool isInitialized() const
	{ // has this hero been on the map at least once?
		return movement != UNINITIALIZED_MOVEMENT && spellPointsInitialized;
	}

	/// Buffer-inclusive spendable Spell Points. Buffer is included exactly once.
	int64_t getManaAvailable() const;
	int32_t getNormalSpellPoints() const;
	int32_t getBufferSpellPoints() const;
	bool areSpellPointsInitialized() const { return spellPointsInitialized; }
	/// Initialize or restore an exact saved pool pair. Invalid negative values are rejected.
	void initializeSpellPoints(int32_t normal, int32_t buffer = 0);
	void restoreSpellPointSnapshot(int32_t normal, int32_t buffer);
	void setNormalSpellPoints(int32_t value);
	bool restoreNormalSpellPoints(int32_t amount);
	bool grantBufferSpellPoints(int32_t amount);
	bool removeBufferSpellPoints(int64_t amount);
	bool spendSpellPoints(int64_t amount);
	/// Clamp Normal after capacity-affecting changes; inactive legacy rules stay uncapped.
	void clampSpellPointsToCapacity();

	//int3 getSightCenter() const; //"center" tile from which the sight distance is calculated
	int getSightRadius() const override; //sight distance (should be used if player-owned structure)
	//////////////////////////////////////////////////////////////////////////

	BoatId getBoatType() const override; //0 - evil (if a ship can be evil...?), 1 - good, 2 - neutral
	EPathfindingLayer getBoatLayer() const override;
	void getOutOffsets(std::vector<int3> &offsets) const override; //offsets to obj pos when we boat can be placed
	const IObjectInterface * getObject() const override;

	//////////////////////////////////////////////////////////////////////////

	// no *Translated() accessors: a map may rename this hero, so its name lives in a
	// map overlay that only the rendering side can resolve
	std::string getBiographyTextID() const;

	std::string getNameTextID() const;

	HeroTypeID getPortraitSource() const;
	int32_t getIconIndex() const;

	std::string getClassNameTextID() const;

	bool inBoat() const;
	CGBoat * getBoat();
	const CGBoat * getBoat() const;
	void setBoat(CGBoat * getBoat);

	/// Returns ID of existing war machine that will be replaced if hero were to purchase new war machine
	/// If there is no replaced war machine (for example slot is empty), method will return empty ArtifactID
	ArtifactID getReplacedWarMachine(ArtifactID newWarMachine) const;
	bool hasSpellbook() const;
	int maxSpellLevel() const;
	void addSpellToSpellbook(const SpellID & spell);
	void removeSpellFromSpellbook(const SpellID & spell);
	/// True for durable knowledge, eligible temporary inscriptions, or an active
	/// perk-granted variant. Variant grants still require a physical Spellbook.
	bool isSpellInscribedForCasting(const SpellID & spell) const;
	/// Durable entries plus eligible temporary or perk-granted entries for casting consumers.
	std::set<SpellID> getInscribedSpellsForCasting() const;
	bool spellbookContainsSpell(const SpellID & spell) const;
	std::vector<BonusSourceID> getSourcesForSpell(const SpellID & spell) const;
	void removeSpellbook();
	void removeAllSpells();
	/// Durable spellbook entries plus active perk-granted variants. This is a
	/// derived view; only the underlying `spells` set is serialized.
	std::set<SpellID> getSpellsInSpellbook() const;
	EAlignment getAlignment() const;
	bool needsLastStack()const override;

	ResourceSet dailyIncome() const override;
	/// Shared New Horizons daily-income quote with a prospective Investor snapshot.
	/// The current hero rules still determine whether the Investor amount applies.
	ResourceSet dailyIncomeWithInvestorGold(int32_t investorDailyGold) const;
	std::vector<CreatureID> providedCreatures() const override;
	const IOwnableObject * asOwnable() const final;

	//INativeTerrainProvider
	FactionID getFactionID() const override;
	bool isNativeTerrain(TerrainId terrain) const override;
	/// Whether this hero uses the saved New Horizons adventure-movement rules.
	/// Legacy heroes intentionally retain the ordinary speed/terrain path.
	bool usesNewHorizonsMovement() const;
	/// Identifies the two artifact movement bonuses replaced by paid Adventure
	/// Spell grants in the current saved New Horizons magic rules.
	bool isNewHorizonsAdventureMovementArtifactBonus(const Bonus & bonus) const;
	/// New Horizons movement affinity is granted by the hero's faction (or an
	/// explicit native-terrain bonus) or by an army whose every roster stack is
	/// native to the terrain.  This is separate from legacy battle affinity.
	bool hasNewHorizonsTerrainAffinity(TerrainId terrain,
		const CCreatureSet * projectedArmy = nullptr) const;
	int getLowestCreatureSpeed() const;
	si32 manaRegain() const; //how many points of mana can hero regain "naturally" in one day
	/// Calculate next-day mana, optionally using the Movement limit captured before pool bonus expiry.
	si32 getManaNewTurn(bool completedDay = true, std::optional<int> previousMovementLimit = std::nullopt) const;
	int getCurrentLuck(int stack=-1, bool town=false) const;
	const JsonNode & getMagicRules() const;
	std::vector<SpellSchool> getSpellSchools(const spells::Spell * spell) const;
	int getSpellLevel(const spells::Spell * spell) const;
	int32_t getListedSpellCost(const spells::Spell * sp) const;
	int32_t getSpellCost(const spells::Spell * sp) const; //do not use during battles -> bonuses from army would be ignored

	/// School insufficiency is reported only when it is the sole failed learning gate.
	enum class SpellLearningStatus
	{
		LEARNABLE,
		INSUFFICIENT_SCHOOL,
		UNAVAILABLE
	};

	SpellLearningStatus getSpellLearningStatus(const spells::Spell * spell, bool allowBanned = false) const;
	bool canLearnSpell(const spells::Spell * spell, bool allowBanned = false) const;
	/// Shared acquisition eligibility for an accepted newly received scroll.
	bool canLearnSpellFromAcquiredScroll(SpellID spell) const;
	/// Paid Adventure spells may only be taught by an owned, unlocked Guild being visited.
	bool canLearnAdventureSpellFromGuild(const spells::Spell * spell, const CGTownInstance * town) const;
	bool canCastThisSpell(const spells::Spell * spell) const; //determines if this hero can cast given spell; takes into account existing spell in spellbook, existing spellbook and artifact bonuses

	/// convert given position between map position (CGObjectInstance::pos) and visitable position used for hero interactions
	int3 convertToVisitablePos(const int3 & position) const;
	int3 convertFromVisitablePos(const int3 & position) const;

	// ----- primary and secondary skill, experience, level handling -----

	/// Returns true if hero has lower level than should upon his experience.
	bool gainsLevel() const;

	ui8 getSecSkillLevel(const SecondarySkill & skill) const; //0 - no skill
	int getPrimSkillLevel(PrimarySkill id) const;
	bool usesPrimaryGrowth() const;
	std::optional<newHorizonsHeroes::PrimaryGrowthView> getPrimaryGrowthView() const;
	const JsonNode & getPrimaryGrowthRules() const { return primaryGrowthRules; }
	const JsonNode & getCapabilityRules() const { return capabilityRules; }
	const newHorizonsHeroes::MasteryState & getMasteryState() const { return masteryState; }
	const newHorizonsHeroes::PerkState & getPerkState() const { return perkState; }
	int32_t getNewHorizonsInvestorDailyGold() const { return newHorizonsInvestorDailyGold; }
	void setNewHorizonsInvestorDailyGold(int32_t value)
	{
		static constexpr int32_t goldPerInvestorStep = 50;
		static constexpr int32_t maximumInvestorDailyGold = 250;
		if(value < 0 || value > maximumInvestorDailyGold || value % goldPerInvestorStep != 0)
			throw std::runtime_error("Invalid New Horizons Investor daily Gold snapshot");
		newHorizonsInvestorDailyGold = value;
	}
	using DemonicReserve = std::map<CreatureID, TQuantity>;
	const DemonicReserve & getDemonicReserve() const { return demonicReserve; }
	TQuantity getDemonicReserveCount(CreatureID creature) const
	{
		const auto found = demonicReserve.find(creature);
		return found == demonicReserve.end() ? 0 : found->second;
	}
	void setDemonicReserve(DemonicReserve value) { demonicReserve = std::move(value); }
	bool hasNewHorizonsAdventureSpellCastToday() const { return newHorizonsAdventureSpellState.castToday; }
	void setNewHorizonsAdventureSpellCastToday(bool value) { newHorizonsAdventureSpellState.castToday = value; }
	void resetNewHorizonsAdventureSpellCastToday() { newHorizonsAdventureSpellState.castToday = false; }
	bool hasUsedNewHorizonsCastleGateToday(int32_t day) const { return newHorizonsCastleGateLastUseDay == day; }
	void markNewHorizonsCastleGateUsed(int32_t day) { newHorizonsCastleGateLastUseDay = day; }
	int32_t getTrainingDrillLastWeek() const { return trainingDrillLastWeek; }
	void setTrainingDrillLastWeek(int32_t week)
	{
		if(week < -1)
			throw std::runtime_error("Invalid Reinforcement Drill use week");
		trainingDrillLastWeek = week;
	}
	void validateRecruitmentTrainingSerialization(bool supported) const
	{
		CCreatureSet::validateTrainingSerialization(supported);
		if(trainingDrillLastWeek < -1 || (!supported && trainingDrillLastWeek != -1))
			throw std::runtime_error("Cannot discard Reinforcement Drill use week");
	}
	int32_t getNewHorizonsPursuitMarchLastUseDay() const { return newHorizonsPursuitMarchLastUseDay; }
	bool hasUsedNewHorizonsPursuitMarchToday(int32_t day) const { return newHorizonsPursuitMarchLastUseDay == day; }
	void setNewHorizonsPursuitMarchLastUseDay(int32_t day)
	{
		if(day < -1)
			throw std::runtime_error("Invalid New Horizons Pursuit March use day");
		newHorizonsPursuitMarchLastUseDay = day;
	}
	int32_t getNewHorizonsForcedMarchLastUseDay() const { return newHorizonsForcedMarchLastUseDay; }
	int32_t getNewHorizonsForcedMarchPenaltyDay() const { return newHorizonsForcedMarchPenaltyDay; }
	void setNewHorizonsForcedMarchState(int32_t lastUseDay, int32_t penaltyDay)
	{
		if(!isValidNewHorizonsForcedMarchState(lastUseDay, penaltyDay))
			throw std::runtime_error("Invalid New Horizons Forced March state");
		newHorizonsForcedMarchLastUseDay = lastUseDay;
		newHorizonsForcedMarchPenaltyDay = penaltyDay;
	}
	bool hasUsedNewHorizonsMuster(int32_t week) const { return getNewHorizonsMusterUsesThisWeek(week) > 0; }
	int32_t getNewHorizonsMusterLastWeek() const { return newHorizonsMusterLastWeek; }
	int32_t getNewHorizonsMusterUsesThisWeek(int32_t week) const
	{
		return newHorizonsMusterLastWeek == week ? newHorizonsMusterUsesThisWeek : 0;
	}
	void markNewHorizonsMusterUsed(int32_t week, int32_t usesThisWeek = 1)
	{
		if(week < 0)
		{
			newHorizonsMusterLastWeek = -1;
			newHorizonsMusterUsesThisWeek = 0;
			return;
		}
		newHorizonsMusterLastWeek = week;
		newHorizonsMusterUsesThisWeek = std::clamp<int32_t>(usesThisWeek, 0, 2);
	}
	int32_t getNewHorizonsLearningMentorLastWeek() const { return newHorizonsLearningMentorLastWeek; }
	int32_t getNewHorizonsRecruitersContactsLastWeek() const { return newHorizonsRecruitersContactsLastWeek; }
	void markNewHorizonsRecruitersContactsUsed(int32_t week);
	void validateNewHorizonsRecruitersContactsSerialization(bool supported) const;
	const std::set<ObjectInstanceID> & getNewHorizonsSageGuildVisits() const { return newHorizonsSageGuildVisits; }
	void markNewHorizonsSageGuildVisit(ObjectInstanceID town);
	void validateNewHorizonsSageSerialization(bool supported) const;
	int32_t getNewHorizonsScholarWeek() const { return newHorizonsScholarWeek; }
	const std::vector<ObjectInstanceID> & getNewHorizonsScholarPartners() const { return newHorizonsScholarPartners; }
	static bool isValidNewHorizonsScholarState(ObjectInstanceID hero, int32_t week, const std::vector<ObjectInstanceID> & partners);
	void validateNewHorizonsScholarSerialization(bool supported) const
	{
		if(!isValidNewHorizonsScholarState(id, newHorizonsScholarWeek, newHorizonsScholarPartners))
			throw std::runtime_error("Invalid New Horizons Scholar state");
		if(!supported && newHorizonsScholarWeek != -1)
			throw std::runtime_error("New Horizons Scholar state requires the new save format");
	}
	void markNewHorizonsScholarMeeting(ObjectInstanceID partner, int32_t week);
	bool hasNewHorizonsScholarMeeting(ObjectInstanceID partner, int32_t week) const;
	bool canExchangeNewHorizonsScholarWith(const CGHeroInstance & partner, int32_t week) const;
	std::optional<SpellID> getNewHorizonsScholarSpellFor(const CGHeroInstance & recipient) const;
	bool hasUsedNewHorizonsLearningMentor(int32_t week) const { return newHorizonsLearningMentorLastWeek == week; }
	using LearningMentorRecipients = std::array<ObjectInstanceID, 2>;
	const LearningMentorRecipients & getNewHorizonsLearningMentorRecipients() const { return newHorizonsLearningMentorRecipients; }
	static bool isValidNewHorizonsLearningMentorState(ObjectInstanceID heroId, int32_t week,
		const LearningMentorRecipients & recipients);
	void setNewHorizonsLearningMentorState(int32_t week, const LearningMentorRecipients & recipients);
	void markNewHorizonsLearningMentorUsed(int32_t week)
	{
		setNewHorizonsLearningMentorState(week < 0 ? -1 : week, {ObjectInstanceID::NONE, ObjectInstanceID::NONE});
	}
	/// Shared candidate/quota rule; callers must additionally establish allied ownership.
	bool canGrantNewHorizonsLearningMentorTo(const CGHeroInstance & recipient, int32_t week) const;
	bool hasNewHorizonsLearningMentorUseFor(ObjectInstanceID recipientId, int32_t week) const;
	TExpType getNewHorizonsLearningMentorExperiencePerLevel() const;
	int32_t getNewHorizonsLandSurveyorLastWeek() const { return newHorizonsLandSurveyorLastWeek; }
	bool hasUsedNewHorizonsLandSurveyor(int32_t week) const { return newHorizonsLandSurveyorLastWeek == week; }
	int32_t getNewHorizonsProspectorLastWeek() const { return newHorizonsProspectorLastWeek; }
	bool hasUsedNewHorizonsProspector(int32_t week) const { return newHorizonsProspectorLastWeek == week; }
	ObjectInstanceID getNewHorizonsMagnateLastTown() const { return newHorizonsMagnateLastTown; }
	int32_t getNewHorizonsMagnateVisitWeek() const { return newHorizonsMagnateVisitWeek; }
	void recordNewHorizonsMagnateTownVisit(ObjectInstanceID town, int32_t week)
	{
		if(town.getNum() < 0 || week < 0)
			throw std::runtime_error("Invalid New Horizons Magnate town visit");
		newHorizonsMagnateLastTown = town;
		newHorizonsMagnateVisitWeek = week;
	}
	void validateNewHorizonsMagnateSerialization(bool supported) const
	{
		if((newHorizonsMagnateLastTown == ObjectInstanceID::NONE) != (newHorizonsMagnateVisitWeek == -1)
			|| newHorizonsMagnateVisitWeek < -1 || newHorizonsMagnateLastTown.getNum() < -1)
			throw std::runtime_error("Invalid New Horizons Magnate visit receipt");
		if(!supported && newHorizonsMagnateLastTown != ObjectInstanceID::NONE)
			throw std::runtime_error("Cannot discard New Horizons Magnate visit receipt");
	}
	int32_t getNewHorizonsPeacemakerLastWeek() const { return newHorizonsPeacemakerLastWeek; }
	ObjectInstanceID getNewHorizonsPacifiedCreatureId() const { return newHorizonsPacifiedCreatureId; }
	bool hasUsedNewHorizonsPeacemaker(int32_t week) const { return week >= 0 && newHorizonsPeacemakerLastWeek == week; }
	bool isNewHorizonsCreaturePacified(ObjectInstanceID creatureId, int32_t week) const
	{
		return week >= 0 && newHorizonsPeacemakerLastWeek == week
			&& newHorizonsPacifiedCreatureId != ObjectInstanceID::NONE
			&& newHorizonsPacifiedCreatureId == creatureId
			&& hasActivePerk("new-horizons:diplomacy", "new-horizons:diplomacy.peacemaker");
	}
	int32_t getNewHorizonsTributeLastWeek() const { return newHorizonsTributeLastWeek; }
	bool hasUsedNewHorizonsTribute(int32_t week) const { return week >= 0 && newHorizonsTributeLastWeek == week; }
	int32_t getNewHorizonsRecruitmentPactExpiryDay() const { return newHorizonsRecruitmentPactExpiryDay; }
	bool hasNewHorizonsRecruitmentPact(int32_t currentDay) const
	{
		return currentDay >= 0 && newHorizonsRecruitmentPactExpiryDay >= 0
			&& currentDay <= newHorizonsRecruitmentPactExpiryDay
			&& hasActivePerk("new-horizons:diplomacy", "new-horizons:diplomacy.recruitmentPact");
	}
	void setNewHorizonsDiplomacyState(int32_t peacemakerLastWeek, ObjectInstanceID pacifiedCreatureId,
		int32_t tributeLastWeek, int32_t pactExpiryDay = -1)
	{
		if(!isValidNewHorizonsDiplomacyState(peacemakerLastWeek, pacifiedCreatureId, tributeLastWeek, pactExpiryDay))
			throw std::runtime_error("Invalid New Horizons Diplomacy weekly state");
		newHorizonsPeacemakerLastWeek = peacemakerLastWeek;
		newHorizonsPacifiedCreatureId = pacifiedCreatureId;
		newHorizonsTributeLastWeek = tributeLastWeek;
		newHorizonsRecruitmentPactExpiryDay = pactExpiryDay;
	}
	int getPerkSkillRank(const std::string & skillId) const;
	bool hasActivePerk(const std::string & skillId, const std::string & perkId) const;
	/// New Horizons Necromancy is a separate saved-rules path.  Legacy heroes
	/// retaining core:necromancy continue through calculateNecromancy().
	bool usesNewHorizonsNecromancy() const;
	int getNewHorizonsNecromancyRank() const;
	/// Active percentage points from a visited Necromancy Amplifier under captured New Horizons rules.
	int32_t getNewHorizonsNecromancyAmplifierBonusPercent() const;
	void applyPerkSelection(const newHorizonsHeroes::PerkSelection & selection);
	std::optional<newHorizonsHeroes::MasteryView> getMasteryView() const;
	void captureMasteryEligibility(uint32_t nextLevel);
	void captureMasteryEligibility(uint32_t nextLevel, bool artilleryExpertBeforeGain, bool logisticsExpertBeforeGain = false);
	std::optional<newHorizonsHeroes::MasteryOffer> prepareMasteryOffer() const;
	void applyMasteryOffer(const newHorizonsHeroes::MasteryOffer & offer);
	void applyMasteryChoice(uint64_t sequence, int choice);
	void refreshMasteryBonuses();
	std::optional<newHorizonsHeroes::LeadershipCapacity> getLeadershipCapacity() const;
	std::optional<newHorizonsHeroes::SiegeCapabilities> getSiegeCapabilities() const;
	/// Read-only projection for AI army exchanges; does not attach or transfer units.
	std::optional<newHorizonsHeroes::LeadershipCapacity> getLeadershipCapacity(const CCreatureSet & army) const;
	std::optional<newHorizonsHeroes::LeadershipSlotCapacity> getLeadershipSlotCapacity(CreatureID creature) const;
	bool isPrimaryRatingNode() const override { return usesPrimaryGrowth(); }

	/// Returns true if hero has free secondary skill slot.
	bool canLearnSkill() const;
	bool canLearnSkill(const SecondarySkill & which) const;

	void setExperience(si64 value, ChangeValueMode mode);
	void setPrimarySkill(PrimarySkill primarySkill, si64 value, ChangeValueMode mode);
	void setSecSkillLevel(const SecondarySkill & which, int val, ChangeValueMode mode); // abs == 0 - changes by value; 1 - sets to value
	void levelUp(const std::array<int, GameConstants::PRIMARY_SKILLS> & gains = {});

	void setMovementPoints(int points);
	int movementPointsRemaining() const;
	int movementPointsLimit() const;
	//cached version is much faster, TurnInfo construction is costly
	int movementPointsLimitCached(const EPathfindingLayer & layer, const TurnInfo * ti) const;

	int movementPointsAfterEmbark(int MPsBefore, int basicCost, bool disembark, const TurnInfo * ti) const;

	std::unique_ptr<TurnInfo> getTurnInfo(int days, const CCreatureSet * projectedArmy = nullptr) const;

	double getFightingStrength() const; // takes attack / defense skill into account
	double getMagicStrength() const; // takes knowledge / spell power skill but also current mana, whether the hero owns a spell-book and whether that books contains anything into account
	double getHeroStrength() const; // includes fighting and magic strength

	/// Returns true if 'left' hero is stronger than 'right' when considering campaign transfer priority
	static bool compareCampaignValue(const CGHeroInstance * left, const CGHeroInstance * right);
	uint64_t getValueForDiplomacy() const;
	/// Army strength as seen by neutral creatures and Thieves Guild - may be scaled by artifacts such as Diplomat's Cloak
	uint64_t getArmyStrengthPerceivedByOthers() const;

	ui64 getTotalStrength() const; // includes fighting strength and army strength
	TExpType calculateXp(TExpType exp) const; //apply learning skill
	TExpType calculateXp(TExpType exp, int32_t additionalPercent) const;
	int getBasePrimarySkillValue(PrimarySkill which) const; //the value of a base-skill without items or temporary bonuses

	CStackBasicDescriptor calculateNecromancy (const BattleResult &battleResult) const;
	EDiggingStatus diggingStatus() const;

	//////////////////////////////////////////////////////////////////////////

	const CHeroClass * getHeroClass() const;
	HeroClassID getHeroClassID() const;

	const CHero * getHeroType() const;
	HeroTypeID getHeroTypeID() const;
	/// Uses the saved New Horizons creature-line specialty rules only when this
	/// instance carries their persistent conversion markers.
	std::string getSpecialtyDescriptionTranslated() const;
	/// Returns the saved New Horizons damage-specialty component bonus only
	/// when this hero has the matching persistent local conversion marker.
	int getDamageSpellSpecialtyBonusPercent(SpellID spell) const;
	/// Returns the saved New Horizons non-damage spell-specialty component bonus
	/// only when this hero has the matching persistent local conversion marker.
	int getNonDamageSpellSpecialtyBonusPercent(SpellID spell) const;
	/// Returns the saved New Horizons core Skill-specialty bonus only when this
	/// hero has the matching persistent local conversion marker.
	int getSkillSpecialtyCoreBonusPercent(SecondarySkill skill) const;
	void setHeroType(HeroTypeID type);

	bool isGarrisoned() const;
	const CGTownInstance * getVisitedTown() const;
	CGTownInstance * getVisitedTown();
	void setVisitedTown(const CGTownInstance * town, bool garrisoned);

	const CCommanderInstance * getCommander() const;
	CCommanderInstance * getCommander();

	void initObj(IGameRandomizer & gameRandomizer) override;
	void initHero(IGameRandomizer & gameRandomizer, bool isFake = false);
	void initHero(IGameRandomizer & gameRandomizer, const HeroTypeID & SUBID, bool isFake = false);

	ArtPlacementMap putArtifact(const ArtifactPosition & pos, const CArtifactInstance * art) override;
	void removeArtifact(const ArtifactPosition & pos) override;
	void initExp(vstd::RNG & rand);
	void initArmy(vstd::RNG & rand, IArmyDescriptor *dst = nullptr);
	void pushPrimSkill(PrimarySkill which, int val);
	ui8 maxlevelsToMagicSchool() const;
	ui8 maxlevelsToWisdom() const;
	void recreateSecondarySkillsBonuses();
	void updateSkillBonus(const SecondarySkill & which, int val);

	void fillUpgradeInfo(UpgradeInfo & info, const CStackInstance &stack) const override;

	bool hasVisions(const CGObjectInstance * target, BonusSubtypeID masteryLevel) const;
	/// If this hero perishes, the scenario is failed
	bool isMissionCritical() const;

	CGHeroInstance(IGameInfoCallback *cb);
	virtual ~CGHeroInstance();

	PlayerColor getOwner() const override;

	///ArtBearer
	ArtBearer bearerType() const override;

	///IBonusBearer
	CBonusSystemNode & whereShouldBeAttached(CGameState & gs) override;
	std::string nodeName() const override;
	si32 manaLimit() const override;

	///IConstBonusProvider
	const IBonusBearer* getBonusBearer() const override;

	///spells::Caster
	int32_t getCasterUnitId() const override;
	int32_t getSpellSchoolLevel(const spells::Spell * spell, SpellSchool * outSelectedSchool = nullptr) const override;
	int64_t getSpellBonus(const spells::Spell * spell, int64_t base, const battle::Unit * affectedStack) const override;
	int32_t getElementalSpellDamageBonus(SpellDamageElement element) const override;
	int64_t getSpecificSpellBonus(const spells::Spell * spell, int64_t base) const override;

	int32_t getEffectLevel(const spells::Spell * spell) const override;
	int32_t getEffectPower(const spells::Spell * spell) const override;
	int32_t getEffectPowerDivisor(const spells::Spell * spell) const override;
	int32_t getEnchantPower(const spells::Spell * spell) const override;
	int64_t getEffectValue(const spells::Spell * spell) const override;
	int64_t getEffectRange(const spells::Spell * spell) const override;

	PlayerColor getCasterOwner() const override;
	const CGHeroInstance * getHeroCaster() const override;

	std::string getCasterNameTextID() const override;
	void getCastDescription(const spells::Spell * spell, const battle::Units & attacked, MetaString & text) const override;
	void spendMana(ServerCallback * server, const int spellCost) const override;

	void updateAppearance();

	void pickRandomObject(IGameRandomizer & gameRandomizer) override;
	void onHeroVisit(IGameEventCallback & gameEvents, const CGHeroInstance * h) const override;
	MetaString getObjectName() const override;
	MetaString getHoverText(PlayerColor player) const override;
	MetaString getMovementPointsTextIfOwner(PlayerColor player) const;

	TObjectTypeHandler getObjectHandler() const override;

	void afterAddToMap(CMap * map) override;
	void afterRemoveFromMap(CMap * map) override;
	void restoreBonusSystem(CGameState & gs) override;

	void updateFrom(const JsonNode & data) override;

	bool isCoastVisitable() const override;
	bool isBlockedVisitable() const override;
	BattleField getBattlefield() const override;

	bool isCampaignYog() const;
	bool isCampaignGem() const;

protected:
	void setPropertyDer(ObjProperty what, ObjPropertyID identifier) override;//synchr
	///common part of hero instance and hero definition
	void serializeCommonOptions(JsonSerializeFormat & handler);

	void serializeJsonOptions(JsonSerializeFormat & handler) override;

private:
	bool capabilityRulesCaptured = false;
	JsonNode capabilityRules;
	bool masteryRulesCaptured = false;
	newHorizonsHeroes::MasteryState masteryState;
	bool perkRulesCaptured = false;
	newHorizonsHeroes::PerkState perkState;
	/// Investor's fixed per-day bonus captured from the owner's treasury at week start.
	int32_t newHorizonsInvestorDailyGold = 0;
	bool primaryGrowthCaptured = false;
	JsonNode primaryGrowthRules;
	newHorizonsMagic::AdventureSpellState newHorizonsAdventureSpellState;
	int32_t newHorizonsCastleGateLastUseDay = -1;
	int32_t newHorizonsPursuitMarchLastUseDay = -1;
	int32_t trainingDrillLastWeek = -1;
	int32_t newHorizonsForcedMarchLastUseDay = -1;
	int32_t newHorizonsForcedMarchPenaltyDay = -1;
	int32_t newHorizonsMusterLastWeek = -1;
	int32_t newHorizonsMusterUsesThisWeek = 0;
	int32_t newHorizonsLearningMentorLastWeek = -1;
	int32_t newHorizonsScholarWeek = -1;
	std::vector<ObjectInstanceID> newHorizonsScholarPartners;
	int32_t newHorizonsRecruitersContactsLastWeek = -1;
	std::set<ObjectInstanceID> newHorizonsSageGuildVisits;
	LearningMentorRecipients newHorizonsLearningMentorRecipients{ObjectInstanceID::NONE, ObjectInstanceID::NONE};
	int32_t newHorizonsLandSurveyorLastWeek = -1;
	int32_t newHorizonsProspectorLastWeek = -1;
	ObjectInstanceID newHorizonsMagnateLastTown = ObjectInstanceID::NONE;
	int32_t newHorizonsMagnateVisitWeek = -1;
	int32_t newHorizonsPeacemakerLastWeek = -1;
	ObjectInstanceID newHorizonsPacifiedCreatureId = ObjectInstanceID::NONE;
	int32_t newHorizonsTributeLastWeek = -1;
	int32_t newHorizonsRecruitmentPactExpiryDay = -1;
	DemonicReserve demonicReserve;
	std::array<int, GameConstants::PRIMARY_SKILLS> lastPrimaryGains{};
	void levelUpAutomatically(IGameRandomizer & gameRandomizer);
	void attachCommanderToArmy();
	bool isNewHorizonsSpellExcluded(const SpellID & spell) const;
	static bool isValidNewHorizonsDiplomacyState(int32_t peacemakerLastWeek,
		ObjectInstanceID pacifiedCreatureId, int32_t tributeLastWeek, int32_t pactExpiryDay = -1)
	{
		return peacemakerLastWeek >= -1 && tributeLastWeek >= -1 && pactExpiryDay >= -1
			&& (pacifiedCreatureId == ObjectInstanceID::NONE
				|| (pacifiedCreatureId.hasValue() && peacemakerLastWeek >= 0));
	}
	static bool isValidNewHorizonsForcedMarchState(int32_t lastUseDay, int32_t penaltyDay)
	{
		return lastUseDay >= -1 && penaltyDay >= -1
			&& (penaltyDay == -1 || (lastUseDay >= 0 && penaltyDay == lastUseDay));
	}

public:
	std::string getHeroTypeName() const;
	void setHeroTypeName(const std::string & identifier);

	void serializeJsonDefinition(JsonSerializeFormat & handler);

	template <typename Handler> void serialize(Handler &h)
	{
		if(h.saving)
			validateRecruitmentTrainingSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_TRAINING));
		if(h.saving)
			newHorizonsHeroes::validateFrailtySpecialtySerialization(primaryGrowthRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_FRAILTY_SPECIALTIES));
		if(h.saving)
			newHorizonsHeroes::validateAenainFrailtySpecialtySerialization(primaryGrowthRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY));
		if(h.saving)
			newHorizonsHeroes::validateDefensiveStartSpecialtySerialization(primaryGrowthRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES));
		if(h.saving)
			newHorizonsHeroes::validateOffensiveStartSpecialtySerialization(primaryGrowthRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES));
		if(h.saving)
			newHorizonsHeroes::validateStartingDevelopmentSerialization(primaryGrowthRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES));
		if(h.saving)
			newHorizonsHeroes::validateRemainingStartSerialization(primaryGrowthRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_REMAINING_START_REPLACEMENTS));
		if(h.saving)
			newHorizonsHeroes::validateCoroniusHolyWrathSerialization(primaryGrowthRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_CORONIUS_HOLY_WRATH));
		if(h.saving)
			newHorizonsHeroes::validateReanimateSpecialtySerialization(primaryGrowthRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_THANT_REANIMATE));
		if(h.saving)
			newHorizonsHeroes::validateHasteSpecialtySerialization(primaryGrowthRules,
				h.hasFeature(Handler::Version::NEW_HORIZONS_HASTE_SPECIALTIES));
		if(h.saving)
			validateNewHorizonsMagnateSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_MAGNATE));
		if(h.saving && (newHorizonsProspectorLastWeek < -1
			|| (!h.hasFeature(Handler::Version::NEW_HORIZONS_PROSPECTOR) && newHorizonsProspectorLastWeek != -1)))
			throw std::runtime_error("Invalid or unsupported New Horizons Prospector receipt");
		if(h.saving)
		{
			validateNewHorizonsScholarSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_LEARNING_SCHOLAR));
			validateNewHorizonsRecruitersContactsSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITERS_CONTACTS));
			validateNewHorizonsSageSerialization(h.hasFeature(Handler::Version::NEW_HORIZONS_SAGE_GUILD_VISITS));
			if(!isValidNewHorizonsLearningMentorState(id, newHorizonsLearningMentorLastWeek,
				newHorizonsLearningMentorRecipients))
				throw std::runtime_error("Invalid New Horizons Learning Mentor state");
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_LEARNING_MASTER_TEACHER)
				&& newHorizonsLearningMentorRecipients[0] != ObjectInstanceID::NONE)
				throw std::runtime_error("New Horizons Master Teacher state requires the new save format");
			if(!isValidNewHorizonsDiplomacyState(newHorizonsPeacemakerLastWeek,
				newHorizonsPacifiedCreatureId, newHorizonsTributeLastWeek, newHorizonsRecruitmentPactExpiryDay))
				throw std::runtime_error("Invalid New Horizons Diplomacy weekly state");
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_DIPLOMACY_WEEKLY_STATE)
				&& (newHorizonsPeacemakerLastWeek != -1
					|| newHorizonsPacifiedCreatureId != ObjectInstanceID::NONE
					|| newHorizonsTributeLastWeek != -1))
				throw std::runtime_error("New Horizons Diplomacy weekly state requires the new save format");
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_PACT_STATE)
				&& newHorizonsRecruitmentPactExpiryDay != -1)
				throw std::runtime_error("New Horizons Recruitment Pact state requires the new save format");
			if(!isValidNewHorizonsForcedMarchState(newHorizonsForcedMarchLastUseDay,
				newHorizonsForcedMarchPenaltyDay))
				throw std::runtime_error("Invalid New Horizons Forced March state");
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_FORCED_MARCH)
				&& (newHorizonsForcedMarchLastUseDay != -1 || newHorizonsForcedMarchPenaltyDay != -1))
				throw std::runtime_error("New Horizons Forced March state requires the new save format");
		}
		if(h.saving && (!h.hasFeature(Handler::Version::NEW_HORIZONS_INVESTOR_INCOME)
			&& newHorizonsInvestorDailyGold != 0))
			throw std::runtime_error("New Horizons Investor state requires the new save format");
		if(h.saving)
		{
			setNewHorizonsPursuitMarchLastUseDay(newHorizonsPursuitMarchLastUseDay);
			if(!h.hasFeature(Handler::Version::NEW_HORIZONS_PURSUIT_MARCH)
				&& newHorizonsPursuitMarchLastUseDay != -1)
				throw std::runtime_error("New Horizons Pursuit March use requires the new save format");
		}
		if(h.saving)
			setNewHorizonsInvestorDailyGold(newHorizonsInvestorDailyGold);
		if(!h.saving)
			spellPointCapacityRevision.reset();
		h & static_cast<CArmedInstance&>(*this);
		h & static_cast<CArtifactSet&>(*this);
		h & exp;
		h & level;
		h & nameCustomTextId;
		h & biographyCustomTextId;
		h & customPortraitSource;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SPELL_POINTS))
		{
			int32_t normal = spellPointsInitialized ? spellPointState.getNormal() : 0;
			int32_t buffer = spellPointState.getBuffer();
			h & normal;
			h & buffer;
			h & spellPointsInitialized;
			if(!h.saving)
			{
				if(spellPointsInitialized && !spellPointState.restoreSnapshot(normal, buffer, std::numeric_limits<int32_t>::max()))
					throw std::runtime_error("Invalid negative Spell Point pool in hero state");
				if(!spellPointsInitialized && (normal != 0 || buffer != 0))
					throw std::runtime_error("Uninitialized hero has nonzero Spell Point pools");
				if(!spellPointsInitialized)
					spellPointState.restoreSnapshot(0, 0, std::numeric_limits<int32_t>::max());
			}
		}
		else
		{
			if(h.saving && spellPointState.getBuffer() != 0)
				throw std::runtime_error("Cannot discard Buffer Spell Points in an older save format");
			int32_t legacyMana = h.saving
				? (spellPointsInitialized ? spellPointState.getNormal() : UNINITIALIZED_MANA)
				: UNINITIALIZED_MANA;
			h & legacyMana;
			if(!h.saving)
			{
				if(legacyMana < UNINITIALIZED_MANA)
					throw std::runtime_error("Invalid negative legacy Spell Point pool in hero state");
				spellPointsInitialized = legacyMana != UNINITIALIZED_MANA;
				if(spellPointsInitialized && !spellPointState.restoreSnapshot(legacyMana, 0, std::numeric_limits<int32_t>::max()))
					throw std::runtime_error("Invalid legacy Spell Point pool in hero state");
			}
		}
		h & secSkills;
		h & movement;
		h & gender;
		h & inTownGarrison;
		h & spells;
		h & patrol;
		h & moveDir;
		if (h.hasFeature(Handler::Version::DISABLE_TACTICS))
			h & tacticFormationEnabled;

		h & visitedTown;
		h & boardedBoat;

		h & commander;
		h & visitedObjects;

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_HERO_GROWTH))
		{
			h & primaryGrowthRules;
			h & lastPrimaryGains;
			if(!h.saving)
			{
				newHorizonsHeroes::validateStartingDevelopmentSerialization(primaryGrowthRules,
					h.hasFeature(Handler::Version::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES));
				newHorizonsHeroes::validateCoroniusHolyWrathSerialization(primaryGrowthRules,
					h.hasFeature(Handler::Version::NEW_HORIZONS_CORONIUS_HOLY_WRATH));
				newHorizonsHeroes::validateFrailtySpecialtySerialization(primaryGrowthRules,
					h.hasFeature(Handler::Version::NEW_HORIZONS_FRAILTY_SPECIALTIES));
				newHorizonsHeroes::validateAenainFrailtySpecialtySerialization(primaryGrowthRules,
					h.hasFeature(Handler::Version::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY));
				newHorizonsHeroes::validateDefensiveStartSpecialtySerialization(primaryGrowthRules,
					h.hasFeature(Handler::Version::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES));
				newHorizonsHeroes::validateOffensiveStartSpecialtySerialization(primaryGrowthRules,
					h.hasFeature(Handler::Version::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES));
				newHorizonsHeroes::validateRemainingStartSerialization(primaryGrowthRules,
					h.hasFeature(Handler::Version::NEW_HORIZONS_REMAINING_START_REPLACEMENTS));
				newHorizonsHeroes::validateReanimateSpecialtySerialization(primaryGrowthRules,
					h.hasFeature(Handler::Version::NEW_HORIZONS_THANT_REANIMATE));
				newHorizonsHeroes::validateHasteSpecialtySerialization(primaryGrowthRules,
					h.hasFeature(Handler::Version::NEW_HORIZONS_HASTE_SPECIALTIES));
				newHorizonsHeroes::validateResolvedHeroRules(primaryGrowthRules);
			}
		}
		else if(!h.saving)
		{
			primaryGrowthRules = JsonNode();
			lastPrimaryGains.fill(0);
		}

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CAPABILITIES))
		{
			h & capabilityRules;
			if(!h.saving)
				newHorizonsHeroes::validateResolvedCapabilityRules(capabilityRules);
		}
		else if(!h.saving)
			capabilityRules = JsonNode();

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_ADVENTURE_MAGIC))
			h & newHorizonsAdventureSpellState;
		else if(!h.saving)
			newHorizonsAdventureSpellState = newHorizonsMagic::AdventureSpellState();

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CASTLE_GATE))
			h & newHorizonsCastleGateLastUseDay;
		else if(h.saving && newHorizonsCastleGateLastUseDay != -1)
			throw std::runtime_error("New Horizons Castle Gate state requires the new save format");
		else if(!h.saving)
			newHorizonsCastleGateLastUseDay = -1;

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MUSTER))
			h & newHorizonsMusterLastWeek;
		else if(h.saving && newHorizonsMusterLastWeek != -1)
			throw std::runtime_error("New Horizons Muster state requires the new save format");
		else if(!h.saving)
			newHorizonsMusterLastWeek = -1;

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MUSTER_PERKS))
			h & newHorizonsMusterUsesThisWeek;
		else if(h.saving && newHorizonsMusterUsesThisWeek != 0)
			throw std::runtime_error("New Horizons Muster perk state requires the new save format");
		else if(!h.saving)
			// Saves made by the rank-only vertical slice had only a boolean
			// weekly marker. Treat an authored marker as one use when loading
			// those saves; this cannot create a second use retroactively.
			newHorizonsMusterUsesThisWeek = newHorizonsMusterLastWeek == -1 ? 0 : 1;

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_LEARNING_MENTOR))
			h & newHorizonsLearningMentorLastWeek;
		else if(h.saving && newHorizonsLearningMentorLastWeek != -1)
			throw std::runtime_error("New Horizons Learning Mentor state requires the new save format");
		else if(!h.saving)
			newHorizonsLearningMentorLastWeek = -1;

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_LAND_SURVEYOR))
			h & newHorizonsLandSurveyorLastWeek;
		else if(h.saving && newHorizonsLandSurveyorLastWeek != -1)
			throw std::runtime_error("New Horizons Land Surveyor state requires the new save format");
		else if(!h.saving)
			newHorizonsLandSurveyorLastWeek = -1;

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_PROSPECTOR))
		{
			h & newHorizonsProspectorLastWeek;
			if(newHorizonsProspectorLastWeek < -1)
				throw std::runtime_error("Invalid New Horizons Prospector receipt");
		}
		else if(!h.saving)
			newHorizonsProspectorLastWeek = -1;

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MAGNATE))
		{
			h & newHorizonsMagnateLastTown;
			h & newHorizonsMagnateVisitWeek;
			validateNewHorizonsMagnateSerialization(true);
		}
		else if(!h.saving)
		{
			newHorizonsMagnateLastTown = ObjectInstanceID::NONE;
			newHorizonsMagnateVisitWeek = -1;
		}

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_DIPLOMACY_WEEKLY_STATE))
		{
			h & newHorizonsPeacemakerLastWeek;
			h & newHorizonsPacifiedCreatureId;
			h & newHorizonsTributeLastWeek;
		}
		else if(!h.saving)
		{
			newHorizonsPeacemakerLastWeek = -1;
			newHorizonsPacifiedCreatureId = ObjectInstanceID::NONE;
			newHorizonsTributeLastWeek = -1;
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_PACT_STATE))
			h & newHorizonsRecruitmentPactExpiryDay;
		else if(!h.saving)
			newHorizonsRecruitmentPactExpiryDay = -1;
		if(!h.saving && !isValidNewHorizonsDiplomacyState(newHorizonsPeacemakerLastWeek,
			newHorizonsPacifiedCreatureId, newHorizonsTributeLastWeek, newHorizonsRecruitmentPactExpiryDay))
			throw std::runtime_error("Invalid New Horizons Diplomacy state");

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_DEMONIC_RESERVE))
			h & demonicReserve;
		else
		{
			if(h.saving && !demonicReserve.empty())
				throw std::runtime_error("New Horizons Demonic Reserve requires the new save format");
			if(!h.saving)
				demonicReserve.clear();
		}

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_MASTERIES))
			h & masteryState;
		else if(!h.saving)
			masteryState = newHorizonsHeroes::MasteryState();

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_PERKS))
			h & perkState;
		else
		{
			if(h.saving && (newHorizonsHeroes::usesPerkRules(perkState.rules) || !perkState.selected.empty()))
				throw std::runtime_error("New Horizons perk state requires the new save format");
			if(!h.saving)
				perkState = newHorizonsHeroes::PerkState();
		}

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_INVESTOR_INCOME))
		{
			h & newHorizonsInvestorDailyGold;
			if(!h.saving)
				setNewHorizonsInvestorDailyGold(newHorizonsInvestorDailyGold);
		}
		else if(!h.saving)
			newHorizonsInvestorDailyGold = 0;

		// Appended daily use and pending first-combat penalty state.
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_FORCED_MARCH))
		{
			h & newHorizonsForcedMarchLastUseDay;
			h & newHorizonsForcedMarchPenaltyDay;
		}
		else if(!h.saving)
		{
			newHorizonsForcedMarchLastUseDay = -1;
			newHorizonsForcedMarchPenaltyDay = -1;
		}
		if(!h.saving && !isValidNewHorizonsForcedMarchState(newHorizonsForcedMarchLastUseDay,
			newHorizonsForcedMarchPenaltyDay))
			throw std::runtime_error("Invalid New Horizons Forced March state");

		// Append recipient identities without shifting any older hero fields.
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_LEARNING_MASTER_TEACHER))
			h & newHorizonsLearningMentorRecipients;
		else if(!h.saving)
			newHorizonsLearningMentorRecipients = {ObjectInstanceID::NONE, ObjectInstanceID::NONE};
		if(!h.saving && !isValidNewHorizonsLearningMentorState(id, newHorizonsLearningMentorLastWeek,
			newHorizonsLearningMentorRecipients))
			throw std::runtime_error("Invalid New Horizons Learning Mentor state");

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_TRAINING))
			h & trainingDrillLastWeek;
		else if(!h.saving)
			trainingDrillLastWeek = -1;
		if(!h.saving)
			setTrainingDrillLastWeek(trainingDrillLastWeek);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_PURSUIT_MARCH))
			h & newHorizonsPursuitMarchLastUseDay;
		else if(!h.saving)
			newHorizonsPursuitMarchLastUseDay = -1;
		if(!h.saving)
			setNewHorizonsPursuitMarchLastUseDay(newHorizonsPursuitMarchLastUseDay);

		if(h.hasFeature(Handler::Version::NEW_HORIZONS_LEARNING_SCHOLAR))
		{
			h & newHorizonsScholarWeek;
			h & newHorizonsScholarPartners;
		}
		else if(!h.saving)
		{
			newHorizonsScholarWeek = -1;
			newHorizonsScholarPartners.clear();
		}
		if(!h.saving)
			validateNewHorizonsScholarSerialization(true);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITERS_CONTACTS))
			h & newHorizonsRecruitersContactsLastWeek;
		else if(!h.saving)
			newHorizonsRecruitersContactsLastWeek = -1;
		if(!h.saving)
			validateNewHorizonsRecruitersContactsSerialization(true);
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SAGE_GUILD_VISITS))
			h & newHorizonsSageGuildVisits;
		else if(!h.saving)
			newHorizonsSageGuildVisits.clear();
		if(!h.saving)
			validateNewHorizonsSageSerialization(true);

		if(!h.saving)
		{
			masteryRulesCaptured = true;
			perkRulesCaptured = true;
			primaryGrowthCaptured = true; // Includes old saves: absence is legacy, not a new-game request.
			capabilityRulesCaptured = true;
		}
		if(!h.saving && h.loadingGamestate)
			attachCommanderToArmy();
	}
};
