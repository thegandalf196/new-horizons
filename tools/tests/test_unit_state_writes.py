"""Regression cases for the battle-unit plain-state write guard."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "CI"))

from check_unit_state_writes import file_issues, header_issues, health_mutators, main

HEALTH = b"""namespace battle {
class CHealth final {
public:
	explicit CHealth(const Unit * owner);
	void damage(int64_t & amount);
	void reset(bool clear = true);
	int32_t getCount() const;
};
}"""
MUTATING = health_mutators(HEALTH)


def messages(source, relative="lib/battle/CUnitStateExample.cpp"):
    return [message for _, message in file_issues(relative, source, MUTATING)]


class UnitStateWriteTest(unittest.TestCase):
    def test_learns_only_mutating_health_methods(self):
        self.assertEqual(MUTATING, {"damage", "reset"})

    def test_direct_member_writes_are_rejected(self):
        source = b"""void CUnitState::change(CUnitState & other)
{
	position = BattleHex(3);
	other.ghost = true;
	++ghostPending;
	health.damage(amount);
	other.health = health;
	std::swap(phantomIntegrity, other.phantomIntegrity);
	bool & alias = ghost;
	const bool & view = ghost;
	const int32_t count = health.getCount();
}"""
        self.assertEqual(messages(source), [
            "direct write of position; use its CUnitState mutator",
            "direct write of ghost; use its CUnitState mutator",
            "direct write of ghostPending; use its CUnitState mutator",
            "health.damage() bypasses the revision; use editHealth()->",
            "direct write of health; use its CUnitState mutator",
            "swap of phantomIntegrity bypasses the revision",
            "non-const reference to ghost bypasses the revision",
        ])

    def test_mutators_and_handles_are_allowed(self):
        source = b"""void CUnitState::setHealth(const CHealth & value)
{
	health = value;
}
void CUnitState::change()
{
	setPosition(BattleHex(3));
	editHealth()->damage(amount);
	editOriginalHealth()->reset();
	battleFormRoundsRemaining = 1;
	stateRevision.bump();
}"""
        self.assertEqual(messages(source), [])

    def test_form_writes_need_a_revision_change(self):
        source = b"""void CUnitState::tick()
{
	--battleFormRoundsRemaining;
	phantomInitialIntegrity = 0;
}"""
        self.assertEqual(messages(source), ["tick writes battle-form or Phantom state without changing the revision"])

    def test_kept_or_unused_handles_are_rejected(self):
        source = b"""void use(CUnitState & unit)
{
	auto edit = unit.editHealth();
	unit.editHealth();
	unit.editHealth()->damage(amount);
}"""
        self.assertEqual(messages(source, "AI/Example.cpp"), [
            "use editHealth() at once with ->; never keep the handle",
            "use editHealth() at once with ->; never keep the handle",
        ])

    def test_unrelated_classes_and_files_are_ignored(self):
        source = b"""void Window::move()
{
	position = Point(3, 4);
	health.damage(amount);
}"""
        self.assertEqual(messages(source, "client/Window.cpp"), [])

    def test_friend_files_are_checked(self):
        source = b"""void exchange(CUnitState & destination, CUnitState & prepared)
{
	destination.position = prepared.position;
}"""
        self.assertEqual(messages(source, "lib/battle/PreparedStackStateTransfer.cpp"),
                         ["direct write of position; use its CUnitState mutator"])

    def test_header_requires_private_fields_and_known_friends(self):
        header = b"""class CUnitState : public Unit
{
	friend class UnitStateValueFields;
	friend class Stranger;
public:
	BattleHex position;
private:
	UnitStateRevision stateRevision;
	CHealth health;
	bool ghost, ghostPending;
	int64_t phantomInitialIntegrity = 0, phantomIntegrity = 0;
	CHealth battleFormOriginalHealth;
};"""
        self.assertEqual([message for _, message in header_issues(header)], [
            "CUnitState::position must stay private",
            "new CUnitState friend Stranger: add its files to FRIEND_FILES in this check",
        ])

    def test_repository_has_no_violations(self):
        self.assertEqual(main(), 0)


if __name__ == "__main__":
    unittest.main()
