#include "node_analysis.hpp"
#include "types.hpp"
#include "../core/pawn_file.hpp"

#include <cassert>

namespace lysis {

    namespace {

        bool IsArray(TypeSet* ts) {
            if (!ts || ts->numTypes() != 1) return false;
            try {
                const TypeUnit& tu = ts->types(0);
                if (tu.kind() == TypeUnit::Kind::Array) return true;
                if (tu.kind() == TypeUnit::Kind::Reference) {
                    const TypeUnit* inner = tu.inner();
                    if (inner && inner->kind() == TypeUnit::Kind::Array) return true;
                }
            }
            catch (...) {}
            return false;
        }

        DNode* GuessArrayBase(DNode* op1, DNode* op2) {
            if (!op1 || !op2) return nullptr;
            if (op1->usedAsArrayIndex()) return op2;
            if (op2->usedAsArrayIndex()) return op1;
            if (op1->type() == NodeType::ArrayRef || op1->type() == NodeType::LocalRef || IsArray(op1->typeSet())) return op1;
            if (op2->type() == NodeType::ArrayRef || op2->type() == NodeType::LocalRef || IsArray(op2->typeSet())) return op2;
            if (op1->type() == NodeType::Load) {
                auto* load = static_cast<DLoad*>(op1);
                if (load->from() && load->from()->type() == NodeType::ArrayRef) return op1;
            }
            if (op2->type() == NodeType::Load) {
                auto* load = static_cast<DLoad*>(op2);
                if (load->from() && load->from()->type() == NodeType::ArrayRef) return op2;
            }
            return nullptr;
        }

        bool IsReallyLikelyArrayCompute(DNode* node, DNode* abase) {
            if (abase->type() == NodeType::ArrayRef) return true;
            if (abase->type() == NodeType::Load) {
                auto* load = static_cast<DLoad*>(abase);
                if (load->from()->type() == NodeType::ArrayRef) return true;
            }
            if (IsArray(abase->typeSet())) return true;
            for (const DUse& use : node->uses()) {
                if (use.node()->type() == NodeType::Store || use.node()->type() == NodeType::Load) return true;
            }
            return false;
        }

        bool ShouldOperantsBeSwitched(DNode* abase, DNode* index, DBinary*) {
            if (abase->type() != NodeType::Load || index->type() != NodeType::Load) return false;
            return abase->getOperand(0) == index && index->getOperand(0)->type() == NodeType::DeclareLocal;
        }

        bool HasEnoughArrayDimensions(DNode* check, int num_dims, int depth = 0) {
            if (depth > 64) return true;
            if (!check) return false;
            switch (check->type()) {
            case NodeType::Load: {
                auto* load = static_cast<DLoad*>(check);
                if (!load->from()) return false;
                return HasEnoughArrayDimensions(load->from(), num_dims, depth + 1);
            }
            case NodeType::DeclareLocal: {
                auto* local = static_cast<DDeclareLocal*>(check);
                return local->var() && !local->var()->dims().empty() && (int)local->var()->dims().size() < num_dims;
            }
            case NodeType::Global: {
                auto* g = static_cast<DGlobal*>(check);
                return g->var() && !g->var()->dims().empty() && (int)g->var()->dims().size() < num_dims;
            }
            case NodeType::GenArray: {
                auto* ga = static_cast<DGenArray*>(check);
                return ga->var() && !ga->var()->dims().empty() && (int)ga->var()->dims().size() < num_dims;
            }
            case NodeType::Constant: {
                if (check->uses().size() == 1) {
                    DNode* u = check->uses().front().node();
                    if (u && u->type() == NodeType::Binary && u->getOperand(0))
                        return HasEnoughArrayDimensions(u->getOperand(0), num_dims, depth + 1);
                }
                return false;
            }
            default:
                return false;
            }
        }

