// MonteCalc - dependencies between MC FLO variables (for the influence diagram,
// the FLO_Context sheet and the AI mentor)
#pragma once

#include "model.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace montecalc {

struct ModelGraph {
    std::vector<VarCell> vars;
    // edges[i] = indices of variables that variable i depends on (direct, through
    // intermediate non-variable cells)
    std::vector<std::set<int>> dependsOn;
    // correlation variables: partners by index (-1 if unknown)
    std::vector<std::pair<int, int>> corrPartners;
    std::vector<std::string> cellValues;   // current displayed values
    std::vector<double> numValues;
};

ModelGraph buildGraph(const ModelDoc& doc);

} // namespace montecalc
