// MonteCalc - UNO component registration
#include "addin.hpp"
#include "engine.hpp"

#include <cppuhelper/implementationentry.hxx>

using namespace com::sun::star;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;

using rtl::OUString;

namespace {

Reference<uno::XInterface> SAL_CALL createAddIn(const Reference<uno::XComponentContext>&) {
    return static_cast<cppu::OWeakObject*>(new montecalc::MonteCalcAddIn());
}
Reference<uno::XInterface> SAL_CALL createEngine(const Reference<uno::XComponentContext>& ctx) {
    return static_cast<cppu::OWeakObject*>(new montecalc::Engine(ctx));
}
OUString SAL_CALL addInImplName() { return OUString::createFromAscii(montecalc::kAddInImplName); }
OUString SAL_CALL engineImplName() { return OUString::createFromAscii(montecalc::kEngineImplName); }
Sequence<OUString> SAL_CALL addInServices() { return montecalc::MonteCalcAddIn::serviceNames(); }
Sequence<OUString> SAL_CALL engineServices() { return montecalc::Engine::serviceNames(); }

const cppu::ImplementationEntry entries[] = {
    {createAddIn, addInImplName, addInServices, cppu::createSingleComponentFactory, nullptr, 0},
    {createEngine, engineImplName, engineServices, cppu::createSingleComponentFactory, nullptr, 0},
    {nullptr, nullptr, nullptr, nullptr, nullptr, 0}};

} // namespace

extern "C" SAL_DLLPUBLIC_EXPORT void* SAL_CALL component_getFactory(const char* implName, void* serviceManager,
                                                                     void* registryKey) {
    return cppu::component_getFactoryHelper(implName, serviceManager, registryKey, entries);
}
