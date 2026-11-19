/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
#include "ScreenHandler.h"
#include "DisplayFailureHandler.h"
#include "../events/NotificationHandler.h"
#include "lib/CConfigHandler.h"
#include <SDL3/SDL.h>

ScreenHandler::~ScreenHandler()
{
	if(settings["general"]["notifications"].Bool())
		NotificationHandler::destroy();

	destroyScreenBuffers();
	destroyWindow();
	SDL_Quit();
	// The fatal I/O hook must still be live while SDL closes its X11 connection.
	displayFailureHandler.reset();
}
