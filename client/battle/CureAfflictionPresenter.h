/*
 * CureAfflictionPresenter.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

class CObjectListWindow;
class CStack;
class JsonNode;
namespace battle { class Unit; }

/// Eligible physical entries, in stable Frozen-then-Grave-Dust presentation order.
std::vector<std::string> curePhysicalAfflictionChoices(const JsonNode & rules, const battle::Unit * target);

/// Presentation only. The installed callbacks retain all live-state validation.
class CureAfflictionPresenter
{
public:
	virtual ~CureAfflictionPresenter() = default;
	/// Borrow target only during this synchronous call; never retain its address.
	virtual void show(const std::vector<std::string> & names,
		const CStack & initialTarget, std::function<void(int)> confirm,
		std::function<void()> cancel) = 0;
};

/// The ordinary window, text, close/select behavior and GUI-thread push.
std::shared_ptr<CObjectListWindow> showCureAfflictionModal(
	const std::vector<std::string> & names, std::function<void(int)> confirm,
	std::function<void()> cancel);
