/* Unregistered positive-only actual-widget prototype. GPL-2.0-or-later.
 * No tracked EntryPoint/QuickSpellPanel hook, build registration or GUI launch.
 */
#include "../StdInc.h"
#include "QuickSpellWidgetDriver.h"
#include "QuickSpellWidgetProbe.h"

#include "../GameEngine.h"
#include "../CPlayerInterface.h"
#include "../gui/WindowHandler.h"
#include "../battle/BattleInterface.h"
#include "../battle/BattleWindow.h"
#include "../battle/BattleActionsController.h"
#include "../battle/QuickSpellPanel.h"
#include "../battle/UnitActionPanel.h"
#include "../windows/CSpellWindow.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

#include <atomic>
#include <charconv>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

namespace quickSpellWidgetTest
{
namespace
{
struct Command
{
	std::uint64_t sequence = 0;
	std::string operation;
};

bool parseCommand(const std::string & line, const std::string & token, Command & result)
{
	if(line.size() > 512)
		return false;
	std::istringstream input(line);
	std::string suppliedToken, sequence, extra;
	if(!(input >> suppliedToken >> sequence >> result.operation) || (input >> extra) || suppliedToken != token)
		return false;
	const auto converted = std::from_chars(sequence.data(), sequence.data() + sequence.size(), result.sequence);
	if(converted.ec != std::errc() || converted.ptr != sequence.data() + sequence.size() || result.sequence == 0)
		return false;
	// Registry-changing and synthetic negative operations are NOT implemented.
	return result.operation == "BIND" || result.operation == "REPLACE" || result.operation == "VERIFY";
}

template<typename T>
T * onlyChild(const BattleWindow & window)
{
	T * result = nullptr;
	for(auto * child : window.children)
		if(auto * typed = dynamic_cast<T *>(child))
		{
			if(result)
				return nullptr;
			result = typed;
		}
	return result;
}
}

struct Driver::State : std::enable_shared_from_this<Driver::State>
{
	GameEngine * const engine;
	const bool nativeOnly;
	const Dispatcher post;
	Options options;
	const std::thread::id guiThread = std::this_thread::get_id();
	std::atomic<bool> active{true};
	std::atomic<bool> pending{false};
	std::atomic<bool> readerFinished{true};
	std::jthread reader;
	std::mutex reportMutex;
	int directory = -1;
	int fifo = -1;
	int report = -1;
	std::uint64_t sequence = 0;
	bool replaced = false;
	std::string completion;
	std::shared_ptr<CSpellWindow> book;
	std::shared_ptr<BattleInterface> battle;
	std::shared_ptr<CPlayerBattleCallback> callback;
	ObjectInstanceID heroID;
	JsonNode initialPreferences;
	std::uint64_t initialWrites = 0;
	std::uint64_t initialDispatches = 0;

	State(GameEngine * engine, bool nativeOnly, Dispatcher post, Options options)
		: engine(engine), nativeOnly(nativeOnly), post(std::move(post)), options(std::move(options))
	{
		if(!this->post || nativeOnly != (engine == nullptr))
			throw std::runtime_error("Invalid immutable dispatcher/engine tag");
	}
	~State()
	{
		if(fifo >= 0) ::close(fifo);
		if(report >= 0) ::close(report);
		if(directory >= 0) ::close(directory);
	}

	void record(const std::string & message)
	{
		std::lock_guard lock(reportMutex);
		const auto line = message + "\n";
		if(report < 0 || ::write(report, line.data(), line.size()) != static_cast<ssize_t>(line.size()))
			active = false;
	}

	void reject(const char * reason)
	{
		active = false; // Entire driver fails closed; no later operation is accepted.
		record(std::string("REJECT ") + reason);
	}

