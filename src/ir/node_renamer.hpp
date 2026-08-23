#pragma once

#include "nodes.hpp"

namespace lysis {

    class NodeRenamer {
    public:
        explicit NodeRenamer(NodeGraph& graph) : graph_(graph) {}
        void rename();
    private:
        NodeGraph& graph_;
        void renameBlock(NodeBlock* block);
    };

}