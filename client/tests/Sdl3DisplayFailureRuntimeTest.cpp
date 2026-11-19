/* SPDX-License-Identifier: GPL-2.0-or-later */
// Process-only regression. No SDL initialization, display, renderer or audio.
#include "../../clientsdl3/render/DisplayFailureHandler.h"
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_error.h>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
bool rejectLibrary = false;
bool rejectSymbol = false;
int unloadedLibraries = 0;
bool interruptNextWait = false;
}

extern "C" SDL_SharedObject * __real_SDL_LoadObject(const char * name);
extern "C" SDL_FunctionPointer __real_SDL_LoadFunction(SDL_SharedObject * library, const char * name);
extern "C" void __real_SDL_UnloadObject(SDL_SharedObject * library);
extern "C" pid_t __real_waitpid(pid_t child, int * status, int options);

extern "C" pid_t __wrap_waitpid(pid_t child, int * status, int options)
{
	if(interruptNextWait)
	{
		interruptNextWait = false;
		errno = EINTR;
		return -1;
	}
	return __real_waitpid(child, status, options);
}

extern "C" SDL_SharedObject * __wrap_SDL_LoadObject(const char * name)
{
	if(rejectLibrary)
	{
		SDL_SetError("Synthetic missing X11 provider");
		return nullptr;
	}
	return __real_SDL_LoadObject(name);
}

extern "C" SDL_FunctionPointer __wrap_SDL_LoadFunction(SDL_SharedObject * library, const char * name)
{
	if(rejectSymbol)
	{
		SDL_SetError("Synthetic missing X11 symbol");
		return nullptr;
	}
	return __real_SDL_LoadFunction(library, name);
}

extern "C" void __wrap_SDL_UnloadObject(SDL_SharedObject * library)
{
	++unloadedLibraries;
	__real_SDL_UnloadObject(library);
}