	bool leaseValid() const
	{
		const int fd = ::openat(directory, "lease.token", O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
		if(fd < 0) return false;
		struct stat metadata{};
		char bytes[130];
		const bool safe = ::fstat(fd, &metadata) == 0 && S_ISREG(metadata.st_mode)
			&& metadata.st_uid == ::getuid() && (metadata.st_mode & 077) == 0 && metadata.st_size <= 129;
		const auto length = safe ? ::read(fd, bytes, sizeof(bytes)) : -1;
		::close(fd);
		return length >= 0 && std::string(bytes, static_cast<std::size_t>(length)) == options.token + "\n";
	}

	bool inspectionTime() const
	{
		const auto elapsed = std::chrono::steady_clock::now() - options.sessionStart;
		return elapsed >= std::chrono::steady_clock::duration::zero() && elapsed < std::chrono::seconds(180);
	}

	void openFiles()
	{
		if(options.privateDirectory.empty() || options.privateDirectory.front() != '/' || options.token.empty() || options.token.size() > 128 || options.token.find_first_of(" \t\r\n") != std::string::npos
			|| options.slot < 0 || options.slot >= QuickSpellPanel::QUICKSPELL_SLOTS
			|| options.expectedCanonicalSpell.find(':') == std::string::npos || !inspectionTime())
			throw std::runtime_error("Invalid test-driver options/deadline");
		directory = ::open(options.privateDirectory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
		struct stat metadata{};
		if(directory < 0 || ::fstat(directory, &metadata) != 0 || !S_ISDIR(metadata.st_mode)
			|| metadata.st_uid != ::getuid() || (metadata.st_mode & 077) != 0 || !leaseValid())
			throw std::runtime_error("Missing private test directory/lease");
		// RDWR avoids an EOF/HUP busy loop while the external writer is absent.
		fifo = ::openat(directory, "control.fifo", O_RDWR | O_NONBLOCK | O_NOFOLLOW);
		if(fifo < 0 || ::fstat(fifo, &metadata) != 0 || !S_ISFIFO(metadata.st_mode)
			|| metadata.st_uid != ::getuid() || (metadata.st_mode & 077) != 0)
			throw std::runtime_error("Unsafe test control FIFO");
		report = ::openat(directory, "driver.log", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_APPEND, 0600);
		if(report < 0) throw std::runtime_error("Refuse existing/unwritable driver report");
	}

	void execute(const Command & command)
	{
		// dispatchMainThread already runs under nonrecursive interfaceMutex.
		// NEVER acquire it again here. No callback-registry transition is allowed.
		if(std::this_thread::get_id() != guiThread) { reject("wrong-thread"); return; }
		if(!active || !leaseValid() || !inspectionTime()) { reject("lease-or-deadline"); return; }
		if(command.sequence != sequence + 1) { reject("sequence"); return; }
		sequence = command.sequence;
		try
		{
			// Real active/lease/deadline guards above run even in native-only mode.
			// No GUI/global access is legal without an actual engine.
			if(!engine) throw std::runtime_error("native-no-engine");
			const auto current = CPlayerInterface::battleInt;
			if(!current || !current->curInt || !current->curInt->cb || !current->windowObject)
				throw std::runtime_error("missing-current-context");
			const auto currentCallback = current->getBattle();
			const auto * hero = currentCallback && currentCallback->getBattle() ? currentCallback->battleGetMyHero() : nullptr;
			if(!hero || !current->actionsController || current->actionsController->heroSpellcastingModeActive())
				throw std::runtime_error("missing-hero-or-casting");
			if(command.operation == "BIND")
			{
				if(book) throw std::runtime_error("already-bound");
				book = engine->windows().topWindow<CSpellWindow>();
				if(!book) throw std::runtime_error("no-topmost-book");
				battle = current;
				callback = currentCallback;
				heroID = hero->id;
				initialPreferences = persistentStorage["quickSpell"];
				if(!initialPreferences.isNull() && !initialPreferences.isStruct())
					throw std::runtime_error("invalid-preference-shape");
				initialWrites = quickSpellWidgetProbe::preferenceWrites.load();
				initialDispatches = quickSpellWidgetProbe::castDispatches.load();
				if(persistentStorage["quickSpell"][std::to_string(options.slot)].String() == options.expectedCanonicalSpell)
					throw std::runtime_error("preference-already-equal");
				completion = "BOUND same-live-context";
				return;
			}
			if(!book || current != battle || currentCallback != callback || hero->id != heroID)
				throw std::runtime_error("changed-context");
			if(command.operation == "REPLACE")
			{
				if(replaced || engine->windows().topWindow<CSpellWindow>() != book)
					throw std::runtime_error("replacement-phase-or-book");
				auto & window = *current->windowObject;
				const auto oldPanel = reinterpret_cast<std::uintptr_t>(onlyChild<QuickSpellPanel>(window));
				const auto oldActions = reinterpret_cast<std::uintptr_t>(onlyChild<UnitActionPanel>(window));
				if(!oldPanel || !oldActions) throw std::runtime_error("ambiguous-panels");
				window.updateQueue(); // REAL replacement while the SAME book stays open.
				const auto newPanel = reinterpret_cast<std::uintptr_t>(onlyChild<QuickSpellPanel>(window));
				const auto newActions = reinterpret_cast<std::uintptr_t>(onlyChild<UnitActionPanel>(window));
				if(!newPanel || !newActions || newPanel == oldPanel || newActions == oldActions
					|| engine->windows().topWindow<CSpellWindow>() != book || current->getBattle() != callback
					|| callback->battleGetMyHero()->id != heroID)
					throw std::runtime_error("replacement-invariant");
				replaced = true;
				completion = "REPLACED panel=" + std::to_string(oldPanel) + ":" + std::to_string(newPanel)
					+ " action=" + std::to_string(oldActions) + ":" + std::to_string(newActions);
				return;
			}
			if(!replaced) throw std::runtime_error("selection-before-replacement");
			for(const auto & openBook : engine->windows().findWindows<CSpellWindow>())
				if(openBook == book) throw std::runtime_error("selection-not-finished");
			auto expectedPreferences = initialPreferences;
			expectedPreferences[std::to_string(options.slot)].String() = options.expectedCanonicalSpell;
			if(persistentStorage["quickSpell"] != expectedPreferences
				|| quickSpellWidgetProbe::preferenceWrites.load() != initialWrites + 1
				|| quickSpellWidgetProbe::castDispatches.load() != initialDispatches)
				throw std::runtime_error("storage-or-dispatch-oracle");
			completion = "POSITIVE_WIDGET_RESULT canonical-write-once no-cast-dispatch";
			active = false;
		}
		catch(const std::exception & error)
		{
			reject(error.what()); // Test driver failure, NOT a product exception fallback.
		}
	}

	void readCommands(std::stop_token stop)
	{
		std::string buffered;
		while(!stop.stop_requested() && active)
		{
			if(!inspectionTime()) { reject("reader-deadline"); break; }
			pollfd watched{fifo, POLLIN, 0};
			const auto ready = ::poll(&watched, 1, 100);
			if(ready < 0 && errno == EINTR) continue;
			if(ready < 0 || (watched.revents & (POLLERR | POLLNVAL))) { reject("reader-poll"); break; }
			if(!ready) continue;
			char bytes[513];
			const auto length = ::read(fifo, bytes, sizeof(bytes));
			if(length < 0 && (errno == EAGAIN || errno == EINTR)) continue;
			if(length <= 0) { reject("reader-read"); break; }
			buffered.append(bytes, static_cast<std::size_t>(length));
			if(buffered.size() > 512) { reject("reader-size"); break; }
			const auto end = buffered.find('\n');
			if(end == std::string::npos) continue;
			Command command;
			if(end != buffered.size() - 1 || !parseCommand(buffered.substr(0, end), options.token, command)
				|| pending.exchange(true)) { reject("reader-command-or-overlap"); break; }
			buffered.clear();
			const std::weak_ptr<State> weak = shared_from_this();
			post([weak, command]()
			{
				const auto state = weak.lock();
				if(!state || !state->active) return;
				state->execute(command);
				state->pending = false;
				if(!state->completion.empty())
				{
					state->record(state->completion);
					state->completion.clear();
				}
			});
		}
	}
};

Options readOptionsFromEnvironment()
{
	const auto required = [](const char * name)
	{
		const char * value = std::getenv(name);
		if(!value || !*value || std::char_traits<char>::length(value) > 4096)
			throw std::runtime_error("Missing/oversized test-only environment option");
		return std::string(value);
	};
	Options options;
	options.privateDirectory = required("NH_WIDGET_DIRECTORY");
	options.token = required("NH_WIDGET_TOKEN");
	options.expectedCanonicalSpell = required("NH_WIDGET_SPELL");
	const auto slot = required("NH_WIDGET_SLOT");
	const auto parsedSlot = std::from_chars(slot.data(), slot.data() + slot.size(), options.slot);
	const auto start = required("NH_WIDGET_LEASE_START_UNIX_MS");
	std::int64_t unixStart = 0;
	const auto parsedStart = std::from_chars(start.data(), start.data() + start.size(), unixStart);
	if(parsedSlot.ec != std::errc() || parsedSlot.ptr != slot.data() + slot.size()
		|| parsedStart.ec != std::errc() || parsedStart.ptr != start.data() + start.size() || unixStart <= 0)
		throw std::runtime_error("Invalid test-only slot/start");
	const auto unixNow = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	if(unixStart > unixNow || unixNow - unixStart >= 100000)
		throw std::runtime_error("Test-driver attachment missed setup deadline");
	options.sessionStart = std::chrono::steady_clock::now() - std::chrono::milliseconds(unixNow - unixStart);
	return options;
}

Driver::Driver(GameEngine & engine, const Options & options)
	: state(std::make_shared<State>(&engine, false,
		[&engine](const std::function<void()> & task) { engine.dispatchMainThread(task); }, options))
{
	startReader();
}

Driver::Driver(NativeDispatcherTag, const Options & options, Dispatcher dispatcher)
	: state(std::make_shared<State>(nullptr, true, std::move(dispatcher), options))
{
	startReader();
}

void Driver::startReader()
{
	state->openFiles();
	state->readerFinished = false;
	state->reader = std::jthread([weak = std::weak_ptr<State>(state)](std::stop_token stop)
	{
		if(const auto locked = weak.lock())
		{
			try { locked->readCommands(stop); }
			catch(const std::exception &) { locked->reject("reader-exception"); }
			locked->readerFinished = true;
		}
	});
}

void Driver::revoke() noexcept
{
	if(!state) return;
	if(std::this_thread::get_id() != state->guiThread) std::terminate();
	state->active = false;
}

void Driver::stop() noexcept
{
	if(!state) return;
	if(std::this_thread::get_id() != state->guiThread) std::terminate();
	state->active = false;
	state->reader.request_stop();
	// Never detach a reader that can still dispatch into ENGINE. A stalled reader
	// is a hard test failure, not permission for an unbounded cleanup or UAF.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
	while(!state->readerFinished)
	{
		if(std::chrono::steady_clock::now() >= deadline) std::terminate();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if(state->reader.joinable()) state->reader.join();
	// NO GUI-holder release here: NET may still be using these objects.
}

void Driver::releaseGUIState() noexcept
{
	if(!state) return;
	if(state->nativeOnly || std::this_thread::get_id() != state->guiThread || state->active
		|| !state->readerFinished || state->reader.joinable()) std::terminate();
	// Caller holds the EXISTING interfaceMutex after endNetwork returned relocked.
	// Never relock here. NET has joined; GAME/ENGINE still exist.
	state->book.reset();
	state->callback.reset();
	state->battle.reset();
	state.reset();
}

std::weak_ptr<void> Driver::nativeLifetimeObserver() const
{
	if(!state || !state->nativeOnly || std::this_thread::get_id() != state->guiThread)
		throw std::runtime_error("Observer requires live native-tagged state on owner thread");
	return state;
}

void Driver::releaseNativeState() noexcept
{
	if(!state) return;
	if(!state->nativeOnly || state->engine || std::this_thread::get_id() != state->guiThread
		|| state->active || !state->readerFinished || state->reader.joinable()
		|| state->book || state->callback || state->battle) std::terminate();
	// NO GUI holders ever bound: no fictional NET join or interface mutex.
	state.reset();
}

Driver::~Driver()
{
	stop(); // No-op after protected release; never accesses ENGINE.
	// Missing protected release is a hard harness failure, not permission to
	// destroy GUI holders without exclusion or after GAME/ENGINE destruction.
	if(state) std::terminate();
}
}
