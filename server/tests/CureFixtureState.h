/* License: GNU General Public License v2.0 or later; see license.txt. */
#pragma once

#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace cureFixture
{
inline void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

/// No game pointers, save fields, RNG or action state. Owned by the GUI driver,
/// shared only for lifetime; setup transitions execute on the simulation thread.
class Context
{
	mutable std::mutex mutex;
	bool requested = false;
	bool authenticated = false;
	bool consumed = false;
	bool publicationAttempted = false;
	int battle = -1;
	int target = -1;
	int round = -1;
	std::thread::id simulationThread;

public:
	Context(const char * optIn, const char * bootstrap, const char * mode)
	{
		if(!optIn)
			return;
		require(std::string_view(optIn) == "ordinary44b24", "Invalid NH_CURE_FROZEN_FIXTURE intent");
		require(bootstrap && std::string_view(bootstrap) == "ordinary44b24", "Fixture requires authenticated bootstrap");
		require(mode && (std::string_view(mode) == "same-session" || std::string_view(mode) == "late-session"
			|| std::string_view(mode) == "replaced-owner"), "Fixture requires selected Cure mode");
		requested = true;
	}

	void authenticate(std::string_view digest)
	{
		std::lock_guard lock(mutex);
		if(!requested)
			return;
		if(authenticated || consumed || digest != "44b24ce7605c482609df466550e03e57528b59b5892be1cfc58fb6e71ecf4313")
		{
			consumed = true;
			throw std::runtime_error("Fixture authentication cannot change or rearm");
		}
		authenticated = true;
	}

	bool pendingFor(int battleID, bool humanSide)
	{
		std::lock_guard lock(mutex);
		if(!requested || consumed)
			return false;
		if(simulationThread == std::thread::id{})
			simulationThread = std::this_thread::get_id();
		if(simulationThread != std::this_thread::get_id() || !authenticated
			|| battleID != 0 || (battle != -1 && battle != battleID))
		{
			consumed = true;
			throw std::runtime_error("Fixture requires authenticated first battle on simulation thread");
		}
		battle = battleID;
		if(!humanSide)
			return false;
		consumed = true; // BEFORE validation or publication; failure/restart cannot retry.
		return true;
	}

	void published(int battleID, int unitID, int applicationRound)
	{
		std::lock_guard lock(mutex);
		require(!publicationAttempted, "Fixture receipt cannot be retried");
		publicationAttempted = true;
		require(consumed && battle == battleID && target == -1 && applicationRound == 1,
			"Fixture publication receipt mismatch");
		target = unitID;
		round = applicationRound;
	}

	bool isReceipt(int battleID, int unitID, int applicationRound) const
	{
		std::lock_guard lock(mutex);
		return consumed && battle == battleID && target == unitID && round == applicationRound;
	}

	bool enabled() const
	{
		std::lock_guard lock(mutex);
		return requested;
	}
};
}
