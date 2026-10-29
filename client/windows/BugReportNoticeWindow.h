/* New Horizons local report notice. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "CWindowObject.h"

class CFilledTexture;

// Plain report feedback also works without gameplay services in reporter mode.
class BugReportNoticeWindow final : public CWindowObject
{
	std::shared_ptr<CFilledTexture> texture;
	std::vector<std::shared_ptr<CIntObject>> controls;
public:
	explicit BugReportNoticeWindow(const std::string & message);
	void showAll(Canvas & to) override;
};
