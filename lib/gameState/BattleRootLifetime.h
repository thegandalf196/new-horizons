/*
 * BattleRootLifetime.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../bonuses/NativeBonusGraphAccess.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class CGameState;
class GameStatePackVisitor;

/// Native per-world ROOT LIFETIME admission, not a graph/query/value lease.
/// Covers only integrated root mutation/retirement owners; the public vector,
/// mutable Bonus aliases, map objects and foreign metadata remain separate.
class DLL_LINKAGE BattleRootLifetime final
{
	friend class CGameState;
	friend class GameStatePackVisitor;
	std::recursive_mutex mutex;
	std::recursive_mutex pinStateMutex;
	std::thread::id pinThread;
	size_t pins = 0;
	size_t mutations = 0;
	bool committing = false; // Protected by pinStateMutex, never a world/graph lease.
	std::atomic<size_t> retirements{0};
	std::atomic<bool> permanentlyRetired{false};

public:
	enum class RetirementMode { TEMPORARY, PERMANENT };
	class DLL_LINKAGE Retirement;
	class DLL_LINKAGE CommitAdmission;
	class DLL_LINKAGE Pin final
	{
		friend class BattleRootLifetime;
		// Release lock BEFORE the last shared world can run its destructor.
		std::shared_ptr<CGameState> world;
		NativeBonusGraphAccess graphAccess; // Before root lock; after world ownership.
		BattleRootLifetime & owner;
		std::unique_lock<std::recursive_mutex> lock;
		const std::thread::id thread = std::this_thread::get_id();
		Pin(std::shared_ptr<CGameState> world, BattleRootLifetime & owner);
	public:
		~Pin();
		bool admitted() const noexcept;
		/// Short, checked no-wait admission; retain through CAS and all exchange tails.
		CommitAdmission tryCommit() const noexcept;
		Pin(const Pin &) = delete;
		Pin & operator=(const Pin &) = delete;
		Pin(Pin &&) = delete;
		Pin & operator=(Pin &&) = delete;
	};

	/// Thread-affine through destruction. Retain the originating Pin as well.
	/// Holds only the state interlock, not a new graph lock or allocation permit.
	class DLL_LINKAGE CommitAdmission final
	{
		friend class Pin;
		std::shared_ptr<CGameState> world; // Destruct AFTER state unlock.
		BattleRootLifetime & owner;
		std::unique_lock<std::recursive_mutex> lock;
		bool accepted = false;
		CommitAdmission(std::shared_ptr<CGameState> world, BattleRootLifetime & owner, std::thread::id thread) noexcept;
	public:
		~CommitAdmission();
		explicit operator bool() const noexcept { return accepted; }
		CommitAdmission(const CommitAdmission &) = delete;
		CommitAdmission & operator=(const CommitAdmission &) = delete;
		CommitAdmission(CommitAdmission &&) = delete;
		CommitAdmission & operator=(CommitAdmission &&) = delete;
	};

	class DLL_LINKAGE Mutation final
	{
		friend class BattleRootLifetime;
		friend class Retirement;
		std::shared_ptr<CGameState> world;
		NativeBonusGraphAccess graphAccess; // Before root lock; after world ownership.
		BattleRootLifetime & owner;
		std::unique_lock<std::recursive_mutex> lock;
		Mutation(std::shared_ptr<CGameState> world, BattleRootLifetime & owner);
	public:
		~Mutation();
		Mutation(const Mutation &) = delete;
		Mutation & operator=(const Mutation &) = delete;
		Mutation(Mutation &&) = delete;
		Mutation & operator=(Mutation &&) = delete;
	};

	class DLL_LINKAGE Retirement final
	{
		friend class BattleRootLifetime;
		std::shared_ptr<CGameState> world;
		BattleRootLifetime & owner;
		Retirement(std::shared_ptr<CGameState> world, BattleRootLifetime & owner, RetirementMode mode);
	public:
		~Retirement();
		Retirement(const Retirement &) = delete;
		Retirement & operator=(const Retirement &) = delete;
		Retirement(Retirement &&) = delete;
		Retirement & operator=(Retirement &&) = delete;
		/// Take only at the final root mutation, AFTER stop/waiter/GUI teardown.
		Mutation mutate();
	};

	/// Retain the REAL world and prevent integrated root retirement. Thread-affine
	/// through destruction. Does not authorize reading/writing any graph payload.
	static std::unique_ptr<Pin> retain(std::shared_ptr<CGameState> world);
	/// Strongly retains a retiring world across CClient/session shared_ptr reset.
	/// Admission never waits for the root pin lock; it may wait for the short
	/// admitted CAS/exchange tail. The shared state interlock closes
	/// NEW pins and rejects same-thread reentrancy
	/// BEFORE teardown. Holds no mutex during GUI cleanup or waiter/worker joins.
	/// Prepared pin holders must NEVER wait for interfaceMutex, observers or joins.
	static Retirement retire(std::shared_ptr<CGameState> world, RetirementMode mode = RetirementMode::TEMPORARY);

private:
	/// Actual member/visitor call already owns the world for the complete scope.
	Mutation mutate();
};
