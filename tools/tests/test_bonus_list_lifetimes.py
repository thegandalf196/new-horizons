"""Regression cases for the C++20 bonus-list range-for guard."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "CI"))

from check_bonus_list_lifetimes import CPP, Parser, unsafe_loop_lines

# The workflow runs this module; it also carries the temporary-lifetime guard's cases.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_temporary_lifetimes import TemporaryLifetimeTest  # noqa: F401


class BonusListLifetimeTest(unittest.TestCase):
    def test_temporary_owner(self):
        source = b"""void inspect() {
    for (auto & bonus : *unit->getBonusesOfType(BonusType::BIND)) {}
    for (auto & bonus : *(unit.getBonuses(select(type)))) {}
    for (auto & bonus : *Base::getAllBonuses()) {}
}"""
        self.assertEqual(unsafe_loop_lines(source, Parser(CPP)), [2, 3, 4])

    def test_retained_owner_and_unrelated_text(self):
        source = b"""void inspect() {
    const auto retained = unit->getAllBonuses();
    for (auto & bonus : *retained) {}
    for (auto retained = unit->getBonusesOfType(type); auto & bonus : *retained) {}
    for (auto & record : *unit->permanentRecords()) {}
    // for (auto & bonus : *unit.getAllBonuses()) {}
    log("for (auto & bonus : *unit.getAllBonuses()) {}");
}"""
        self.assertEqual(unsafe_loop_lines(source, Parser(CPP)), [])


if __name__ == "__main__":
    unittest.main()