        void RemoveDeadCodeInBlock(NodeBlock* block) {
            for (auto it = block->nodes().rbegin(); it.more(); ) {
                DNode* n = it.node();

                if (n->type() == NodeType::DeclareLocal) {
                    auto* decl = static_cast<DDeclareLocal*>(n);
                    if (!decl->var() && (decl->uses().empty() || (decl->uses().size() == 1 && decl->value()))) {
                        if (decl->uses().size() == 1) {
                            DUse& use = decl->uses().front();
                            bool keep = false;
                            if (decl->value()) {
                                if (decl->value()->type() == NodeType::Constant) keep = true;
                                else if (decl->value()->type() == NodeType::LocalRef
                                    && decl->value()->getOperand(0)
                                    && decl->value()->getOperand(0)->getOperand(0)
                                    && decl->value()->getOperand(0)->getOperand(0)->type() == NodeType::Constant)
                                    keep = true;
                            }
                            if (keep) { it.next(); continue; }
                            use.node()->replaceOperand((size_t)use.index(), decl->value());
                        }
                        n->removeFromUseChains();
                        block->nodes().remove(it);
                        continue;
                    }
                }

                if (n->type() == NodeType::Store
                    && n->getOperand(0)->type() == NodeType::Heap
                    && n->getOperand(0)->uses().size() == 1) {
                    n->removeFromUseChains();
                    block->nodes().remove(it);
                    continue;
                }

                if (!n->idempotent() || n->guard() || !n->uses().empty()) { it.next(); continue; }
                n->removeFromUseChains();
                block->nodes().remove(it);
            }
        }

        bool CollapseArrayReferencesInBlock(NodeBlock* block, NodeGraph& graph) {
            bool changed = false;

            for (auto it = block->nodes().rbegin(); it.more(); it.next()) {
                DNode* node = it.node();

                if (node->type() == NodeType::Store || node->type() == NodeType::Load) {
                    if (node->getOperand(0)
                        && node->getOperand(0)->type() != NodeType::ArrayRef
                        && IsArray(node->getOperand(0)->typeSet())) {
                        auto* index0 = graph.newNode<DConstant>(0);
                        auto* aref0 = graph.newNode<DArrayRef>(node->getOperand(0), index0, 0);
                        block->nodes().insertBefore(node, index0);
                        block->nodes().insertBefore(node, aref0);
                        node->replaceOperand(0, aref0);
                        changed = true;
                        continue;
                    }
                }

                if (node->type() != NodeType::Binary) continue;
                auto* binary = static_cast<DBinary*>(node);
                if (binary->spop() != SPOpcode::add) continue;
                if (!binary->lhs() || !binary->rhs()) continue;

                DNode* abase = GuessArrayBase(binary->lhs(), binary->rhs());
                if (!abase) continue;
                DNode* index = (abase == binary->lhs()) ? binary->rhs() : binary->lhs();

                if (ShouldOperantsBeSwitched(abase, index, binary)) {
                    abase = binary->rhs();
                    index = binary->lhs();
                }

                if (!IsReallyLikelyArrayCompute(binary, abase)) continue;

                if (abase->type() == NodeType::Load) {
                    auto* load = static_cast<DLoad*>(abase);
                    if (load->from()->type() == NodeType::ArrayRef
                        && binary->uses().size() == 1
                        && index->type() == NodeType::Constant
                        && binary->uses().front().node()->type() == NodeType::Store) {
                        auto* store = static_cast<DStore*>(binary->uses().front().node());
                        if (store->rhs() == binary) continue;
                    }
                }

                int num_dims = 1;
                DNode* check = abase;
                while (check->type() == NodeType::Load) {
                    auto* load = static_cast<DLoad*>(check);
                    if (load->from()->type() != NodeType::ArrayRef) break;
                    num_dims++;
                    check = static_cast<DArrayRef*>(load->from())->lhs();
                }
                if (HasEnoughArrayDimensions(check, num_dims)) continue;

                if (index->type() == NodeType::Load && index->getOperand(0) == abase) {
                    node->replaceAllUsesWith(index);
                    node->removeFromUseChains();
                    block->nodes().remove(it);
                    changed = true;
                    continue;
                }

                if (index->type() == NodeType::Constant) index->setUsedAsArrayIndex();

                auto* aref = graph.newNode<DArrayRef>(abase, index);
                DNode* nodeToRemove = it.node();
                block->nodes().insertBefore(nodeToRemove, aref);
                nodeToRemove->replaceAllUsesWith(aref);
                nodeToRemove->removeFromUseChains();
                block->nodes().remove(it);
                changed = true;
            }
            return changed;
        }

