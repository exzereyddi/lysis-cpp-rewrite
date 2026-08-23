#pragma once

#include "../core/pawn_file.hpp"
#include "../core/lstructure.hpp"
#include "nodes.hpp"
#include "instructions.hpp"

#include <memory>
#include <vector>

namespace lysis {

    class NodeBuilder {
    public:
        NodeBuilder(PawnFile* file, LGraph* graph);
        std::unique_ptr<NodeGraph> buildNodes();

    private:
        PawnFile* file_;
        LGraph* graph_;
        std::vector<std::unique_ptr<NodeBlock>> blocks_;
        NodeGraph* nodeGraph_ = nullptr;

        void traverse(NodeBlock* block);
    };

}