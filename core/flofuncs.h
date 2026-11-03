// MonteCalc - semantics of the FLOsimula_* model functions (MC FLO naming).
//
// Shared by the Calc add-in and the in-memory calculation engine, so both
// evaluate a model identically.
//
// Argument conventions follow the MC FLO wiki (www.mcflosim.ch/wiki):
//   distributions  FLOsimula_<Dist>(p1; p2; ...; [Variable_name]; [Number]; [Min]; [Max]; [Round])
//                  e.g. =FLOsimula_Triangular(10;16;13;"Umsatz_Kunde"). The name may be left out
//                  (unnamed input, named by Register variables). Number sums that many draws per
//                  iteration (compound distribution); Min/Max truncate; Round = decimals (TRUE = 0).
//   time series    FLOsimula_TemporalS_ARmodel(Start; Weight; StDeviation; UseBefore; [Name]; [Number]) ...
//                  Number = 0 (default): the path continues from iteration to iteration
//                  (5,000 iterations = 5,000 periods); Number = n: every iteration starts at
//                  Start and returns period n. UseBefore = 1: every iteration starts at Start
//                  (typically the previous period's cell).
//   user defined   FLOsimula_UserDefined(Data; NumberOfBins; [Name]; ...)
//                  FLOsimula_DiscreteUserDefined(Values; Probabilities; [Name]; ...)
//   others         FLOsimula_Bounded(Variable; LowerLimit; UpperLimit; [Name])  truncates a variable
//                  FLOsimula_output(Name; [Value])
//                  FLOsimula_correlation(Name1; Name2; Coefficient; [CorType])  MC FLO type codes
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
    std::string name;          // e.g. "FLOsimula_Triangular"
    std::vector<std::string> aliases;   // e.g. FLOsimula_Uniforme
    Kind kind;
    bool discrete = false;
    std::string label;         // e.g. "Triangular" (wizard), "AR(1)" for time series
    std::string description;
    std::vector<FloParam> params;   // distribution / process parameters (without Name and options)
    std::function<DistPtr(const std::vector<double>&)> make;  // Dist
    TsType ts = TsType::AR1;                                 // TimeSeries
    size_t minParams = 0;                                    // 0 = all params required
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
