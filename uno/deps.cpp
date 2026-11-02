// MonteCalc - dependencies between MC FLO variables
#include "deps.hpp"
#include "util.hpp"

#include <com/sun/star/text/XText.hpp>

#include <deque>

using namespace com::sun::star;
using com::sun::star::uno::Reference;
using com::sun::star::uno::UNO_QUERY_THROW;

namespace montecalc {

ModelGraph buildGraph(const ModelDoc& doc) {
    ModelGraph g;
    g.vars = doc.scan();
    auto src = doc.formulaSource();
    std::map<mc::formula::CellAddr, int> varAt;
    std::map<std::string, int> byName;
    for (size_t i = 0; i < g.vars.size(); ++i) {
        const auto& a = g.vars[i].addr;
        varAt[{a.Sheet, a.Column, a.Row}] = int(i);
        if (g.vars[i].kind != mc::VarKind::Correlation) byName[mc::lower(g.vars[i].name)] = int(i);
    }
    // direct references of a cell (parsed once per cell)
    std::map<mc::formula::CellAddr, std::vector<mc::formula::CellAddr>> refsCache;
    auto refsOf = [&](const mc::formula::CellAddr& a) -> const std::vector<mc::formula::CellAddr>& {
        auto it = refsCache.find(a);
        if (it != refsCache.end()) return it->second;
        std::vector<mc::formula::CellAddr> out;
        auto c = src->cell(a);
        if (c.type == mc::formula::CellContent::Formula) {
            try {
                auto tree = mc::formula::parse(c.text, a.sheet, src.get());
                std::vector<mc::formula::RangeAddr> rs;
                mc::formula::collectRanges(tree, rs);
                for (const auto& r : rs) {
                    long cells = long(r.b.col - r.a.col + 1) * long(r.b.row - r.a.row + 1);
                    if (cells > 20000) continue;
                    for (int col = r.a.col; col <= r.b.col; ++col)
                        for (int row = r.a.row; row <= r.b.row; ++row) out.push_back({r.a.sheet, col, row});
                }
            } catch (...) {
            }
        }
        return refsCache[a] = out;
    };
    g.dependsOn.resize(g.vars.size());
    for (size_t i = 0; i < g.vars.size(); ++i) {
        const auto& a = g.vars[i].addr;
        mc::formula::CellAddr start{a.Sheet, a.Column, a.Row};
        std::set<mc::formula::CellAddr> seen{start};
        std::deque<mc::formula::CellAddr> q;
        for (const auto& r : refsOf(start)) q.push_back(r);
        int steps = 0;
        while (!q.empty() && ++steps < 200000) {
            auto c = q.front();
            q.pop_front();
            if (!seen.insert(c).second) continue;
            auto v = varAt.find(c);
            if (v != varAt.end()) {
                if (g.vars[v->second].kind != mc::VarKind::Correlation) {
                    g.dependsOn[i].insert(v->second);
                    continue;  // stop at variables
                }
            }
            for (const auto& r : refsOf(c)) q.push_back(r);
        }
    }
    g.corrPartners.assign(g.vars.size(), {-1, -1});
    for (size_t i = 0; i < g.vars.size(); ++i) {
        if (g.vars[i].kind != mc::VarKind::Correlation) continue;
        auto a = byName.find(mc::lower(g.vars[i].corrA)), b = byName.find(mc::lower(g.vars[i].corrB));
        g.corrPartners[i] = {a == byName.end() ? -1 : a->second, b == byName.end() ? -1 : b->second};
    }
    for (const auto& v : g.vars) {
        auto c = doc.cell(v.addr);
        g.numValues.push_back(c->getError() ? std::nan("") : c->getValue());
        g.cellValues.push_back(u8(Reference<text::XText>(c, UNO_QUERY_THROW)->getString()));
    }
    return g;
}

} // namespace montecalc
