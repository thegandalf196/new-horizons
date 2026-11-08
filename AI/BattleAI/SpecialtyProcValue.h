/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/bonuses/Bonus.h"
#include "AttackAbilityPrefix.h"
#include "../../lib/battle/BattleSide.h"
#include <compare>
#include <map>
#include <memory>
#include <set>
class AttackPossibility;
struct BattleAttackInfo;
class CBattleInfoCallback;
class DamageCache;
class HypotheticBattle;
namespace battle { class Unit; }
namespace newHorizonsAttackControllers { struct Strike; }
namespace specialtyProcAI
{
enum class Kind { POISON, DISPEL, STUN, BLOODLUST };
struct Key
{
	uint32_t target = 0;
	Kind kind = Kind::STUN;
	int potency = 0;
	std::vector<const Bonus *> selectedRecords;
	auto operator<=>(const Key &) const = default;
};
struct Outcome
{
	bool rerollAvailable = false;
	std::set<Key> applied;
	std::set<attackAbilityPrefix::StatusKey> completedStatuses;
	auto operator<=>(const Outcome &) const = default;
};
/// Candidate-local nominal status mass only; no native RNG or live state writes.
class Opportunities
{
	struct Effect
	{
		std::vector<Bonus> incoming;
		std::vector<std::shared_ptr<const Bonus>> selectedRecords;
	};
	struct Ledger
	{
		std::map<Outcome, double> outcomes;
		std::map<Key, Effect> effects;
	};
	std::map<BattleSide, Ledger> ledgers;
	/// Uniform recipients use bounded marginals, not a recipient-subset tree.
	std::map<Key, std::pair<double, Effect>> bloodlust;
	static void transition(Ledger &, const battle::Unit &, int chance, bool harmful, const std::optional<Key> &, const std::optional<attackAbilityPrefix::StatusKey> &);
public:
	static bool requiresPreview(const CBattleInfoCallback &, const BattleAttackInfo &);
	/// Accepted post-primary, before generic reactions, in native attempt order.
	void observe(const std::shared_ptr<HypotheticBattle> &, const BattleAttackInfo &, bool includeStun = true);
	/// Controller spells occupy the explicit boundary between native attempts and Yog.
	void observeStun(const std::shared_ptr<HypotheticBattle> &, const BattleAttackInfo &);
	void observeBloodlust(const std::shared_ptr<HypotheticBattle> &, const newHorizonsAttackControllers::Strike &);
	void observeCounter(const std::shared_ptr<HypotheticBattle> &, const battle::Unit &, const battle::Unit &);
	/// One private value probe after the completed command; surviving recipients once.
	void complete(AttackPossibility &, const std::shared_ptr<CBattleInfoCallback> &, DamageCache &);
};
}
