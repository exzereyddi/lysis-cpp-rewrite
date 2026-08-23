#pragma once

#include "nodes.hpp"

namespace lysis {

    class NodeRewriter : public NodeVisitor {
    public:
        explicit NodeRewriter(NodeGraph& graph) : graph_(graph) {}
        void rewrite();

        void visit(DSysReq& n)   override;
        void visit(DCall& n)     override;
        void visit(DPhi& n)      override;
        void visit(DArrayRef& n) override;

    private:
        NodeGraph& graph_;
        NodeBlock* current_ = nullptr;
        NodeList::iterator* iterator_ = nullptr;

        void rewriteBlock(NodeBlock* block);
    };

}