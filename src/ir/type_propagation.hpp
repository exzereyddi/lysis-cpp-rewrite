#pragma once

#include "nodes.hpp"

namespace lysis {

    class ForwardTypePropagation : public NodeVisitor {
    public:
        explicit ForwardTypePropagation(NodeGraph& graph) : graph_(graph) {}
        void propagate();

        void visit(DConstant&)      override;
        void visit(DDeclareLocal&)  override;
        void visit(DLocalRef&)      override;
        void visit(DJump&)          override;
        void visit(DJumpCondition&) override;
        void visit(DSysReq&)        override;
        void visit(DBinary&)        override;
        void visit(DBoundsCheck&)   override;
        void visit(DArrayRef&)      override;
        void visit(DStore&)         override;
        void visit(DLoad&)          override;
        void visit(DReturn&)        override;
        void visit(DGlobal&)        override;
        void visit(DString&)        override;
        void visit(DCall&)          override;
        void visit(DGenArray&)      override;

    private:
        NodeGraph& graph_;
        NodeBlock* block_ = nullptr;
        void visitSignature(DNode* call, Signature* sig);
    };

    class BackwardTypePropagation : public NodeVisitor {
    public:
        explicit BackwardTypePropagation(NodeGraph& graph) : graph_(graph) {}
        void propagate();

        void visit(DConstant&)      override;
        void visit(DDeclareLocal&)  override;
        void visit(DLocalRef&)      override;
        void visit(DJump&)          override;
        void visit(DJumpCondition&) override;
        void visit(DSysReq&)        override;
        void visit(DCall&)          override;
        void visit(DBinary&)        override;
        void visit(DBoundsCheck&)   override;
        void visit(DArrayRef&)      override;
        void visit(DStore&)         override;
        void visit(DLoad&)          override;
        void visit(DReturn&)        override;
        void visit(DGlobal&)        override;
        void visit(DString&)        override;
        void visit(DGenArray&)      override;

    private:
        NodeGraph& graph_;
        NodeBlock* block_ = nullptr;

        void visitSignature(DNode* call, Signature* sig);
        void visitArgument(DNode* call, DNode* arg, size_t index);
        DNode* ConstantToReference(DConstant* node, const TypeUnit* tu);
        static void propagateInputs(DNode* lhs, DNode* rhs);
    };

}