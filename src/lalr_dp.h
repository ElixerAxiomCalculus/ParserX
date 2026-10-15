#ifndef LALR_DP_H
#define LALR_DP_H

#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "first_follow.h"
#include "grammar.h"
#include "table.h"

struct DPStats {
    int ntTransitions = 0;
    int readsEdges = 0;
    int includesEdges = 0;
    int lookbackEdges = 0;
    int nontrivialSccs = 0;
};

// The digraph algorithm of DeRemer and Pennello: for every node x,
// F(x) = Fp(x) U union of F(y) over x R y, computed with one SCC traversal.
// Nodes are 0..n-1, sets are vectors of terminal indices marked 0/1.
void digraph(const std::vector<std::vector<int> >& relation,
             std::vector<std::vector<char> >& sets, int* nontrivialSccs);

// LALR(1) by DeRemer & Pennello (1982) on the LR(0) automaton:
// DR, reads -> Read, includes -> Follow, lookback -> LA.
LRMachine buildLALRMachineDP(const Grammar& g, const FirstFollow& ff,
                             const LRMachine& lr0, DPStats* stats);

// Checks that two LALR tables are identical up to state renumbering, matching
// states through their LR(0) cores. Writes differences to os; true when equal.
bool verifyLalrTables(const Grammar& g, const ParsingTable& merge,
                      const ParsingTable& dp, std::ostream& os);

#endif
