#ifndef FIRST_FOLLOW_H
#define FIRST_FOLLOW_H

#include <map>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "grammar.h"

// Why a terminal is in FOLLOW(B): $ seeded at the start symbol, FIRST of the
// symbols after B in production prod, or FOLLOW of prod's left side because
// what follows B there can be empty.
struct FollowReason {
    enum Kind { SEED, FIRST_OF_REST, FOLLOW_OF_LHS };
    Kind kind = SEED;
    int prod = -1;
    int position = -1;
};

class FirstFollow {
public:
    static const std::string EPS;

    explicit FirstFollow(const Grammar& g);

    const std::set<std::string>& first(const std::string& symbol) const;
    const std::set<std::string>& follow(const std::string& symbol) const;
    bool nullable(const std::string& symbol) const;

    std::set<std::string> firstOfSequence(const std::vector<std::string>& sequence) const;
    const FollowReason* followReason(const std::string& symbol, const std::string& terminal) const;

    void printFirst(std::ostream& os) const;
    void printFollow(std::ostream& os) const;

private:
    void computeNullable();
    void computeFirst();
    void computeFollow();
    void addFollow(const std::string& B, const std::string& a, const FollowReason& why,
                   bool& changed);

    const Grammar& g_;
    std::set<std::string> nullable_;
    std::map<std::string, std::set<std::string> > first_;
    std::map<std::string, std::set<std::string> > follow_;
    std::map<std::string, std::map<std::string, FollowReason> > followWhy_;
};

#endif
