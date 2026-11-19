/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "DisplayFailureHandler.h"

#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <cstdlib>
#include <string_view>

#ifdef __linux__
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

namespace
{
void writeDisplayLossDiagnostic()
{
	// A full pipe must not delay termination, and a vanished reader must not
	// turn this explicit failure into SIGPIPE. Keep SIGPIPE blocked until _Exit.
	sigset_t blocked;
	sigemptyset(&blocked);
	sigaddset(&blocked, SIGPIPE);
	if(pthread_sigmask(SIG_BLOCK, &blocked, nullptr) != 0)
		return;

	const int flags = fcntl(STDERR_FILENO, F_GETFL);
	if(flags < 0)
		return;
	const bool changed = (flags & O_NONBLOCK) == 0;
	if(changed && fcntl(STDERR_FILENO, F_SETFL, flags | O_NONBLOCK) < 0)
		return;

	constexpr char message[] = "New Horizons: X11 display connection lost; terminating without unsafe teardown.\n";
	const auto written = write(STDERR_FILENO, message, sizeof(message) - 1);
	(void)written;
	if(changed)
		fcntl(STDERR_FILENO, F_SETFL, flags);
}

[[noreturn]] int stopAfterDisplayLoss(_XDisplay *)
{
	// Xlib normally calls exit(), which runs static destructors while asynchronous
	// image scaling still reads their data. A dead display cannot be recovered by
	// SDL teardown. Leave an abnormal exit and let the OS release the process.
	writeDisplayLossDiagnostic();
	std::_Exit(EXIT_FAILURE);
}
}
#endif

DisplayFailureHandler::DisplayFailureHandler(const char * videoDriver)
{
#ifdef __linux__
	if(!videoDriver || std::string_view(videoDriver) != "x11")
		return;

	library = SDL_LoadObject("libX11.so.6");
	if(!library)
	{
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "X11 display-loss protection unavailable: %s", SDL_GetError());
		return;
	}

	setHandler = reinterpret_cast<SetErrorHandler>(SDL_LoadFunction(library, "XSetIOErrorHandler"));
	if(!setHandler)
	{
		SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "X11 display-loss protection unavailable: %s", SDL_GetError());
		SDL_UnloadObject(library);
		library = nullptr;
		return;
	}
	previousHandler = setHandler(stopAfterDisplayLoss);
#else
	(void)videoDriver;
#endif
}

DisplayFailureHandler::~DisplayFailureHandler()
{
	if(setHandler)
		setHandler(previousHandler);
	if(library)
		SDL_UnloadObject(library);
}
