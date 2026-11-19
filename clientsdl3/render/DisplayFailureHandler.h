/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

struct SDL_SharedObject;
struct _XDisplay;

/// Prevent global destruction racing rendering workers after fatal X11 I/O loss.
/// This handler remains installed until the SDL video backend has shut down.
class DisplayFailureHandler final
{
	using ErrorHandler = int (*)(_XDisplay *);
	using SetErrorHandler = ErrorHandler (*)(ErrorHandler);

	SDL_SharedObject * library = nullptr;
	SetErrorHandler setHandler = nullptr;
	ErrorHandler previousHandler = nullptr;

public:
	explicit DisplayFailureHandler(const char * videoDriver);
	~DisplayFailureHandler();
	DisplayFailureHandler(const DisplayFailureHandler &) = delete;
	DisplayFailureHandler & operator=(const DisplayFailureHandler &) = delete;
};