        void CoalesceLoadStoresInBlock(NodeBlock* block) {
            for (auto it = block->nodes().rbegin(); it.more(); it.next()) {
                if (it.node()->type() != NodeType::Store) continue;
                auto* store = static_cast<DStore*>(it.node());

                DNode* coalesce = nullptr;
                if (store->rhs()->type() == NodeType::Binary) {
                    auto* rhs = static_cast<DBinary*>(store->rhs());
                    if (rhs->lhs()->type() == NodeType::Load) {
                        auto* load = static_cast<DLoad*>(rhs->lhs());
                        if (load->from() == store->lhs()) {
                            coalesce = rhs->rhs();
                        }
                        else if (load->from()->type() == NodeType::ArrayRef
                            && store->lhs()->type() == NodeType::Load) {
                            auto* aref = static_cast<DArrayRef*>(load->from());
                            load = static_cast<DLoad*>(store->lhs());
                            if (aref->abase() == load
                                && aref->index()->type() == NodeType::Constant
                                && static_cast<DConstant*>(aref->index())->value() == 0) {
                                coalesce = rhs->rhs();
                                store->replaceOperand(0, aref);
                            }
                        }
                    }
                    if (coalesce) store->makeStoreOp(rhs->spop());
                }
                else if (store->rhs()->type() == NodeType::Load
                    && store->rhs()->getOperand(0) == store->lhs()) {
                    if (store->prev() && store->prev()->type() == NodeType::IncDec
                        && store->prev()->getOperand(0) == store->rhs()) {
                        store->removeFromUseChains();
                        block->nodes().remove(it);
                        assert(it.node()->type() == NodeType::IncDec);
                        it.node()->replaceOperand(0, it.node()->getOperand(0)->getOperand(0));
                    }
                }

                if (coalesce) store->replaceOperand(1, coalesce);
            }
        }

        Signature* SignatureOf(DNode* node) {
            if (node->type() == NodeType::Call) return static_cast<DCall*>(node)->function();
            return static_cast<DSysReq*>(node)->nativeX();
        }

