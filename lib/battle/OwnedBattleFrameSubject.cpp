/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedBattleFrameSubject.h"
#include "OwnedBattleFrameBatch.h"
#include "OwnedBattleFrameRefusals.h"
#include "CBattleInfoCallback.h"
#include "PossiblePlayerBattleAction.h"
#include "CObstacleInstance.h"
#include "CUnitState.h"
#include "BattleLayout.h"
#include "LiveBattleAccessibilityContext.h"
#include "ProjectedBattleAccessibility.h"
#include "../int3.h"
namespace battle
{
namespace
{
class CapturedBattleInfo final : public ownedFrameCapture::RefusingBattleInfo
{
	BattleID battleId;
	int32_t active, serial, round;
	ui8 tactics;
	BattleSide tacticSide;
	BattleField field;
	TerrainId terrain;
	BattleSideArray<PlayerColor> players;
	JsonNode heroRules, magicRules;
	newHorizonsCreatures::CreatureCategoryRules categories;
	BattleSideArray<std::optional<newHorizonsCommand::ResourceState>> commandResources;
	BattleSideArray<newHorizonsElementalAttunement::Profile> elementalProfiles;
	BattleSideArray<uint8_t> elementalMasks;
	std::vector<std::shared_ptr<const OwnedUnitBonusFrame>> frames;
public:
	CapturedBattleInfo(const CBattleInfoCallback & source, const OwnedBattleFrameBatch & batch)
		: battleId(batch.battle()), active(batch.activeUnitId()), serial(batch.activationSerial()),
		round(source.battleGetRound()), tactics(source.battleTacticDist()), tacticSide(source.battleGetTacticsSide()),
		field(source.battleGetBattlefieldType()), terrain(source.battleTerrainType()),
		heroRules(source.getBattle()->getHeroCommandRules()), magicRules(source.getBattle()->getMagicRules()),
		categories(source.getBattle()->getCreatureCategoryRules())
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			players.at(side) = source.getBattle()->getSidePlayer(side);
			// Current family activation is a battlefield fact. Prospective hero
			// policy and the Order budget retain the original observer boundary.
			elementalMasks.at(side) = source.getBattle()->getElementalAttunementMask(side);
			if(source.battleGetMySide() == BattleSide::ALL_KNOWING || source.battleGetMySide() == side)
			{
				commandResources.at(side) = source.getBattle()->getCommandResources(side);
				elementalProfiles.at(side) = source.getBattle()->getElementalAttunementProfile(side);
			}
		}
		for(const auto & occurrence : batch.occurrences()) frames.push_back(occurrence.frame);
	}
	BattleID getBattleID() const override { return battleId; }
	int32_t getActiveStackID() const override { return active; }
	int32_t getActivationSerial() const override { return serial; }
	int32_t getRound() const override { return round; }
	ui8 getTacticDist() const override { return tactics; }
	BattleSide getTacticsSide() const override { return tacticSide; }
	BattleField getBattlefieldType() const override { return field; }
	TerrainId getTerrainType() const override { return terrain; }
	PlayerColor getSidePlayer(BattleSide side) const override { return players.at(side); }
	const JsonNode & getHeroCommandRules() const override { return heroRules; }
	const JsonNode & getMagicRules() const override { return magicRules; }
	const newHorizonsCreatures::CreatureCategoryRules & getCreatureCategoryRules() const override { return categories; }
	const newHorizonsCommand::ResourceState & getCommandResources(BattleSide side) const override
	{
		const auto & captured = commandResources.at(side);
		if(!captured) throw std::logic_error("Owned opposing Command budget was not captured");
		return *captured;
	}
	const newHorizonsElementalAttunement::Profile & getElementalAttunementProfile(BattleSide side) const override
	{
		return elementalProfiles.at(side); // Empty for an unknown opposing hero.
	}
	uint8_t getElementalAttunementMask(BattleSide side) const override { return elementalMasks.at(side); }
	Units getUnitsIf(const UnitFilter & predicate) const override
	{
		Units result;
		for(const auto & frame : frames) if(predicate(&frame->unit())) result.push_back(&frame->unit());
		return result;
	}
	std::optional<std::shared_ptr<CUnitState>> acquire(const Unit & unit) const
	{
		for(const auto & frame : frames)
			if(&unit == &frame->unit())
			{
				auto loan = std::make_shared<OwnedUnitBonusFrame::Scratch>(frame->scratch());
				return std::shared_ptr<CUnitState>(loan, &loan->unit());
			}
		return std::shared_ptr<CUnitState>{}; // Engaged UNKNOWN, never a native fallback.
	}
};
class FrameSceneContext final : public BattleAccessibilityContext
{
	const BattleAccessibilityState & scene;
	const CapturedBattleInfo & battle;
public:
	FrameSceneContext(const BattleAccessibilityState & scene, const CapturedBattleInfo & battle) : scene(scene), battle(battle) {}
	BattleHexArray impassableTiles() const override { return scene.impassable; }
	bool hasWalls() const override { return scene.walls; }
	EGateState gateState() const override { return scene.gate; }
	Units aliveUnits() const override { return battle.getUnitsIf([](const Unit * unit) { return unit->isValidTarget(false); }); }
	std::vector<BattleHexArray> blockedObstacles() const override { return scene.obstacles; }
	EWallState wallState(EWallPart part) const override { return scene.wallStates.at(static_cast<size_t>(part)); }
	std::vector<PendingDemonicGateFootprint> pendingGates(BattleSide side) const override { return scene.reservations.at(side); }
};
class CapturedCallback final : public CBattleInfoCallback
{
	CapturedBattleInfo battle;
	PlayerColor observer;
	BattleSideArray<std::optional<VisibleHeroPolicy>> policies;
	std::shared_ptr<const BattleAccessibilityState> scene;
public:
	CapturedCallback(const CBattleInfoCallback & source, const OwnedBattleFrameBatch & batch,
		std::shared_ptr<const BattleAccessibilityState> scene)
		: battle(source, batch), observer(batch.observer()), scene(std::move(scene))
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER}) policies.at(side) = batch.heroPolicy(side);
	}
	const IBattleInfo * getBattle() const override { return &battle; }
	std::optional<PlayerColor> getPlayerID() const override { return observer; }
	VisibleHeroPolicy battleGetVisibleHeroPolicy(BattleSide side) const override { return policies.at(side).value(); }
	std::optional<const CreatureService *> battleOwnedCreatureService() const override { return static_cast<const CreatureService *>(nullptr); }
	std::optional<std::shared_ptr<CUnitState>> battleAcquireOwnedUnitState(const Unit & unit) const override { return battle.acquire(unit); }
	AccessibilityInfo getAccessibility() const override { return buildBattleAccessibility(FrameSceneContext(*scene, battle)); }
	// Uncaptured service/script/registry capabilities are UNKNOWN, never false.
	int battleGetSurrenderCost(const PlayerColor & Player) const override { throw std::logic_error("Owned callback capability was not captured: battleGetSurrenderCost"); }
	bool battleCanTargetEmptyHex(const battle::Unit * attacker) const override { throw std::logic_error("Owned callback capability was not captured: battleCanTargetEmptyHex"); }
	DamageEstimation calculateDmgRange(const BattleAttackInfo & info) const override { throw std::logic_error("Owned callback capability was not captured: calculateDmgRange"); }
	std::vector<SpellID> getAvailableBeneficialSpells(const battle::Unit * caster, const battle::Unit * target) const override { throw std::logic_error("Owned callback capability was not captured: getAvailableBeneficialSpells"); }
	std::vector<SpellID> getAvailableRandomCreatureSpells(const battle::Unit * caster, const battle::Unit * target) const override { throw std::logic_error("Owned callback capability was not captured: getAvailableRandomCreatureSpells"); }
	std::vector<PossiblePlayerBattleAction> getClientActionsForStack(const CStack * stack, const BattleClientInterfaceData & data) override { throw std::logic_error("Owned callback capability was not captured: getClientActionsForStack"); }
	void configurePassingLines(const battle::Unit * mover, ReachabilityInfo::Parameters & params) const override { throw std::logic_error("Owned callback capability was not captured: configurePassingLines"); }
	int battleGetRetreatPermission(BattleSide side, BonusType permission) const override { throw std::logic_error("Owned callback capability was not captured: battleGetRetreatPermission"); }

};
}
std::shared_ptr<const BattleAccessibilityState> OwnedBattleFrameSubject::captureScene(const CBattleInfoCallback & source, const OwnedBattleFrameBatch & batch)
{
	// The same ordinary callback visibility and canonical accessibility inputs.
	// Do not relax full-authority BattleAccessibilityState::capture admission.
	const LiveBattleAccessibilityContext live(source);
	auto result = std::make_shared<BattleAccessibilityState>();
	result->impassable = live.impassableTiles(); result->walls = live.hasWalls();
	// The ordinary visibility adapter asks battleHasNativeStack(), which queries
	// native bonus caches. Derive that SAME current fact on the completed owned
	// clone instead, then reuse the canonical value visibility predicate.
	const auto side = source.battleGetMySide();
	bool hasNative = false;
	if(side != BattleSide::ALL_KNOWING)
		for(const auto & occurrence : batch.occurrences())
		{
			const auto & unit = occurrence.frame->unit();
			if(!unit.isGhost() && unit.unitSide() == side && unit.isNativeTerrain(source.battleTerrainType()))
			{ hasNative = true; break; }
		}
	for(const auto & obstacle : source.getBattle()->getAllObstacles())
		if(CBattleInfoEssentials::battleIsObstacleVisibleForSide(*obstacle, side, hasNative))
			result->obstacles.push_back(obstacle->getBlockedTiles());
	if(result->walls)
	{
		result->gate = live.gateState();
		for(const auto & part : accessibilityWallParts()) result->wallStates.at(static_cast<size_t>(part.first)) = live.wallState(part.first);
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER}) result->reservations.at(side) = live.pendingGates(side);
	return result;
}
std::shared_ptr<CBattleInfoCallback> OwnedBattleFrameSubject::seal(const CBattleInfoCallback & source, const OwnedBattleFrameBatch & batch)
{
	return std::make_shared<CapturedCallback>(source, batch, batch.scene);
}
}
