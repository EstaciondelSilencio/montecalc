// MonteCalc - MC FLO function names in Excel files (see mcflo_names.hpp)
#include "mcflo_names.hpp"

#include "addin.hpp"
#include "util.hpp"

#include <com/sun/star/beans/NamedValue.hpp>
#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/container/XEnumeration.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/embed/ElementModes.hpp>
#include <com/sun/star/embed/StorageFactory.hpp>
#include <com/sun/star/embed/XStorage.hpp>
#include <com/sun/star/embed/XTransactedObject.hpp>
#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/io/XOutputStream.hpp>
#include <com/sun/star/io/XStream.hpp>
#include <com/sun/star/lang/XSingleServiceFactory.hpp>
#include <com/sun/star/sheet/CellFlags.hpp>
#include <com/sun/star/sheet/XCellRangesQuery.hpp>
#include <com/sun/star/sheet/XSheetCellRanges.hpp>
#include <com/sun/star/sheet/XSpreadsheetDocument.hpp>
#include <com/sun/star/table/XCell.hpp>
#include <com/sun/star/util/XModifiable.hpp>
#include <cppuhelper/supportsservice.hxx>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <map>

using namespace com::sun::star;
using com::sun::star::uno::Any;
using com::sun::star::uno::Reference;
using com::sun::star::uno::Sequence;
using com::sun::star::uno::UNO_QUERY;
using com::sun::star::uno::UNO_QUERY_THROW;
using rtl::OUString;

