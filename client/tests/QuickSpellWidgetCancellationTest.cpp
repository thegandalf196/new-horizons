/* Standalone UNREGISTERED no-engine test entry. GPL-2.0-or-later.
 * Build owns compilation/execution and static-initializer/host-access admission.
 * This is NOT SDL, widget, network-join or populated-GUI-holder acceptance.
 */
#include "QuickSpellWidgetDriver.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>

// Explicit test-entry fatal handler: never open the player's message box.
// Any unexpected use is a fatal test failure, not a substitute engine.
void handleFatalError(const std::string &, bool) { std::abort(); }

namespace
{
using quickSpellWidgetTest::Driver;
using quickSpellWidgetTest::Options;

void require(bool condition, const char * reason)
{
	if(!condition) throw std::runtime_error(reason);
}

class Queue
{
	std::mutex mutex;
	std::condition_variable changed;
	std::deque<std::function<void()>> tasks;
	unsigned receipts = 0;
public:
	void post(const std::function<void()> & task)
	{
		std::lock_guard lock(mutex);
		require(tasks.empty(), "dispatcher queue overflow");
		tasks.push_back(task); // The ACTUAL Driver closure, never a copied algorithm.
		++receipts;
		changed.notify_all();
	}

	void awaitReceipt()
	{
		std::unique_lock lock(mutex);
		require(changed.wait_for(lock, std::chrono::seconds(1), [&] { return receipts == 1; }),
			"actual queue receipt timed out");
		require(tasks.size() == 1, "missing actual queued closure");
	}

	void pump()
	{
		std::function<void()> task;
		{
			std::lock_guard lock(mutex);
			require(tasks.size() == 1, "pump requires one actual closure");
			task = std::move(tasks.front());
			tasks.pop_front();
		}
		task();
	}
};

struct Cleanup
{
	Driver & driver;
	~Cleanup()
	{
		driver.stop();
		driver.releaseNativeState(); // Tagged/empty/joined, NEVER releaseGUIState.
	}
};

void writeExclusive(const std::string & path, const std::string & value)
{
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	require(fd >= 0, "cannot create fresh private lease");
	const auto written = ::write(fd, value.data(), value.size());
	::close(fd);
	require(written == static_cast<ssize_t>(value.size()), "short lease write");
}

void send(const std::string & directory, const std::string & message)
{
	const int fd = ::open((directory + "/control.fifo").c_str(), O_WRONLY | O_NONBLOCK | O_NOFOLLOW);
	require(fd >= 0, "cannot open actual FIFO writer");
	const auto written = ::write(fd, message.data(), message.size());
	::close(fd);
	require(written == static_cast<ssize_t>(message.size()), "short FIFO write");
}

std::string report(const std::string & directory)
{
	std::ifstream file(directory + "/driver.log");
	require(file.good(), "missing actual driver report");
	return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void run(const std::string & base, const std::string & name)
{
	const auto started = std::chrono::steady_clock::now();
	require(name == "uncancelled" || name == "active" || name == "lease" || name == "weak"
		|| name == "idempotent" || name == "sequence" || name == "unjoined-release", "unknown case");
	require(!base.empty() && base.front() == '/', "absolute private output required");
	struct stat metadata{};
	require(::lstat(base.c_str(), &metadata) == 0 && S_ISDIR(metadata.st_mode)
		&& metadata.st_uid == ::getuid() && (metadata.st_mode & 077) == 0, "unsafe private parent");
	const auto directory = base + "/" + name;
	require(::mkdir(directory.c_str(), 0700) == 0, "case output must be fresh");
	require(::mkfifo((directory + "/control.fifo").c_str(), 0600) == 0, "cannot create private FIFO");
	const std::string token = "native-cancellation-" + name;
	writeExclusive(directory + "/lease.token", token + "\n");

	Options options;
	options.sessionStart = started;
	options.privateDirectory = directory;
	options.token = token;
	options.slot = 0;
	options.expectedCanonicalSpell = "core:magicArrow"; // Never resolved: no engine.
	const auto queue = std::make_shared<Queue>(); // Outlives join AND late pump.
	Driver driver(Driver::NativeDispatcherTag{}, options,
		[queue](const std::function<void()> & task) { queue->post(task); });
	Cleanup cleanup{driver};
	const auto observer = driver.nativeLifetimeObserver();
	require(!observer.expired(), "missing initial native lifetime");

	if(name == "unjoined-release")
	{
		driver.revoke();
		driver.releaseNativeState(); // MUST terminate: thread is still joinable.
		throw std::runtime_error("unjoined native release was accepted");
	}
	if(name == "idempotent")
	{
		driver.revoke();
		driver.revoke();
		driver.stop();
		driver.stop();
		driver.releaseNativeState();
		driver.releaseNativeState();
		driver.revoke();
		require(observer.expired(), "state survived idempotent cleanup");
	}
	else
	{
		send(directory, token + (name == "sequence" ? " 2 BIND\n" : " 1 BIND\n"));
		queue->awaitReceipt(); // Not a sleep; proves the real reader posted a task.
		if(name == "active") driver.revoke();
		if(name == "lease") require(::unlink((directory + "/lease.token").c_str()) == 0, "lease revoke failed");
		if(name == "weak")
		{
			driver.stop();
			driver.releaseNativeState();
			const bool expiredBeforePump = observer.expired();
			queue->pump(); // Also breaks an intentional strong-capture mutant cycle.
			require(expiredBeforePump, "queued closure retained state strongly after release");
		}
		else queue->pump();

		const auto observed = report(directory);
		if(name == "uncancelled") require(observed == "REJECT native-no-engine\n", "positive closure did not reach no-engine boundary");
		else if(name == "lease") require(observed == "REJECT lease-or-deadline\n", "lease gate did not reject before engine access");
		else if(name == "sequence") require(observed == "REJECT sequence\n", "sequence gate did not reject before engine access");
		else require(observed.empty(), "cancelled closure executed despite active/weak guard");
	}

	driver.stop();
	driver.releaseNativeState();
	require(observer.expired(), "native state leaked after join/release");
	require(std::chrono::steady_clock::now() - started < std::chrono::seconds(3), "per-case deadline missed");
	std::printf("NATIVE_DRIVER_CASE %s PASS (not SDL/widget/NET cleanup)\n", name.c_str());
}
}

int main(int argc, char ** argv)
{
	// Extra in-process safety only; Build's independent TOTAL/per-case guardian
	// and host-access denial must be admitted before any execution, including init.
	const rlimit noCore{0, 0};
	if(::setrlimit(RLIMIT_CORE, &noCore) != 0) return 2;
	::alarm(10);
	try
	{
		require(argc == 3, "usage: cancellation-test ABS_PRIVATE_PARENT CASE");
		run(argv[1], argv[2]);
		return 0;
	}
	catch(const std::exception & error)
	{
		std::fprintf(stderr, "NATIVE_DRIVER_CASE FAIL: %s\n", error.what());
		return 1;
	}
}