namespace
{
using ErrorHandler = int (*)(_XDisplay *);
using SetErrorHandler = ErrorHandler (*)(ErrorHandler);

void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

int previousHandler(_XDisplay *) { return 0; }

void forbiddenExitCleanup()
{
	constexpr char message[] = "UNSAFE_STATIC_DESTRUCTION\n";
	const auto written = write(STDERR_FILENO, message, sizeof(message) - 1);
	(void)written;
}

struct LocalCleanup
{
	~LocalCleanup() { forbiddenExitCleanup(); }
};

void checkInactiveBackends(SetErrorHandler setter)
{
	const char * drivers[] = {nullptr, "dummy", "wayland", "windows", "x11-other"};
	for(const char * driver : drivers)
	{
		DisplayFailureHandler handler(driver);
		require(setter(previousHandler) == previousHandler, "Inactive backend installed an X11 handler");
	}
}

void checkRestoration(SetErrorHandler setter)
{
	{
		DisplayFailureHandler handler("x11");
		auto installed = setter(previousHandler);
		require(installed && installed != previousHandler, "X11 fatal handler was not installed");
		setter(installed);
	}
	// Observe and restore the previous callback, including a deliberately null value.
	require(setter(nullptr) == previousHandler, "Normal shutdown did not restore prior X11 handler");
	{
		DisplayFailureHandler handler("x11");
	}
	require(setter(previousHandler) == nullptr, "Null prior X11 handler was not restored");
}

void checkUnavailableProvider(SetErrorHandler setter)
{
	const int before = unloadedLibraries;
	rejectLibrary = true;
	{
		DisplayFailureHandler handler("x11");
	}
	rejectLibrary = false;
	require(unloadedLibraries == before, "Missing provider attempted to unload an unowned handle");
	require(setter(previousHandler) == previousHandler, "Missing provider changed the global handler");
	rejectSymbol = true;
	{
		DisplayFailureHandler handler("x11");
	}
	rejectSymbol = false;
	require(unloadedLibraries == before + 1, "Missing symbol did not close its owned provider handle");
	require(setter(previousHandler) == previousHandler, "Missing symbol changed the global handler");
}

[[noreturn]] void invokeFatalHandler(SetErrorHandler setter)
{
	LocalCleanup local;
	if(std::atexit(forbiddenExitCleanup) != 0)
		std::_Exit(91);
	DisplayFailureHandler handler("x11");
	auto installed = setter(previousHandler);
	if(!installed || installed == previousHandler)
		std::_Exit(92);
	installed(nullptr);
	std::_Exit(93);
}

void waitForFailure(pid_t child)
{
	int status = 0;
	pid_t finished = 0;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	interruptNextWait = true;
	while(std::chrono::steady_clock::now() < deadline)
	{
		finished = waitpid(child, &status, WNOHANG);
		if(finished == child)
			break;
		if(finished < 0 && errno != EINTR)
			throw std::runtime_error("Cannot wait for the owned fatal handler child");
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	if(finished != child)
	{
		kill(child, SIGKILL);
		while(waitpid(child, &status, 0) < 0 && errno == EINTR) {}
		throw std::runtime_error("Fatal handler child did not exit inside its bound");
	}
	require(!interruptNextWait, "Synthetic EINTR did not exercise child waiting");
	require(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE, "Display loss did not exit with failure");
}

void checkFatalExit(SetErrorHandler setter)
{
	int output[2];
	require(pipe(output) == 0, "Cannot create child diagnostic pipe");
	const pid_t child = fork();
	require(child >= 0, "Cannot create fatal handler child");
	if(child == 0)
	{
		close(output[0]);
		if(dup2(output[1], STDERR_FILENO) < 0)
			std::_Exit(90);
		close(output[1]);
		invokeFatalHandler(setter);
	}
	close(output[1]);
	waitForFailure(child);
	char buffer[512];
	const auto length = read(output[0], buffer, sizeof(buffer));
	close(output[0]);
	require(length > 0, "Display loss omitted its diagnostic");
	const std::string diagnostic(buffer, static_cast<size_t>(length));
	require(diagnostic == "New Horizons: X11 display connection lost; terminating without unsafe teardown.\n",
		"Fatal display loss ran cleanup or emitted an unexpected diagnostic");
}

void checkUnavailableDiagnostic(SetErrorHandler setter, bool closedReader)
{
	int output[2];
	require(pipe(output) == 0, "Cannot create unavailable diagnostic pipe");
	const int flags = fcntl(output[1], F_GETFL);
	require(flags >= 0, "Cannot inspect diagnostic pipe flags");
	if(closedReader)
		close(output[0]);
	else
	{
		require(fcntl(output[1], F_SETFL, flags | O_NONBLOCK) == 0, "Cannot fill diagnostic pipe");
		char padding[4096]{};
		ssize_t written;
		do
		{
			written = write(output[1], padding, sizeof(padding));
		} while(written > 0 || (written < 0 && errno == EINTR));
		require(errno == EAGAIN, "Diagnostic pipe was not filled");
		require(fcntl(output[1], F_SETFL, flags) == 0, "Cannot restore blocking diagnostic pipe");
	}
	const pid_t child = fork();
	require(child >= 0, "Cannot create unavailable diagnostic child");
	if(child == 0)
	{
		if(!closedReader)
			close(output[0]);
		if(dup2(output[1], STDERR_FILENO) < 0)
			std::_Exit(90);
		close(output[1]);
		std::signal(SIGPIPE, SIG_DFL);
		invokeFatalHandler(setter);
	}
	waitForFailure(child);
	require(fcntl(output[1], F_GETFL) == flags, "Fatal handler did not restore shared diagnostic descriptor flags");
	close(output[1]);
	if(!closedReader)
		close(output[0]);
}
}

int main()
{
	try
	{
		std::unique_ptr<SDL_SharedObject, decltype(&SDL_UnloadObject)> library(SDL_LoadObject("libX11.so.6"), SDL_UnloadObject);
		require(bool(library), "Cannot load installed X11 provider");
		auto setter = reinterpret_cast<SetErrorHandler>(SDL_LoadFunction(library.get(), "XSetIOErrorHandler"));
		require(setter != nullptr, "Cannot resolve X11 handler setter");
		auto original = setter(previousHandler);
		checkInactiveBackends(setter);
		checkRestoration(setter);
		checkUnavailableProvider(setter);
		checkFatalExit(setter);
		checkUnavailableDiagnostic(setter, false);
		checkUnavailableDiagnostic(setter, true);
		setter(original);
		std::cout << "Display failure handler regression passed.\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