namespace montecalc {
namespace {

std::string lowerAscii(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string upperAscii(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// lower-case name (MC FLO or programmatic) -> MC FLO name
const std::map<std::string, std::string>& byLowerName() {
    static const std::map<std::string, std::string> m = [] {
        std::map<std::string, std::string> r;
        for (const auto& [prog, name] : mcfloFunctionNames()) {
            r[lowerAscii(name)] = name;
            r[lowerAscii(prog)] = name;
        }
        return r;
    }();
    return m;
}

bool identStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }

} // namespace

std::string toCalcSpelling(const std::string& f, bool& changed) {
    std::string out;
    out.reserve(f.size());
    bool inString = false;
    for (size_t i = 0; i < f.size();) {
        char c = f[i];
        if (c == '"') { inString = !inString; out += c; ++i; continue; }
        if (inString) { out += c; ++i; continue; }
        bool boundary = i == 0 || !(identChar(f[i - 1]) || f[i - 1] == '$' || f[i - 1] == '\'');
        // an optional reference to the add-in file ("[1]!FLOsimula_PERT(") as Excel may write it
        size_t start = i, k = i;
        if (boundary && c == '[') {
            size_t j = i + 1;
            while (j < f.size() && std::isdigit(static_cast<unsigned char>(f[j]))) ++j;
            if (j > i + 1 && j + 1 < f.size() && f[j] == ']' && f[j + 1] == '!') k = j + 2;
        }
        if (boundary && k < f.size() && identStart(f[k])) {
            size_t e = k;
            while (e < f.size() && identChar(f[e])) ++e;
            std::string ident = f.substr(k, e - k);
            std::string bare = ident;
            if (lowerAscii(bare).rfind("_xll.", 0) == 0) bare = bare.substr(5);   // Excel's prefix of XLL functions
            size_t p = e;
            while (p < f.size() && f[p] == ' ') ++p;
            auto it = byLowerName().find(lowerAscii(bare));
            if (p < f.size() && f[p] == '(' && it != byLowerName().end()) {
                std::string calc = upperAscii(it->second);   // Calc resolves add-in names in capitals
                if (f.compare(start, e - start, calc) != 0) changed = true;
                out += calc;
                i = e;
                continue;
            }
            out += f.substr(start, e - start);
            i = e;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

std::string toMcFloSpelling(const std::string& text, bool& changed) {
    static const std::string kPrefix = "ORG.MONTECALC.MONTECALC.";
    std::string out;
    size_t i = 0;
    for (;;) {
        size_t p = text.find(kPrefix, i);
        if (p == std::string::npos) break;
        size_t e = p + kPrefix.size();
        while (e < text.size() && identChar(text[e]) && text[e] != '.') ++e;
        auto it = byLowerName().find(lowerAscii(text.substr(p + kPrefix.size(), e - p - kPrefix.size())));
        out.append(text, i, p - i);
        if (it != byLowerName().end()) {
            out += it->second;
            changed = true;
        } else {
            out.append(text, p, e - p);
        }
        i = e;
    }
    out.append(text, i, std::string::npos);
    return out;
}

int resolveMcFloFormulas(const Reference<frame::XModel>& model) {
    Reference<sheet::XSpreadsheetDocument> doc(model, UNO_QUERY);
    if (!doc.is()) return 0;
    int n = 0;
    Reference<container::XIndexAccess> sheets(doc->getSheets(), UNO_QUERY_THROW);
    for (sal_Int32 s = 0; s < sheets->getCount(); ++s) {
        Reference<sheet::XCellRangesQuery> q(sheets->getByIndex(s), UNO_QUERY);
        if (!q.is()) continue;
        Reference<sheet::XSheetCellRanges> ranges = q->queryContentCells(sheet::CellFlags::FORMULA);
        Reference<container::XEnumerationAccess> ea(ranges.is() ? ranges->getCells() : nullptr, UNO_QUERY);
        if (!ea.is()) continue;
        Reference<container::XEnumeration> en = ea->createEnumeration();
        while (en->hasMoreElements()) {
            Reference<table::XCell> cell(en->nextElement(), UNO_QUERY);
            if (!cell.is()) continue;
            std::string f = u8(cell->getFormula());
            bool changed = false;
            std::string g = toCalcSpelling(f, changed);
            if (!changed) continue;
            try {
                cell->setFormula(ou(g));
                ++n;
            } catch (const uno::Exception&) {   // e.g. part of an array formula
            }
        }
    }
    return n;
}

namespace {
std::string readAll(const Reference<io::XInputStream>& in) {
    std::string r;
    Sequence<sal_Int8> buf;
    for (;;) {
        sal_Int32 got = in->readBytes(buf, 65536);
        if (got <= 0) break;
        r.append(reinterpret_cast<const char*>(buf.getConstArray()), size_t(got));
    }
    return r;
}

// the XML parts of the workbook ("xl/...") with formulas
bool rewriteStorage(const Reference<embed::XStorage>& st, bool inXl) {
    bool any = false;
    for (const OUString& n : st->getElementNames()) {
        std::string name = u8(n);
        if (st->isStorageElement(n)) {
            if (!inXl && name != "xl") continue;
            Reference<embed::XStorage> sub = st->openStorageElement(n, embed::ElementModes::READWRITE);
            if (rewriteStorage(sub, true)) {
                Reference<embed::XTransactedObject>(sub, UNO_QUERY_THROW)->commit();
                any = true;
            }
            continue;
        }
        if (!inXl || name.size() < 4 || name.compare(name.size() - 4, 4, ".xml") != 0) continue;
        std::string text;
        {
            Reference<io::XStream> in = st->openStreamElement(n, embed::ElementModes::READ);
            text = readAll(in->getInputStream());
            in->getInputStream()->closeInput();
        }
        bool changed = false;
        std::string out = toMcFloSpelling(text, changed);
        if (!changed) continue;
        Reference<io::XStream> w = st->openStreamElement(n, embed::ElementModes::WRITE | embed::ElementModes::TRUNCATE);
        Reference<io::XOutputStream> os = w->getOutputStream();
        os->writeBytes(Sequence<sal_Int8>(reinterpret_cast<const sal_Int8*>(out.data()), sal_Int32(out.size())));
        os->closeOutput();
        any = true;
    }
    return any;
}
} // namespace

bool writeMcFloSpelling(const Reference<uno::XComponentContext>& ctx, const std::string& url) {
    Reference<lang::XSingleServiceFactory> factory = embed::StorageFactory::create(ctx);
    beans::PropertyValue fmt;
    fmt.Name = "StorageFormat";
    fmt.Value <<= OUString("ZipFormat");
    Sequence<beans::PropertyValue> media(&fmt, 1);
    Sequence<Any> args(3);
    args.getArray()[0] <<= ou(url);
    args.getArray()[1] <<= embed::ElementModes::READWRITE;
    args.getArray()[2] <<= media;
    Reference<embed::XStorage> root(factory->createInstanceWithArguments(args), UNO_QUERY_THROW);
    bool changed = rewriteStorage(root, false);
    if (changed) Reference<embed::XTransactedObject>(root, UNO_QUERY_THROW)->commit();
    Reference<lang::XComponent>(root, UNO_QUERY_THROW)->dispose();
    return changed;
}

// ---------------------------------------------------------------- the job (registry/Jobs.xcu)
Any McFloNamesJob::execute(const Sequence<beans::NamedValue>& args) {
    try {
        Sequence<beans::NamedValue> env;
        for (const auto& a : args)
            if (a.Name == "Environment") a.Value >>= env;
        OUString event;
        Reference<frame::XModel> model;
        for (const auto& e : env) {
            if (e.Name == "EventName") e.Value >>= event;
            if (e.Name == "Model") e.Value >>= model;
        }
        if (const char* log = std::getenv("MONTECALC_DEBUG_JOB")) {   // development aid
            if (FILE* fp = std::fopen(log, "a")) {
                std::fprintf(fp, "job: event %s model %d url %s\n", u8(event).c_str(), model.is() ? 1 : 0,
                             model.is() ? u8(model->getURL()).c_str() : "");
                std::fclose(fp);
            }
        }
        if (!model.is()) return Any();
        std::string url = u8(model->getURL()), filter;
        for (const auto& p : model->getArgs())
            if (p.Name == "FilterName") { OUString f; p.Value >>= f; filter = u8(f); }
        std::string ext = lowerAscii(url.substr(url.find_last_of('.') == std::string::npos ? url.size() : url.find_last_of('.')));
        bool calcOwn = filter.rfind("calc8", 0) == 0 || filter.rfind("calc_", 0) == 0 || filter.find("StarOffice") != std::string::npos ||
                       filter.find("OpenDocument") != std::string::npos;
        if ((event == "OnLoad" || event == "OnLoadFinished") && !calcOwn) {
            // MC FLO workbooks (and other foreign files): MC FLO's names become working MonteCalc calls
            Reference<util::XModifiable> mod(model, UNO_QUERY);
            bool wasModified = mod.is() && mod->isModified();
            if (resolveMcFloFormulas(model) > 0 && mod.is() && !wasModified) mod->setModified(false);
        } else if ((event == "OnSaveDone" || event == "OnSaveAsDone") && (ext == ".xlsx" || ext == ".xlsm") &&
                   (filter == "Calc Office Open XML" || filter == "Calc MS Excel 2007 XML" || filter == "Calc MS Excel 2007 VBA XML")) {
            writeMcFloSpelling(ctx_, url);   // MC FLO's spelling in the saved workbook
        }
    } catch (const uno::Exception&) {
    } catch (const std::exception&) {
    }
    return Any();
}

OUString McFloNamesJob::getImplementationName() { return OUString::createFromAscii(kNamesJobImplName); }
sal_Bool McFloNamesJob::supportsService(const OUString& name) { return cppu::supportsService(this, name); }
Sequence<OUString> McFloNamesJob::getSupportedServiceNames() { return serviceNames(); }
Sequence<OUString> McFloNamesJob::serviceNames() {
    OUString n = OUString::createFromAscii(kNamesJobServiceName);
    return Sequence<OUString>(&n, 1);
}

} // namespace montecalc
