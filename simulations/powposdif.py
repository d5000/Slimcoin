#!/usr/bin/env python3

import math, sys
from decimal import Decimal

# if using as command line script, the list must be in the following format:
# "TYPETIME;TYPETIME"
# with TYPE: W = PoW, S = PoS. PoB blocks count as PoW blocks
# example: "W90;S60;W20;W50;W120"
# the chain is read from left to right (W 90 being the first non_genesis block in the example)

#INITIAL_TARGET = Decimal("1")
INITIAL_TARGET = 1
#SIX_HOURS = Decimal("21600")
SIX_HOURS = 21600
#STAKE_TARGET_SPACING = Decimal("90")
STAKE_TARGET_SPACING = 90
nTargetSpacingWorkMax = STAKE_TARGET_SPACING * 10 # 15 minutes, 10 * PoS spacing time
verbose = True
# difficulty = INITIAL_TARGET / TARGET

# list format: [ block_type, time_to_prevblock, target ]
# genesis block is added as ["W",0, 1 ]
# target is added by main loop
# blist = [["W", 0, INITIAL_TARGET]] + [[blockraw[0], Decimal(blockraw[1:]), None ] for blockraw in sys.argv[1].split(";") ]
blist = [["W", 0, INITIAL_TARGET]] + [[blockraw[0], float(blockraw[1:]), None ] for blockraw in sys.argv[1].split(";") ]


def getdifficulty(target):

    # Original Slimcoin code
    #int nShift = (blockindex->nBits >> 24) & 0xff;
    #double dDiff =
    #    (double)0x0000ffff / (double)(blockindex->nBits & 0x00ffffff);

    #while (nShift < 29)
    #{
    #    dDiff *= 256.0;
    #    nShift++;
    #}
    #while (nShift > 29)
    #{
    #    dDiff /= 256.0;
    #    nShift--;
    #}
    #
    #return dDiff;

    # as initial diff in this simulation is 1, this would represent the difficulty_1_target / current_target calculation
    return 1 / target 


def calc_target(bnNew, nActualSpacing, nTargetSpacing):

    nInterval = SIX_HOURS / nTargetSpacing
    # We subtract one "theoretic block time" from 6 hours and add TWO times the current "real" block time.
    bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing)

    # then it is divided by (nInterval + 1) * nTargetSpacing
    bnNew /= ((nInterval + 1) * nTargetSpacing)

    return bnNew

# recreation of GetLastBlockIndex (last block of the same type)
def getlastblockindex(lastpos):
    if lastpos > 0:
        p = lastpos - 1
    while (blist[lastpos][0] != blist[p][0]) and (p > 0):
        p -= 1

    return p # returns 0 also when no block was found (case first PoS block)
    

def getactualspacing(pos, prevblock):
    # if prevblock <= 0: # there is no spacing value if there are not two blocks of the same type before
    #     if verbose:
    #          print("Block %s: Too few %s blocks, no block spacing calculation possible." % (pos, blist[pos][0]))
    #     return -1

    prevprevblock = getlastblockindex(prevblock)
    if verbose:
        print("Block %s (type %s): prev: %s, prevprev: %s" % (pos, blist[pos][0], prevblock, prevprevblock))

    spacing = 0
    for block in blist[prevprevblock:prevblock]:
        spacing += block[1]
    return spacing

def numberofprevblocks(blocktype, pos):
    n = 0
    for p in range(pos): # checks blocks before pos
        if blocktype == blist[p][0]: 
            n += 1
    return n

# main loop

pos = 0
target = INITIAL_TARGET
t = 0
for block in blist:

    blocktype, blocktime = block[:2]
    if numberofprevblocks(blocktype, pos) < 2: # first blocks: spacing is not available
        prevblock = 0
        target = INITIAL_TARGET
        actualspacing = None

    else:
        
        prevblock = getlastblockindex(pos)
        prevtarget = blist[prevblock][2]
        actualspacing = getactualspacing(pos, prevblock)

        # PoW block target spacing calculation
        # min(nTargetSpacingWorkMax, (int64) STAKE_TARGET_SPACING * (1 + pindexLast->nHeight - pindexPrev->nHeight));

        if block[0] == "W":
            targetspacing = min(nTargetSpacingWorkMax, STAKE_TARGET_SPACING * (1 + (pos - 1) - prevblock))
        else:
            targetspacing = STAKE_TARGET_SPACING

        target = calc_target(prevtarget, actualspacing, targetspacing)
        # print("Target %s Prevtarget %s" % (target, prevtarget))


    t += blocktime
    block[2] = target
    diff = getdifficulty(target)

    print("%s Type: %s Time: %s Spacing: %s Target: %s Diff: %s" % (pos, blocktype, t, actualspacing, target, diff))

    pos += 1

