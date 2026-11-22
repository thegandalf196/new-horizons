"""Regression cases for the guard against views and references bound to temporaries."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "CI"))

from check_temporary_lifetimes import CPP, Parser, Signatures, dangling_lines, without_linkage


DECLARATIONS = b"""struct Hero {
    std::string getJsonKey() const;
    TModID getModScope() const;
    const std::string & storedName() const;
    std::vector<int> ids() const;
    JsonNode toJson() const;
    const char * literalName() const;
    std::string_view viewName() const;
    TextView viewAlias() const;
    const std::vector<int> & storedIds() const;
};
using TextView = std::string_view;
std::string SpellID::encode(int index);
int SpellID::encode(bool unused);
std::string makeText();
using TConstBonusListPtr = std::shared_ptr<const BonusList>;
DLL_LINKAGE TConstBonusListPtr moraleBonuses(const Hero * hero);
std::shared_ptr<Campaign> getCampaign();
"""


def lines(body):
    parser = Parser(CPP)
    source = DECLARATIONS + body
    signatures = Signatures()
    tree = parser.parse(without_linkage(source))
    signatures.add_aliases(tree)
    signatures.add(tree)
    signatures.finish()
    offset = DECLARATIONS.count(b"\n")
    return [line - offset for line in dangling_lines(source, parser, signatures)]


class TemporaryLifetimeTest(unittest.TestCase):
    def test_views_of_returned_values(self):
        # Line 2 is the Stonecaller defect that failed on Windows only.
        body = b"""void inspect(const Hero & hero, std::string_view parameter) {
    const std::string_view identity = hero.getHeroType()->getJsonKey();
    std::string_view name{hero.getModScope()};
    std::string_view key{SpellID::encode(index)};
    auto text = std::string_view(makeText());
    std::span<const int> values = hero.ids();
    std::string_view joined = hero.getJsonKey() + "x";
    std::string_view built = std::string("abc");
    std::string_view chosen = flag ? hero.getJsonKey() : std::string_view("x");
    parameter = hero.getJsonKey();
}"""
        self.assertEqual(lines(body), [2, 3, 4, 5, 6, 7, 8, 9, 10])

    def test_pointers_members_and_returns(self):
        body = b"""struct Named {
    std::string_view name;
    Named(const Hero & hero) : name(hero.getJsonKey()) {}
};
std::string_view fromTemporary(const Hero & hero) { return hero.getJsonKey(); }
const char * pointerFromTemporary(const Hero & hero) { return hero.getJsonKey().c_str(); }
void inspect(const Hero & hero) {
    const char * raw = hero.getJsonKey().c_str();
    const auto * bytes = makeText().data();
}"""
        self.assertEqual(lines(body), [3, 5, 6, 8, 9])

    def test_references_and_loops_into_temporaries(self):
        body = b"""void inspect(const Hero & hero) {
    const std::string & member = hero.toJson()["name"].String();
    const auto & first = hero.ids()[0];
    const auto & front = hero.ids().front();
    for(const auto & entry : hero.toJson().Vector()) {}
    for(const auto & entry : hero.toJson()["list"].Vector()) {}
}"""
        self.assertEqual(lines(body), [2, 3, 4, 5, 6])

    def test_lists_owned_by_returned_smart_pointers(self):
        # The Discipline and Dispel use-after-free reports had this shape.
        body = b"""void inspect(const Hero * hero) {
    for(const auto & bonus : *moraleBonuses(hero)) {}
    const auto & list = *moraleBonuses(hero);
    for(const auto glyphs = moraleBonuses(hero); const auto & bonus : *glyphs) {}
    const auto retained = moraleBonuses(hero);
    for(const auto & bonus : *retained) {}
    const auto & scenario = getCampaign()->scenario(0);
}"""
        self.assertEqual(lines(body), [2, 3])

    def test_owned_stored_and_extended_values(self):
        body = b"""void inspect(const Hero & hero, std::string_view parameter = {}) {
    const std::string identity = hero.getJsonKey();
    std::string_view owned = identity;
    std::string_view stored = hero.storedName();
    std::string_view viewed = hero.viewName();
    std::string_view aliased = hero.viewAlias();
    std::string_view literal = "core:thunar";
    const char * literalName = hero.literalName();
    std::string_view sub = owned.substr(1);
    compare(std::string_view(hero.getJsonKey()));
    const auto & extended = hero.getJsonKey();
    const auto & node = hero.toJson();
    const auto & pointee = hero.getHeroType()->storedName();
    const auto & storedFirst = hero.storedIds()[0];
    for(const auto & id : hero.ids()) {}
    for(const auto & id : hero.storedIds()) {}
    int code = SpellID::encode(true);
    std::string_view reviewed = hero.getJsonKey(); // lifetime-checked: regression fixture
    // const std::string_view identity = hero.getJsonKey();
    log("std::string_view identity = hero.getJsonKey();");
}"""
        self.assertEqual(lines(body), [])

    def test_overloads_that_return_references_are_not_trusted(self):
        body = b"""const std::string & Other::getJsonKey() const;
void inspect(const Hero & hero) {
    std::string_view key = hero.getJsonKey();
}"""
        self.assertEqual(lines(body), [])


if __name__ == "__main__":
    unittest.main()
