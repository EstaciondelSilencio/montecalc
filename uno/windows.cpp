// temporary placeholder (replaced by the real windows)
#include "windows.hpp"
namespace montecalc {
void openWizard(const Ctx&, const Frame&) {}
void openFitWindow(const Ctx&, const Frame&) {}
void openResultWindow(const Ctx&, const Frame&, const std::string&) {}
void openCorrelogram(const Ctx&, const Frame&) {}
void openInfluenceDiagram(const Ctx&, const Frame&) {}
void openMentor(const Ctx&, const Frame&) {}
void openVariableList(const Ctx&, const Frame&) {}
void writeContextSheet(const Ctx&, const Frame&) {}
std::string modelContextText(const Frame&) { return {}; }
}
