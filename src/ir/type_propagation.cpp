#include "type_propagation.hpp"
#include "types.hpp"
#include "../core/pawn_file.hpp"
#include "../frontend/sourcepawn.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace lysis {

    void ForwardTypePropagation::propagate() {
        for (size_t i = 0; i < graph_.numBlocks(); i++) {
            block_ = graph_.blocks(i);
            for (auto it = block_->nodes().begin(); it.more(); it.next())
                it.node()->accept(*this);
        }
    }

    void ForwardTypePropagation::visit(DConstant&) {}
    void ForwardTypePropagation::visit(DJump&) {}
    void ForwardTypePropagation::visit(DReturn&) {}
    void ForwardTypePropagation::visit(DString&) {}
    void ForwardTypePropagation::visit(DBinary&) {}

    void ForwardTypePropagation::visit(DDeclareLocal& local) {
        Variable* var = graph_.file()->lookupVariable(local.pc(), local.offset());
        local.setVariable(var);
        if (!var) return;

        auto tu = TypeUnit::FromVariable(var);
        if (!tu) return;
        local.addType(*tu);

        if (local.value() && local.value()->type() == NodeType::Constant && var->isFloat()) {
            if (var->tag())      local.value()->addType(TypeUnit::FromTag(var->tag()));
            if (var->rttiType()) local.value()->addType(TypeUnit::FromType(var->rttiType()));
        }
    }

    void ForwardTypePropagation::visit(DLocalRef& lref) {
        if (TypeSet* ts = lref.local()->typeSet()) lref.addTypes(*ts);
    }

    void ForwardTypePropagation::visit(DJumpCondition& jcc) {
        jcc.typeSet()->addType(TypeUnit(PawnType(CellType::Bool)));
        (void)jcc;
    }

    void ForwardTypePropagation::visitSignature(DNode* call, Signature* sig) {
        if (sig->args().empty()) return;

        size_t formatIndex;
        if (call->type() == NodeType::SysReq) {
            if (sig->args().size() < 2) return;
            if (sig->args().back().type() != VariableType::Variadic) return;
            const Argument& fa = sig->args()[sig->args().size() - 2];
            if (!fa.isString()) return;
            if (call->numOperands() == (sig->args().size() - 2)) return;
            formatIndex = sig->args().size() - 2;
        }
        else {
            if (sig->args().empty() || call->numOperands() <= sig->args().size()) return;
            if (!sig->args().back().isString()) return;
            formatIndex = sig->args().size() - 1;
        }

        DNode* formatNode = call->getOperand(formatIndex);
        if (formatNode->type() != NodeType::DeclareLocal
            || formatNode->getOperand(0)->type() != NodeType::String) return;

        std::string fs = static_cast<DString*>(formatNode->getOperand(0))->value();
        {
            std::string s; s.reserve(fs.size());
            for (size_t i = 0; i < fs.size(); i++) {
                if (i + 1 < fs.size() && fs[i] == '%' && fs[i + 1] == '%') i++;
                else s += fs[i];
            }
            fs = std::move(s);
        }

        std::vector<std::string> parts;
        {
            std::string cur;
            for (char c : fs) {
                if (c == '%') { parts.push_back(cur); cur.clear(); }
                else cur += c;
            }
            parts.push_back(cur);
        }

        size_t argumentIndex = formatIndex + 1;
        for (size_t i = 1; i < parts.size() && argumentIndex < call->numOperands(); i++) {
            if (parts[i].empty()) { argumentIndex++; continue; }
            char c = parts[i][0];
            DNode* fa = call->getOperand(argumentIndex);
            switch (c) {
            case 'f':
                if (fa->typeSet()->numTypes() == 0)
                    fa->addType(TypeUnit(PawnType(CellType::Float)));
                break;
            case 's':
            case 't':
            case 'T': {
                if (c == 'T') argumentIndex++;
                if (fa->typeSet()->numTypes() != 0) break;
                if (fa->type() == NodeType::Heap) break;
                fa->addType(TypeUnit(PawnType(sig->args()[formatIndex].tag()), 1));
                break;
            }
            case 'c':
                if (fa->typeSet()->numTypes() == 0)
                    fa->addType(TypeUnit(PawnType(CellType::Character)));
                break;
            }
            argumentIndex++;
        }
    }

    void ForwardTypePropagation::visit(DSysReq& sysreq) { visitSignature(&sysreq, sysreq.nativeX()); }
    void ForwardTypePropagation::visit(DCall& call) { if (call.function()) visitSignature(&call, call.function()); }

    void ForwardTypePropagation::visit(DBoundsCheck& c) { c.getOperand(0)->setUsedAsArrayIndex(); }

    void ForwardTypePropagation::visit(DArrayRef& aref) {
        if (TypeSet* ts = aref.abase()->typeSet())
            for (size_t i = 0; i < ts->numTypes(); i++) aref.addType(ts->types(i));
    }

    void ForwardTypePropagation::visit(DStore& store) {
        if (store.getOperand(0)->typeSet()->numTypes() == 1
            && store.getOperand(1)->type() == NodeType::Constant) {
            const TypeUnit& tu = store.getOperand(0)->typeSet()->types(0);
            if (tu.kind() == TypeUnit::Kind::Cell)
                store.getOperand(1)->addType(TypeUnit(PawnType(tu.type().type())));
            else if (tu.kind() == TypeUnit::Kind::Reference && tu.inner())
                store.getOperand(1)->addType(TypeUnit(PawnType(tu.inner()->type().type())));
        }
    }

    void ForwardTypePropagation::visit(DLoad& load) {
        TypeSet* ts = load.from()->typeSet();
        if (!ts) return;
        for (size_t i = 0; i < ts->numTypes(); i++) {
            auto actual = ts->types(i).load();
            load.addType(actual ? *actual : ts->types(i));
        }
    }

    void ForwardTypePropagation::visit(DGlobal& g) {
        if (!g.var()) return;
        if (auto tu = TypeUnit::FromVariable(g.var())) g.addType(*tu);
    }

    void ForwardTypePropagation::visit(DGenArray& ga) {
        Variable* var = graph_.file()->lookupVariable(ga.pc(), ga.offset());
        ga.setVariable(var);
        for (size_t i = 0; i < ga.numOperands(); i++) ga.getOperand(i)->accept(*this);
        if (var)
            if (auto tu = TypeUnit::FromVariable(var)) ga.addType(*tu);
    }

    void BackwardTypePropagation::propagate() {
        for (int i = (int)graph_.numBlocks() - 1; i >= 0; i--) {
            block_ = graph_.blocks((size_t)i);
            for (auto it = block_->nodes().rbegin(); it.more(); it.next())
                it.node()->accept(*this);
        }
    }

    void BackwardTypePropagation::propagateInputs(DNode* lhs, DNode* rhs) {
        lhs->typeSet()->addTypes(*rhs->typeSet());
        rhs->typeSet()->addTypes(*lhs->typeSet());
    }

    DNode* BackwardTypePropagation::ConstantToReference(DConstant* node, const TypeUnit* tu) {
        Variable* global = graph_.file()->lookupGlobal(node->value());
        if (!global) global = graph_.file()->lookupVariable(node->pc(), node->value(), Scope::Static);
        if (global) return graph_.newNode<DGlobal>(global);
        if (tu && tu->isString())
            return graph_.newNode<DString>(graph_.file()->stringFromData(node->value()));
        return nullptr;
    }

    void BackwardTypePropagation::visit(DConstant& node) {
        DNode* replacement = nullptr;
        if (node.typeSet()->numTypes() == 1) {
            const TypeUnit& tu = node.typeSet()->types(0);
            switch (tu.kind()) {
            case TypeUnit::Kind::Cell: {
                switch (tu.type().type()) {
                case CellType::Bool:
                    replacement = graph_.newNode<DBoolean>(node.value() != 0);
                    break;
                case CellType::Character:
                    replacement = graph_.newNode<DCharacter>((char)node.value());
                    break;
                case CellType::Float: {
                    uint32_t bits = (uint32_t)node.value();
                    float v;
                    std::memcpy(&v, &bits, sizeof(v));
                    replacement = graph_.newNode<DFloat>(v);
                    break;
                }
                case CellType::Function: {
                    if (node.value() < 0) break;
                    auto& publics = graph_.file()->publics();
                    size_t idx = (size_t)(node.value() >> 1);
                    if (idx >= publics.size()) break;
                    Public* p = publics[idx].get();
                    Function* fn = graph_.file()->lookupFunction(p->address());
                    replacement = graph_.newNode<DFunction>(p->address(), fn);
                    break;
                }
                default: return;
                }
                break;
            }
            case TypeUnit::Kind::Array:
                replacement = ConstantToReference(&node, &tu);
                break;
            default: return;
            }
        }

        if (!replacement && node.usedAsReference())
            replacement = ConstantToReference(&node, nullptr);

        if (replacement) {
            block_->nodes().insertAfter(&node, replacement);
            node.replaceAllUsesWith(replacement);
        }
    }

    void BackwardTypePropagation::visit(DDeclareLocal& local) {
        if (local.value() && !local.var()) local.value()->addTypes(*local.typeSet());
    }

    void BackwardTypePropagation::visit(DLocalRef& lref) { lref.addTypes(*lref.local()->typeSet()); }
    void BackwardTypePropagation::visit(DJump&) {}

    void BackwardTypePropagation::visit(DJumpCondition& jcc) {
        if (jcc.getOperand(0)->type() == NodeType::Binary) {
            auto* bin = static_cast<DBinary*>(jcc.getOperand(0));
            propagateInputs(bin->lhs(), bin->rhs());
        }
    }

    void BackwardTypePropagation::visitSignature(DNode* call, Signature* sig) {
        if (sig->args().empty()) {
            for (size_t i = 0; i < call->numOperands(); i++)
                visitArgument(call, call->getOperand(i), i);
            return;
        }

        for (size_t i = 0; i < call->numOperands() && i < sig->args().size(); i++) {
            DNode* node = call->getOperand(i);
            const Argument& arg = (i < sig->args().size()) ? sig->args()[i] : sig->args().back();

            if (arg.generated() ||
                (arg.type() == VariableType::ArrayReference
                    && !arg.dimensions().empty() && arg.dimensions().size() == 1
                    && arg.tag() && arg.tag()->name() == "_")) {
                if (node->type() == NodeType::Constant) {
                    auto* cn = static_cast<DConstant*>(node);
                    if (graph_.file()->IsMaybeString(cn->value())) {
                        call->replaceOperand(i, graph_.newNode<DString>(graph_.file()->stringFromData(cn->value())));
                        continue;
                    }
                }
                else if (node->type() == NodeType::DeclareLocal) {
                    auto* ln = static_cast<DDeclareLocal*>(node);
                    if (ln->value() && ln->value()->type() == NodeType::Constant) {
                        auto* cn = static_cast<DConstant*>(ln->value());
                        if (cn->value() > 0 && graph_.file()->IsMaybeString(cn->value())) {
                            call->replaceOperand(i, graph_.newNode<DString>(graph_.file()->stringFromData(cn->value())));
                            continue;
                        }
                    }
                }
            }

            if (arg.type() == VariableType::Reference
                && node->type() == NodeType::DeclareLocal
                && node->getOperand(0) && node->getOperand(0)->type() == NodeType::Constant) {
                auto* ln = static_cast<DDeclareLocal*>(node);
                auto* cn = static_cast<DConstant*>(ln->getOperand(0));
                Variable* g = graph_.file()->lookupGlobal(cn->value());
                if (!g) g = graph_.file()->lookupVariable(ln->pc(), cn->value(), Scope::Static);
                if (g) {
                    call->replaceOperand(i, graph_.newNode<DGlobal>(g));
                    node = call->getOperand(i);
                }
            }

            auto tu = TypeUnit::FromArgument(arg);
            if (!tu) continue;

            if (tu->kind() == TypeUnit::Kind::Cell
                && tu->type().type() == CellType::Function
                && node->type() == NodeType::DeclareLocal
                && node->getOperand(0) && node->getOperand(0)->type() == NodeType::Constant
                && static_cast<DConstant*>(node->getOperand(0))->value() < 0) {
                node->addType(TypeUnit(PawnType(CellType::Tag, arg.tag())));
            }
            else {
                node->addType(*tu);
            }
        }

        if (!sig->args().empty()
            && (sig->args().back().type() == VariableType::Variadic
                || sig->args().size() < call->numOperands())) {
            for (size_t i = sig->args().size() - 1; i < call->numOperands(); i++)
                visitArgument(call, call->getOperand(i), i);
        }
    }

    void BackwardTypePropagation::visitArgument(DNode* call, DNode* arg, size_t index) {
        switch (arg->type()) {
        case NodeType::DeclareLocal: {
            auto* ln = static_cast<DDeclareLocal*>(arg);
            if (ln->value() && ln->value()->type() == NodeType::Constant) {
                auto* cn = static_cast<DConstant*>(ln->value());
                Variable* g = graph_.file()->lookupGlobal(cn->value());
                if (!g) g = graph_.file()->lookupVariable(ln->pc(), cn->value(), Scope::Static);
                if (g) {
                    if (!g->isStateVariable())
                        call->replaceOperand(index, graph_.newNode<DGlobal>(g));
                    return;
                }
                if (graph_.file()->IsMaybeString(cn->value()))
                    call->replaceOperand(index, graph_.newNode<DString>(graph_.file()->stringFromData(cn->value())));
            }
            if (!ln->var()) return;
            if (auto tu = TypeUnit::FromVariable(ln->var())) arg->addType(*tu);
            break;
        }
        case NodeType::DeclareStatic: {
            auto* sn = static_cast<DDeclareStatic*>(arg);
            if (!sn->var()) return;
            if (auto tu = TypeUnit::FromVariable(sn->var())) arg->addType(*tu);
            break;
        }
        case NodeType::Constant: {
            auto* cn = static_cast<DConstant*>(arg);
            if (graph_.file()->IsMaybeString(cn->value()))
                call->replaceOperand(index, graph_.newNode<DString>(graph_.file()->stringFromData(cn->value())));
            break;
        }
        default: break;
        }
    }

    void BackwardTypePropagation::visit(DCall& call) { if (call.function()) visitSignature(&call, call.function()); }
    void BackwardTypePropagation::visit(DSysReq& s) { visitSignature(&s, s.nativeX()); }

    void BackwardTypePropagation::visit(DBinary& binary) {
        if (binary.spop() == SPOpcode::add && binary.usedAsReference())
            binary.lhs()->setUsedAsReference();

        if (binary.uses().size() != 1) return;

        DDeclareLocal* local = nullptr;
        if (binary.lhs()->type() == NodeType::DeclareLocal)      local = static_cast<DDeclareLocal*>(binary.lhs());
        else if (binary.rhs()->type() == NodeType::DeclareLocal) local = static_cast<DDeclareLocal*>(binary.rhs());
        else return;

        if (local->value() && local->value()->type() == NodeType::Constant) {
            auto* con = static_cast<DConstant*>(local->value());
            if (con->value() < 1000000000) return;
            if (con->typeSet() && con->typeSet()->numTypes() > 0) return;

            DNode* use = binary.uses().front().node();
            if (use->type() != NodeType::Store) return;
            auto* store = static_cast<DStore*>(use);
            if (store->lhs()->typeSet()->numTypes() != 1) return;

            const TypeUnit& unit = store->lhs()->typeSet()->types(0);
            if (unit.kind() == TypeUnit::Kind::Reference
                && unit.inner() && unit.inner()->type().type() == CellType::Float) {
                con->addType(TypeUnit(PawnType(CellType::Float)));
            }
        }
    }

    void BackwardTypePropagation::visit(DBoundsCheck&) {}
    void BackwardTypePropagation::visit(DArrayRef& aref) { aref.abase()->setUsedAsReference(); }
    void BackwardTypePropagation::visit(DStore& store) { store.getOperand(0)->setUsedAsReference(); }

    void BackwardTypePropagation::visit(DLoad& load) {
        load.from()->setUsedAsReference();
        if (!load.from()->typeSet() || load.from()->typeSet()->numTypes() != 1) return;

        const TypeUnit& tu = load.from()->typeSet()->types(0);
        if (tu.kind() != TypeUnit::Kind::Array) return;

        if (load.from()->type() == NodeType::ArrayRef) {
            auto* aref = static_cast<DArrayRef*>(load.from());
            if (aref->abase()->type() == NodeType::Global) return;
        }
        auto* cv = graph_.newNode<DConstant>(0);
        auto* aref = graph_.newNode<DArrayRef>(load.from(), cv, 1);
        block_->nodes().insertAfter(load.from(), cv);
        block_->nodes().insertAfter(cv, aref);
        load.replaceOperand(0, aref);
    }

    void BackwardTypePropagation::visit(DReturn& ret) {
        if (graph_.function())
            ret.getOperand(0)->typeSet()->addType(TypeUnit::FromFunction(graph_.function()));
    }

    void BackwardTypePropagation::visit(DGlobal&) {}
    void BackwardTypePropagation::visit(DString&) {}

    void BackwardTypePropagation::visit(DGenArray& ga) {
        for (size_t i = 0; i < ga.numOperands(); i++) ga.getOperand(i)->accept(*this);
    }

}