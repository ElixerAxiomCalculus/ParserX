#ifndef COMPRESS_H
#define COMPRESS_H

#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "grammar.h"
#include "table.h"

// Compressed ACTION/GOTO tables:
//  - default reductions: the most frequent reduce of a row fills its empty cells
//  - identical rows are stored once
//  - rows are packed into one comb vector with base / next / check arrays
// Cells are counted as 16-bit integers for the byte sizes.
class PackedTable {
public:
    void build(const Grammar& g, const ParsingTable& t);

    // Action including the row's default reduction; false means error
    bool action(int state, const std::string& symbol, Action& out) const;
    int gotoState(int state, const std::string& nonterminal) const;
    // Terminals with an explicit entry in the row
    std::vector<std::string> expected(int state) const;
    bool hasDefaultReduce(int state) const;

    // Every filled raw cell must decode to the same action; every empty raw
    // cell must decode to an error or the row's default reduction.
    bool verify(const ParsingTable& t, std::ostream& os) const;

    long rawBytes() const { return rawBytes_; }
    long packedBytes() const { return packedBytes_; }
    int uniqueActionRows() const { return (int)actionBase_.size(); }
    int defaultRows() const { return defaultRows_; }
    int actionSlots() const { return (int)actionNext_.size(); }

private:
    static int encode(const Action& a);
    static Action decode(int v);

    std::vector<std::string> terms_;
    std::vector<std::string> nts_;
    std::map<std::string, int> termIndex_;
    std::map<std::string, int> ntIndex_;

    std::vector<int> actionRowOf_;
    std::vector<int> actionDefault_;
    std::vector<int> actionBase_;
    std::vector<int> actionNext_;
    std::vector<int> actionCheck_;

    std::vector<int> gotoRowOf_;
    std::vector<int> gotoDefault_;
    std::vector<int> gotoBase_;
    std::vector<int> gotoNext_;
    std::vector<int> gotoCheck_;

    long rawBytes_ = 0;
    long packedBytes_ = 0;
    int defaultRows_ = 0;
};

#endif
