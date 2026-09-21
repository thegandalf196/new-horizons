/* Unregistered Linux test-player prototype, GPL-2.0-or-later. No production hook. */
#pragma once
#include <chrono>
#include <memory>
#include <functional>
#include <string>

class GameEngine;

namespace quickSpellWidgetTest
{
struct Options
{
	// Build's private EntryPoint overlay must supply the ORIGINAL session start,
	// not reset this clock after asset loading. Content supplies the private lease.
	std::chrono::steady_clock::time_point sessionStart;
	std::string privateDirectory;
	std::string token;
	int slot = -1;
	std::string expectedCanonicalSpell;
};

// NEW test-only startup adapter, not an existing player API. Reads mandatory
// NH_WIDGET_{DIRECTORY,TOKEN,SLOT,SPELL,LEASE_START_UNIX_MS}; never starts a game.
Options readOptionsFromEnvironment();

/// Positive-only prototype. Commands: TOKEN SEQUENCE BIND|REPLACE|VERIFY.
/// No registry mutation, input generation, normal-game startup or quit operation.
class Driver
{
	struct State;
	std::shared_ptr<State> state;
	void startReader();
public:
	struct NativeDispatcherTag {};
	using Dispatcher = std::function<void(const std::function<void()> &)>;
	Driver(GameEngine & engine, const Options & options);
	// Explicit native-only seam: no engine construction or fabricated reference.
	Driver(NativeDispatcherTag, const Options & options, Dispatcher dispatcher);
	std::weak_ptr<void> nativeLifetimeObserver() const;
	void releaseNativeState() noexcept;
	~Driver();
	Driver(const Driver &) = delete;
	Driver & operator=(const Driver &) = delete;
	// GUI-thread early-quit latch: no join, GUI release, or ENGINE access.
	void revoke() noexcept;
	// Cleanup entry: revoke and bounded reader join ONLY, outside interfaceMutex.
	void stop() noexcept;
	// Caller ALREADY holds existing interfaceMutex, AFTER endNetwork returns
	// relocked, BEFORE endGameplay/window clearing. Never acquires a lock itself.
	void releaseGUIState() noexcept;
};
}
