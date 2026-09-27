#!/usr/bin/env python3
"""Source guard for ordinary, explicit-split, and reverse-rebalance routing."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
EXCHANGE = (ROOT / "client/widgets/CExchangeController.cpp").read_text()
GARRISON = (ROOT / "client/widgets/CGarrisonInt.cpp").read_text()


def compact(source: str) -> str:
    return re.sub(r"\s+", "", re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S))


def main() -> None:
    exchange = EXCHANGE.split("void CExchangeController::moveStack(", 1)[1]
    exchange = exchange.split("void CExchangeController::moveSingleStackCreature(", 1)[0]
    exchange = compact(exchange)

    occupied = "if(target->getCreature(targetSlot)){GAME->interface()->cb->mergeStacks(source,target,sourceSlot,targetSlot);return;}"
    empty_preflight = "if(!UIHelper::checkLeadershipTransfer(source,target,sourceSlot,targetSlot,amountToMove))return;"
    zero_fit = 'if(mustKeepLastSourceCreature&&sourceCount<=1){GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("core.tcommand.5"));return;}'
    empty_last_stack = "if(mustKeepLastSourceCreature){GAME->interface()->cb->mergeOrSwapStacks(source,target,sourceSlot,targetSlot);return;}"
    for expected, label in (
        ("if(!targetSlot.validSlot())return;", "empty-slot availability guard"),
        ("constboolmustKeepLastSourceCreature=source->stacksCount()==1&&source->needsLastStack();", "last-source-stack rule"),
        ("constTQuantitysourceCount=source->getStackCount(sourceSlot);", "current source count"),
        (zero_fit, "single-creature gameplay explanation"),
        (occupied, "occupied same-creature combine route"),
        (empty_last_stack, "empty-slot last-stack move intent"),
        (empty_preflight, "ordinary empty-slot Leadership preflight"),
    ):
        assert expected in exchange, f"missing {label}"
    assert exchange.index("constboolmustKeepLastSourceCreature") < exchange.index(occupied)
    assert exchange.index(zero_fit) < exchange.index(occupied)
    assert exchange.index(empty_last_stack) < exchange.index(empty_preflight)

    click = GARRISON.split("void CGarrisonSlot::clickPressed(", 1)[1]
    click = click.split("void CGarrisonSlot::gesture(", 1)[0]
    click = compact(click)
    explicit_split = "if((owner->getSplittingMode()||ENGINE->isKeyboardShiftDown())&&(!creature||creature==selection->creature)){refr=split();}"
    last_stack_empty = 'elseif(!creature&&lastHeroStackSelected){constautoamount=selection->myStack->getCount()-1;if(amount<=0)GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("core.tcommand.5"));elseGAME->interface()->cb->mergeOrSwapStacks(selectedObj,owner->army(upg),selection->ID,ID);}'
    ordinary_merge = "elseGAME->interface()->cb->mergeStacks(selectedObj,owner->army(upg),selection->ID,ID);"
    assert explicit_split in click, "Shift/splitting mode must retain numeric split dialog"
    assert last_stack_empty in click, "last-stack empty-slot click must explain zero fit and send positive moves as a server-clamped intent"
    assert "elseif(lastHeroStackSelected)refr=split();" not in click, "last-stack combine must not open numeric dialog"
    assert "cb->splitStack(selectedObj,owner->army(upg),selection->ID,ID,amount);" not in click, "last-stack empty-slot moves must not send an exact count"
    assert ordinary_merge in click, "ordinary occupied same-creature click must merge"
    assert click.index(explicit_split) < click.index(ordinary_merge)
    assert click.index(explicit_split) < click.index(last_stack_empty) < click.index(ordinary_merge)

    split = GARRISON.split("bool CGarrisonSlot::split()", 1)[1]
    split = split.split("bool CGarrisonSlot::mustForceReselection()", 1)[0]
    split = compact(split)
    reverse = "if(amountLeft>countLeft&&amountRight<countRight)owner->splitStacks(this,owner->army(selection->upg),selection->ID,amountLeft);"
    forward = "elseif(amountLeft<countLeft&&amountRight>countRight)owner->splitStacks(selection,owner->army(upg),ID,amountRight);"
    assert "[this,selection,countLeft,countRight]" in split, "dialog must snapshot both original counts"
    assert reverse in split, "rebalancing toward original source must use a positive exact reverse split"
    assert forward in split, "rebalancing toward clicked destination must preserve forward split"

    split_stacks = GARRISON.split("void CGarrisonInt::splitStacks(", 1)[1]
    split_stacks = split_stacks.split("bool CGarrisonInt::checkSelected(", 1)[0]
    split_stacks = compact(split_stacks)
    assert "constTQuantitytransferAmount=amount-armyDest->getStackCount(slotDest);" in split_stacks
    assert "if(transferAmount<=0)return;" in split_stacks
    assert "checkLeadershipTransfer(armedObjs[from->upg],armyDest,from->ID,slotDest,transferAmount)" in split_stacks


if __name__ == "__main__":
    main()
