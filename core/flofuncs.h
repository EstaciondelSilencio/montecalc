// MonteCalc - semantics of the FLOsimula_* model functions (MC FLO naming).
//
// Shared by the Calc add-in and the in-memory calculation engine, so both
// evaluate a model identically.
//
// Argument conventions (see docs/FUNCTIONS.md):
//   distributions  FLOsimula_<Dist>([Name;] p1; p2; ...; [Min]; [Max]; [Round]; [Number])
//                  Name is optional: when the first argument is text it is the
//                  variable name. Min/Max truncate the distribution, Round gives
//                  the number of decimals (TRUE = 0), Number sums that many draws
//                  per iteration (compound distribution).
//   time series    FLOsimula_<Process>([Name;] p1; ...; [Number])   Number = periods
//   user defined   FLOsimula_UserDefined(Data; NumberOfColumns; [Name]; [Min]; [Max]; [Round]; [Number])
//                  FLOsimula_DiscreteUserDefined(Values; Probabilities; [Name]; ...)
//   others         FLOsimula_Bounded(Reference; LowerLimit; UpperLimit)
//                  FLOsimula_output(Name; [Value])
//                  FLOsimula_Correlation(Name1; Name2; Coefficient; [Type])
#pragma once

#include "distributions.h"
#include "timeseries.h"
#include "value.h"

#include <functional>
#include <string>
#include <vector>

namespace mc {

struct FloParam {
    const char* name;
    const char* desc;
};

struct FloFunction {
    enum Kind { Dist, TimeSeries, UserDefined, DiscreteUserDefined, Bounded, Output, Correlation };
    std::string name;          // e.g. "FLOsimula_Triangle"
    Kind kind;
    bool discrete = false;
    std::string label;         // e.g. "Triangle"
    std::string description;
    std::vector<FloParam> params;   // distribution / process parameters (without Name and options)
    std::function<DistPtr(const std::vector<double>&)> make;  // Dist
    TsType ts = TsType::AR1;                                 // TimeSeries
};

const std::vector<FloFunction>& floFunctions();
// Find by name; accepts "FLOsimula_Normal", "FLOSIMULA_NORMAL" or a service-qualified
// name such as "ORG.MONTECALC.MONTECALC.FLOSIMULA_NORMAL" (case-insensitive).
const FloFunction* findFlo(const std::string& name);

// Evaluate a FLOsimula function (records/registers through Simulation). Throws
// std::invalid_argument with a readable message on bad arguments.
double evalFlo(const FloFunction& f, const std::vector<Value>& args);

// Variable name given in the arguments (empty for unnamed inputs).
std::string floVariableName(const FloFunction& f, const std::vector<Value>& args);

// Builds the distribution of a user-defined function from a data range, following
// the formats of the manual (raw data, Min/Max/Number histogram, quantile table).
DistPtr userDefinedDistribution(const Value& data, int bins);

} // namespace mc