        bool AnalyzeHeapNode(NodeBlock* block, DHeap* node, NodeGraph& graph) {
            if (node->uses().size() == 2) {
                DUse& firstUse = node->uses().front();
                DUse& lastUse = node->uses().back();

                if ((lastUse.node()->type() == NodeType::Call || lastUse.node()->type() == NodeType::SysReq)
                    && firstUse.node()->type() == NodeType::Store && firstUse.index() == 0) {
                    lastUse.node()->replaceOperand((size_t)lastUse.index(), firstUse.node()->getOperand(1));
                    return true;
                }

                if ((lastUse.node()->type() == NodeType::Call || lastUse.node()->type() == NodeType::SysReq)
                    && firstUse.node()->type() == NodeType::MemCopy && firstUse.index() == 0) {
                    auto* memcopy = static_cast<DMemCopy*>(firstUse.node());
                    auto* cv = static_cast<DConstant*>(memcopy->from());
                    auto* ia = graph.newNode<DInlineArray>(cv->value(), memcopy->bytes());
                    block->nodes().insertAfter(node, ia);
                    lastUse.node()->replaceOperand((size_t)lastUse.index(), ia);
                    Signature* sig = SignatureOf(lastUse.node());
                    if (!sig->args().empty() && (size_t)lastUse.index() < sig->args().size()) {
                        auto tu = TypeUnit::FromArgument(sig->args()[(size_t)lastUse.index()]);
                        if (tu) ia->addType(*tu);
                    }
                    return true;
                }

                if (lastUse.node()->type() == NodeType::Store
                    && firstUse.node()->type() == NodeType::Phi
                    && firstUse.node()->numOperands() == 2
                    && node->next() && node->next()->type() == NodeType::Call) {
                    lastUse.node()->replaceOperand((size_t)lastUse.index(), node->next());
                    return true;
                }
            }
            else if (node->uses().size() == 1) {
                DUse& use = node->uses().back();
                if (use.node()->type() == NodeType::SysReq || use.node()->type() == NodeType::Call) {
                    DNode* next = node;
                    while (next != use.node()) {
                        if (next->type() == NodeType::Call) break;
                        next = next->next();
                    }
                    if (next->type() == NodeType::Call) {
                        auto* call = static_cast<DCall*>(next);
                        if (call->function() && call->function()->isStringReturn()) {
                            use.node()->replaceOperand((size_t)use.index(), call);
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        void AnalyzeHeapUsageInBlock(NodeBlock* block, NodeGraph& graph) {
            for (auto it = block->nodes().rbegin(); it.more(); it.next()) {
                if (it.node()->type() == NodeType::Heap) {
                    if (AnalyzeHeapNode(block, static_cast<DHeap*>(it.node()), graph))
                        block->nodes().remove(it);
                }
            }
        }

        Variable* lookupVarByAddr(NodeGraph& graph, DConstant* con) {
            Variable* var = graph.file()->lookupGlobal(con->value());
            if (!var) var = graph.file()->lookupVariable(con->pc(), con->value(), Scope::Static);
            return var;
        }

        void insertGlobalChain(NodeBlock* block, NodeGraph& graph, DNode* insertAfter,
            Variable* var, int64_t pc,
            DGlobal*& outGlobal, DLoad*& outLoad, DDeclareLocal*& outLocal) {
            outGlobal = graph.newNode<DGlobal>(var);
            outLoad = graph.newNode<DLoad>(outGlobal);
            outLocal = graph.newNode<DDeclareLocal>(pc, outLoad);
            block->nodes().insertAfter(insertAfter, outGlobal);
            block->nodes().insertAfter(outGlobal, outLoad);
            block->nodes().insertAfter(outLoad, outLocal);
        }

    } // namespace

    namespace NodeAnalysis {

        void RemoveGuards(NodeGraph& graph) {
            for (int i = (int)graph.numBlocks() - 1; i >= 0; i--) {
                NodeBlock* block = graph.blocks((size_t)i);
                for (auto it = block->nodes().rbegin(); it.more(); ) {
                    if (it.node()->guard()) {
                        assert(it.node()->idempotent());
                        it.node()->removeFromUseChains();
                        block->nodes().remove(it);
                        continue;
                    }
                    it.next();
                }
            }
        }

        void RemoveDeadCode(NodeGraph& graph) {
            for (int i = (int)graph.numBlocks() - 1; i >= 0; i--)
                RemoveDeadCodeInBlock(graph.blocks((size_t)i));
        }

        void CollapseArrayReferences(NodeGraph& graph) {
            bool changed;
            int iter = 0;
            do {
                changed = false;
                for (int i = (int)graph.numBlocks() - 1; i >= 0; i--)
                    changed |= CollapseArrayReferencesInBlock(graph.blocks((size_t)i), graph);
                if (++iter > 100) break;
            } while (changed);
        }

        void CoalesceLoadsAndDeclarations(NodeGraph& graph) {
            for (size_t i = 0; i < graph.numBlocks(); i++) {
                NodeBlock* block = graph.blocks(i);
                for (auto it = block->nodes().begin(); it.more(); ) {
                    DNode* n = it.node();
                    if (n->type() == NodeType::DeclareLocal) {
                        auto* local = static_cast<DDeclareLocal*>(n);
                        if (n->next() && n->next()->type() == NodeType::Store) {
                            auto* store = static_cast<DStore*>(n->next());
                            if (store->getOperand(0) == local
                                && store->getOperand(1)->type() != NodeType::Unary) {
                                DNode* replacement = (store->spop() != SPOpcode::nop)
                                    ? (DNode*)graph.newNode<DBinary>(store->spop(), local->getOperand(0), store->getOperand(1))
                                    : store->getOperand(1);
                                local->replaceOperand(0, replacement);
                                store->removeFromUseChains();
                                it.next();
                                block->nodes().remove(it);
                                continue;
                            }
                        }
                    }
                    it.next();
                }
            }
        }

        void CoalesceLoadStores(NodeGraph& graph) {
            for (size_t i = 0; i < graph.numBlocks(); i++)
                CoalesceLoadStoresInBlock(graph.blocks(i));
        }

        void HandleMemCopys(NodeGraph& graph) {
            for (size_t i = 0; i < graph.numBlocks(); i++) {
                NodeBlock* block = graph.blocks(i);
                for (auto it = block->nodes().begin(); it.more(); it.next()) {
                    DNode* node = it.node();
                    if (node->type() != NodeType::MemCopy) continue;
                    auto* mcpy = static_cast<DMemCopy*>(node);
                    NodeType tf = mcpy->from()->type();
                    NodeType tt = mcpy->to()->type();

                    if (tf == NodeType::Load && tt == NodeType::DeclareLocal) {
                        auto* load = static_cast<DLoad*>(mcpy->from());
                        auto* local = static_cast<DDeclareLocal*>(mcpy->to());
                        if (local->var() && local->var()->type() == VariableType::Array)
                            block->nodes().insertAfter(node, graph.newNode<DStore>(local, load));
                    }
                    else if (tf == NodeType::ArrayRef && tt == NodeType::Load) {
                        auto* arrayref = static_cast<DArrayRef*>(mcpy->from());
                        auto* load = static_cast<DLoad*>(mcpy->to());
                        block->nodes().insertAfter(node, graph.newNode<DStore>(load, arrayref));
                    }
                    else if (tf == NodeType::DeclareLocal && tt == NodeType::ArrayRef) {
                        auto* local = static_cast<DDeclareLocal*>(mcpy->from());
                        auto* arrayref = static_cast<DArrayRef*>(mcpy->to());
                        block->nodes().insertAfter(node, graph.newNode<DStore>(local, arrayref));
                    }
                    else if (tf == NodeType::DeclareLocal && tt == NodeType::DeclareLocal) {
                        auto* lfrom = static_cast<DDeclareLocal*>(mcpy->from());
                        auto* lto = static_cast<DDeclareLocal*>(mcpy->to());
                        if (lfrom->var() && lfrom->var()->type() == VariableType::Array
                            && lto->var() && lto->var()->type() == VariableType::Array) {
                            block->nodes().insertAfter(node, graph.newNode<DStore>(lto, lfrom));
                        }
                    }
                    else if (tf == NodeType::Constant && tt == NodeType::Constant) {
                        auto* cfrom = static_cast<DConstant*>(mcpy->from());
                        auto* cto = static_cast<DConstant*>(mcpy->to());
                        if (cfrom->value() <= 0 || cto->value() <= 0) continue;
                        Variable* gfrom = lookupVarByAddr(graph, cfrom);
                        Variable* gto = lookupVarByAddr(graph, cto);
                        if (!gfrom || !gto) continue;

                        DGlobal* df; DLoad* lf; DDeclareLocal* delf;
                        insertGlobalChain(block, graph, node, gfrom, cfrom->pc(), df, lf, delf);
                        DGlobal* dt; DLoad* lt; DDeclareLocal* delt;
                        insertGlobalChain(block, graph, delf, gto, cto->pc(), dt, lt, delt);
                        block->nodes().insertAfter(delt, graph.newNode<DStore>(delt, delf));
                    }
                    else if ((tf == NodeType::Constant && tt == NodeType::DeclareLocal)
                        || (tf == NodeType::DeclareLocal && tt == NodeType::Constant)) {
                        DConstant* con;
                        DDeclareLocal* local;
                        bool fromIsConst = (tf == NodeType::Constant);
                        if (fromIsConst) {
                            con = static_cast<DConstant*>(mcpy->from());
                            local = static_cast<DDeclareLocal*>(mcpy->to());
                        }
                        else {
                            con = static_cast<DConstant*>(mcpy->to());
                            local = static_cast<DDeclareLocal*>(mcpy->from());
                        }

                        if (con->value() > 0 && local->var()
                            && local->var()->type() == VariableType::Array) {
                            Variable* global = lookupVarByAddr(graph, con);

                            if (!global) {
                                auto* ia = graph.newNode<DInlineArray>(con->value(), mcpy->bytes());
                                block->nodes().insertAfter(node, ia);
                                auto tu = TypeUnit::FromVariable(local->var());
                                if (tu) ia->addType(*tu);
                                if (local->block()->lir()->id() == con->block()->lir()->id()) {
                                    local->replaceOperand(0, ia);
                                }
                                else {
                                    block->nodes().insertAfter(ia, graph.newNode<DStore>(local, ia));
                                }
                            }
                            else {
                                DGlobal* dg; DLoad* ld; DDeclareLocal* dl;
                                insertGlobalChain(block, graph, node, global, con->pc(), dg, ld, dl);
                                DNode* lhs = fromIsConst ? (DNode*)local : (DNode*)dl;
                                DNode* rhs = fromIsConst ? (DNode*)dl : (DNode*)local;
                                block->nodes().insertAfter(dl, graph.newNode<DStore>(lhs, rhs));
                            }
                        }
                    }
                }
            }
        }

        void AnalyzeHeapUsage(NodeGraph& graph) {
            for (size_t i = 0; i < graph.numBlocks(); i++)
                AnalyzeHeapUsageInBlock(graph.blocks(i), graph);
        }

    }
}