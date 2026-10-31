/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>
#include <string_view>
namespace battle { class Unit; }
class JsonNode;
class CGHeroInstance;
class CHero;
class CBattleInfoCallback;
struct BattleAttackInfo;
struct Bonus;
namespace newHorizonsHeroes
{
struct SpecialtyPresentation;
DLL_LINKAGE bool ordinaryCreatureDamageIsStateful(const CBattleInfoCallback & battle);
DLL_LINKAGE int ordinaryCreatureRoundHealing(const CBattleInfoCallback & battle, const battle::Unit & unit);
DLL_LINKAGE void validateOrdinaryCreatureSpecialtiesSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE void validateOrdinaryCreatureExtensionsSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE void validateOrdinaryCreaturePolicyExtensionsSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE void validateOrdinaryRangedSpecialtiesSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE void validateOrdinaryMaintenanceSpecialtiesSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE void validateOrdinaryMachineSpecialtiesSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE void validateOrdinaryCombatSpecialtiesSerialization(const JsonNode & development, bool supported);
DLL_LINKAGE void validateExtendedOrdinarySpecialtiesSerialization(const JsonNode & development, bool supported);
/// Current effective army owner, visible in this real callback perspective.
/// Null includes unknown/unavailable; it is not proof of an absent hero.
DLL_LINKAGE const CGHeroInstance * ordinaryCreatureVisibleOwnerHero(const CBattleInfoCallback & battle, const battle::Unit & actor);
DLL_LINKAGE bool hasOrdinaryCreatureSpecialty(const CGHeroInstance * hero);
DLL_LINKAGE int ordinaryCreatureSpecialtyValue(const CBattleInfoCallback & battle, const battle::Unit & actor,
	std::string_view expectedIdentity);
DLL_LINKAGE std::shared_ptr<Bonus> ordinaryCreatureSpecialtyBonus(const CGHeroInstance & hero);
/// Trusted hero input; callback consumers use the perspective-aware overload.
DLL_LINKAGE std::shared_ptr<Bonus> ordinaryCreatureStrikeBonus(const CGHeroInstance * hero, const BattleAttackInfo & attack);
DLL_LINKAGE std::shared_ptr<Bonus> ordinaryCreatureStrikeBonus(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE int ordinaryCreatureProtectionPercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE int ordinaryCreatureDamageMultiplier(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
DLL_LINKAGE int ordinaryCreatureAttackPercent(const CGHeroInstance * hero, const BattleAttackInfo & attack,
	bool victimHasOtherFriendlyNeighbor);
DLL_LINKAGE int ordinaryCreatureRetaliationPercent(const CGHeroInstance * hero, const BattleAttackInfo & attack);
DLL_LINKAGE bool resolveOrdinaryCreatureSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	SpecialtyPresentation & result);
}
