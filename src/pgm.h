#ifndef PGM_H
#define PGM_H

#include "first_follow.h"
#include "grammar.h"
#include "table.h"

// Pager's practical general method (weak compatibility): LR(1) states are
// built as for CLR, but a new state is merged into an existing state with
// the same core when the two are weakly compatible. The result has the
// power of canonical LR(1) with a state count close to LALR(1).
LRMachine buildPGMMachine(const Grammar& g, const FirstFollow& ff);

#endif
