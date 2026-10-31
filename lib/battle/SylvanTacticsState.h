/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#pragma once
#include <map>
#include <set>
#include <optional>
#include <vector>
#include <cstdint>
#include <stdexcept>

/// Captured faction policy. Historical Sylvan Luck fields never change meaning.
struct SylvanTacticsState
{
	static constexpr int FLEETFOOT_SPEED = 1;
	static constexpr int REDEPLOYMENT_SPEED = 2;
	static constexpr int HUNT_SPEED = 2;
	static constexpr int CROSSFIRE_PERCENT = 15;
	bool enabled = false;
	bool fleetfoot = false;
	bool patientHunter = false;
	bool dappledCover = false;
	bool measuredAdvance = false;
	bool coveringFire = false;
	bool crossfire = false;
	bool disruptiveStrike = false;
	bool quickRedeployment = false;
	bool perfectMoment = false;
	bool wildHunt = false;
	std::set<uint32_t> activated;
	std::optional<uint32_t> firstActivation;
	std::optional<uint32_t> redeploymentActivation;
	std::set<uint32_t> dappledExpired;
	std::set<uint32_t> covered;
	std::set<uint32_t> disrupted;
	std::map<uint32_t, std::set<uint32_t>> shootersByTarget;
	bool huntTriggered = false;

	bool operator==(const SylvanTacticsState &) const = default;
	void validate() const
	{
		if(!enabled && *this != SylvanTacticsState{})
			throw std::runtime_error("Sylvan Tactics state without captured policy");
		if((!fleetfoot && firstActivation) || (!quickRedeployment && redeploymentActivation)
			|| (!dappledCover && !dappledExpired.empty())
			|| (!coveringFire && !covered.empty()) || (!disruptiveStrike && !disrupted.empty())
			|| (!crossfire && !shootersByTarget.empty()) || (!wildHunt && huntTriggered))
			throw std::runtime_error("Sylvan Tactics history without perk");
		if(firstActivation && !activated.contains(*firstActivation))
			throw std::runtime_error("Sylvan Tactics first activation without history");
		for(const auto & [target, shooters] : shootersByTarget)
			if(shooters.empty()) throw std::runtime_error("Empty Sylvan Tactics shooter history");
	}
	void beginActivation(uint32_t id, bool friendly, bool redeploy = false, bool waited = false)
	{
		firstActivation.reset();
		redeploymentActivation.reset();
		if(enabled && dappledCover && waited) dappledExpired.insert(id);
		covered.erase(id);
		disrupted.erase(id);
		if(enabled && friendly)
		{
			if(activated.insert(id).second && fleetfoot) firstActivation = id;
			if(quickRedeployment && redeploy) redeploymentActivation = id;
		}
	}
	void endActivation()
	{
		firstActivation.reset();
		redeploymentActivation.reset();
	}
	void nextRound()
	{
		huntTriggered = false;
		dappledExpired.clear();
		shootersByTarget.clear();
		endActivation();
	}
	int speedBonus(uint32_t id, int turn = 0) const
	{
		if(!enabled) return 0;
		return (fleetfoot && (!activated.contains(id) || (turn == 0 && firstActivation == id)) ? FLEETFOOT_SPEED : 0)
			+ (turn == 0 && redeploymentActivation == id ? REDEPLOYMENT_SPEED : 0)
			+ (turn == 0 && huntTriggered ? HUNT_SPEED : 0);
	}
	int crossfireBonus(uint32_t shooter, uint32_t target) const
	{
		const auto found = shootersByTarget.find(target);
		if(!enabled || !crossfire || found == shootersByTarget.end()) return 0;
		for(uint32_t previous : found->second) if(previous != shooter) return CROSSFIRE_PERCENT;
		return 0;
	}
	/// Inputs are accepted physical damage against actual hostile ordinary creatures.
	void finishStrike(uint32_t shooter, const std::vector<uint32_t> & rangedTargets,
		const std::vector<uint32_t> & meleeTargets, const std::vector<uint32_t> & adjacentFriends,
		bool enemyKilled)
	{
		if(!enabled) return;
		if(crossfire) for(uint32_t target : rangedTargets) shootersByTarget[target].insert(shooter);
		if(coveringFire && !rangedTargets.empty()) covered.insert(adjacentFriends.begin(), adjacentFriends.end());
		if(disruptiveStrike) disrupted.insert(meleeTargets.begin(), meleeTargets.end());
		if(wildHunt && enemyKilled) huntTriggered = true;
	}
	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validate();
		h & enabled;
		h & fleetfoot;
		h & patientHunter;
		h & dappledCover;
		h & measuredAdvance;
		h & coveringFire;
		h & crossfire;
		h & disruptiveStrike;
		h & quickRedeployment;
		h & perfectMoment;
		h & wildHunt;
		h & activated;
		h & firstActivation;
		h & redeploymentActivation;
		h & dappledExpired;
		h & covered;
		h & disrupted;
		h & shootersByTarget;
		h & huntTriggered;
		if(!h.saving) validate();
	}
};
