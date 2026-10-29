/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include <memory>
#include <cstdint>
#include <optional>
#include "../../lib/battle/NewHorizonsAerialSupremacy.h"
#include "AttackPreviewTargets.h"
class CBattleInfoCallback;
class HypotheticBattle;
class AttackPossibility;
class DamageCache;
struct FortuneStrikeProjection;
struct BattleAttackInfo;
namespace newHorizonsAerialSupremacy { struct Excursion; }
namespace battle { class Unit; }
namespace aerialRetaliationAI
{
bool active(const CBattleInfoCallback & battle, const battle::Unit * actor);
bool firstStrike(const battle::Unit & actor);
attackPreview::Targets targets(const CBattleInfoCallback & battle, const battle::Unit * actor,
	const battle::Unit * requested, const battle::Unit * resolved,
	const newHorizonsAerialSupremacy::Excursion & excursion);
void validateContext(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	BattleHex from, const std::optional<newHorizonsAerialSupremacy::Excursion> & aerial);
/// One bounded native admission; projects a counter at its authoritative origin.
void project(AttackPossibility & outer, const std::shared_ptr<HypotheticBattle> & branch,
	uint32_t shooter, uint32_t griffin, DamageCache & cache);
/// Before hits. Returns false for an admitted but Gaze-cancelled counter.
bool replayAdmission(HypotheticBattle & branch, const FortuneStrikeProjection & strike);
/// Native order: physical source reflection, then live-source Vampirism, then caller's Hex.
/// Returns source health lost to reflection for the existing residual accounting.
int64_t replaySourceAftermath(HypotheticBattle & branch, const FortuneStrikeProjection & strike);
}
