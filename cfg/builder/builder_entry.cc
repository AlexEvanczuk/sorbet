#include "builder.h"

#include <algorithm> // sort
#include <unordered_map>

using namespace std;

namespace ruby_typer {
namespace cfg {

void jumpToDead(BasicBlock *from, CFG &inWhat);

unique_ptr<CFG> CFGBuilder::buildFor(core::Context ctx, ast::MethodDef &md) {
    unique_ptr<CFG> res(new CFG); // private constructor
    res->symbol = md.symbol;
    core::LocalVariable retSym =
        ctx.state.newTemporary(core::UniqueNameKind::CFG, core::Names::returnMethodTemp(), md.symbol);
    core::LocalVariable selfSym =
        ctx.state.newTemporary(core::UniqueNameKind::CFG, core::Names::selfMethodTemp(), md.symbol);

    BasicBlock *entry = res->entry();

    entry->exprs.emplace_back(selfSym, md.loc, make_unique<Self>(md.symbol.info(ctx).owner));
    auto methodName = md.symbol.info(ctx).name;

    int i = 0;
    std::unordered_map<core::SymbolRef, core::LocalVariable> aliases;
    for (core::SymbolRef argSym : md.symbol.info(ctx).arguments()) {
        core::LocalVariable arg(argSym.info(ctx).name);
        entry->exprs.emplace_back(arg, argSym.info(ctx).definitionLoc, make_unique<LoadArg>(selfSym, methodName, i));
        aliases[argSym] = arg;
        i++;
    }
    auto cont = walk(CFGContext(ctx, *res.get(), retSym, 0, nullptr, aliases), *res, md.rhs.get(), entry);
    core::LocalVariable retSym1 =
        ctx.state.newTemporary(core::UniqueNameKind::CFG, core::Names::returnMethodTemp(), md.symbol);

    cont->exprs.emplace_back(retSym1, md.loc, make_unique<Return>(retSym)); // dead assign.
    jumpToDead(cont, *res.get());

    std::vector<Binding> aliasesPrefix;
    for (auto kv : aliases) {
        core::SymbolRef global = kv.first;
        core::LocalVariable local = kv.second;
        res->minLoops[local] = -1;
        if (!global.info(ctx).isMethodArgument()) {
            aliasesPrefix.emplace_back(local, md.symbol.info(ctx).definitionLoc, make_unique<Alias>(global));
        }
    }
    std::sort(aliasesPrefix.begin(), aliasesPrefix.end(),
              [](const Binding &l, const Binding &r) -> bool { return l.bind.name._id < r.bind.name._id; });

    entry->exprs.insert(entry->exprs.begin(), make_move_iterator(aliasesPrefix.begin()),
                        make_move_iterator(aliasesPrefix.end()));

    fillInTopoSorts(ctx, *res);
    dealias(ctx, *res);
    fillInBlockArguments(ctx, *res);
    return res;
}

void CFGBuilder::fillInTopoSorts(core::Context ctx, CFG &cfg) {
    // needed to find loop headers.
    for (auto &bb : cfg.basicBlocks) {
        std::sort(bb->backEdges.begin(), bb->backEdges.end(),
                  [](const BasicBlock *a, const BasicBlock *b) -> bool { return a->outerLoops < b->outerLoops; });
    }

    auto &target1 = cfg.forwardsTopoSort;
    target1.resize(cfg.basicBlocks.size());
    int count = topoSortFwd(target1, 0, cfg.entry());
    Error::check(count == cfg.basicBlocks.size());

    auto &target2 = cfg.backwardsTopoSort;
    target2.resize(cfg.basicBlocks.size());
    count = topoSortBwd(target2, 0, cfg.deadBlock());
    Error::check(count == cfg.basicBlocks.size());
    return;
}

CFGContext CFGContext::withTarget(core::LocalVariable target) {
    auto ret = CFGContext(*this);
    ret.target = target;
    return ret;
}

CFGContext CFGContext::withScope(BasicBlock *scope) {
    auto ret = CFGContext(*this);
    ret.scope = scope;
    ret.loops += 1;
    return ret;
}
} // namespace cfg
} // namespace ruby_typer
