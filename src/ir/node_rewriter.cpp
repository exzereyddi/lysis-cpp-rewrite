#include "node_rewriter.hpp"
#include "types.hpp"

#include <stdexcept>
#include <string>

namespace lysis {

    void NodeRewriter::rewrite() {
        for (size_t i = 0; i < graph_.numBlocks(); i++) rewriteBlock(graph_.blocks(i));
    }

    void NodeRewriter::rewriteBlock(NodeBlock* block) {
        current_ = block;
        NodeList::iterator it = block->nodes().begin();
        iterator_ = &it;
        while (it.more()) {
            it.node()->accept(*this);
            it.next();
        }
        iterator_ = nullptr;
    }

    void NodeRewriter::visit(DSysReq& sysreq) {
        if (sysreq.numOperands() == 1
            && sysreq.nativeX()
            && sysreq.nativeX()->name() == "__FLOAT_NOT__") {
            sysreq.getOperand(0)->addType(TypeUnit(PawnType(CellType::Float)));
            auto* unary = graph_.newNode<DUnary>(SPOpcode::not_, sysreq.getOperand(0));
            sysreq.replaceAllUsesWith(unary);
            sysreq.removeFromUseChains();
            current_->replace(*iterator_, unary);
            return;
        }

        if (sysreq.numOperands() != 2 || !sysreq.nativeX()) return;

        SPOpcode spop;
        const std::string& name = sysreq.nativeX()->name();
        if (name == "FloatAdd")     spop = SPOpcode::add;
        else if (name == "FloatSub")     spop = SPOpcode::sub;
        else if (name == "FloatMul")     spop = SPOpcode::smul;
        else if (name == "FloatDiv")     spop = SPOpcode::sdiv_alt;
        else if (name == "__FLOAT_GT__") spop = SPOpcode::sgrtr;
        else if (name == "__FLOAT_GE__") spop = SPOpcode::sgeq;
        else if (name == "__FLOAT_LT__") spop = SPOpcode::sless;
        else if (name == "__FLOAT_LE__") spop = SPOpcode::sleq;
        else if (name == "__FLOAT_EQ__") spop = SPOpcode::eq;
        else if (name == "__FLOAT_NE__") spop = SPOpcode::neq;
        else return;

        DNode* lhs, * rhs;
        if ((spop == SPOpcode::add || spop == SPOpcode::smul)
            && sysreq.getOperand(0)->type() == NodeType::DeclareLocal) {
            lhs = sysreq.getOperand(1); rhs = sysreq.getOperand(0);
        }
        else {
            lhs = sysreq.getOperand(0); rhs = sysreq.getOperand(1);
        }

        auto* binary = graph_.newNode<DBinary>(spop, lhs, rhs);
        sysreq.getOperand(0)->addType(TypeUnit(PawnType(CellType::Float)));
        sysreq.getOperand(1)->addType(TypeUnit(PawnType(CellType::Float)));
        sysreq.replaceAllUsesWith(binary);
        sysreq.removeFromUseChains();
        current_->replace(*iterator_, binary);
    }

    void NodeRewriter::visit(DPhi& phi) {
        NodeBlock* idom = graph_.blocks((size_t)phi.block()->lir()->idom()->id());
        auto* name = graph_.newNode<DTempName>(graph_.tempName());
        idom->prepend(name);

        for (size_t i = 0; i < phi.numOperands(); i++) {
            auto* store = graph_.newNode<DStore>(name, phi.getOperand(i));
            NodeBlock* pred = graph_.blocks((size_t)phi.block()->lir()->getPredecessor(i)->id());
            pred->prepend(store);
        }
        phi.replaceAllUsesWith(name);
    }

    void NodeRewriter::visit(DArrayRef&) {}

    void NodeRewriter::visit(DCall& call) {
        if (!call.function()) return;
        const std::string& fname = call.function()->name();
        if (fname.size() < 8 || fname.compare(0, 8, "operator") != 0) return;

        std::string op;
        for (size_t i = 8; i < fname.size(); i++) {
            if (fname[i] == '(') break;
            op += fname[i];
        }

        SPOpcode spop;
        if (call.numOperands() == 2) {
            if (op == ">")  spop = SPOpcode::sgrtr;
            else if (op == ">=") spop = SPOpcode::sgeq;
            else if (op == "<")  spop = SPOpcode::sless;
            else if (op == "<=") spop = SPOpcode::sleq;
            else if (op == "==") spop = SPOpcode::eq;
            else if (op == "!=") spop = SPOpcode::neq;
            else if (op == "+")  spop = SPOpcode::add;
            else if (op == "-")  spop = SPOpcode::sub;
            else if (op == "*")  spop = SPOpcode::smul;
            else if (op == "/")  spop = SPOpcode::sdiv_alt;
            else throw std::runtime_error("unknown operator (" + op + ")");
        }
        else {
            if (op == "-")  spop = SPOpcode::neg;
            else if (op == "!")  spop = SPOpcode::not_;
            else if (op == "++") spop = SPOpcode::inc;
            else if (op == "--") spop = SPOpcode::dec;
            else throw std::runtime_error("unknown operator (" + op + ")");
        }

        auto replaceWith = [&](DNode* rep) {
            call.replaceAllUsesWith(rep);
            call.removeFromUseChains();
            current_->replace(*iterator_, rep);
            };

        switch (spop) {
        case SPOpcode::sgeq: case SPOpcode::sleq: case SPOpcode::sgrtr: case SPOpcode::sless:
        case SPOpcode::eq:   case SPOpcode::neq:  case SPOpcode::add:
        case SPOpcode::sub:  case SPOpcode::smul: case SPOpcode::sdiv_alt:
            replaceWith(graph_.newNode<DBinary>(spop, call.getOperand(0), call.getOperand(1)));
            break;
        case SPOpcode::inc:
        case SPOpcode::dec: {
            auto* one = graph_.newNode<DFloat>(1.0f);
            replaceWith(graph_.newNode<DBinary>(
                spop == SPOpcode::inc ? SPOpcode::add : SPOpcode::sub, call.getOperand(0), one));
            break;
        }
        case SPOpcode::neg:
        case SPOpcode::not_:
            replaceWith(graph_.newNode<DUnary>(spop, call.getOperand(0)));
            break;
        default:
            throw std::runtime_error("unknown spop");
        }
    }

}