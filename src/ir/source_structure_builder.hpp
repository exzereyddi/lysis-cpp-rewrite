#pragma once

#include "nodes.hpp"
#include "structure.hpp"

#include <memory>
#include <stack>

namespace lysis {

    class SourceStructureBuilder {
    public:
        explicit SourceStructureBuilder(NodeGraph& graph) : graph_(graph) {}

        ControlBlock* build();
        ControlArena& arena() { return arena_; }

    private:
        NodeGraph& graph_;
        std::stack<NodeBlock*> joinStack_;
        ControlArena arena_;

        void pushScope(NodeBlock* block) { joinStack_.push(block); }
        NodeBlock* popScope() { NodeBlock* b = joinStack_.top(); joinStack_.pop(); return b; }
        bool isJoin(NodeBlock* block) const;

        static bool HasSharedTarget(NodeBlock* pred, DJumpCondition* jcc);
        static LogicOperator ToLogicOp(DJumpCondition* jcc);
        static NodeBlock* SingleTarget(NodeBlock* block);
        static void AssertInnerJoinValidity(NodeBlock* join, NodeBlock* earlyExit);

        struct BuildLogicChainResult {
            NodeBlock* join = nullptr;
            LogicChain* chain = nullptr;
        };
        BuildLogicChainResult buildLogicChain(NodeBlock* block, NodeBlock* earlyExitStop);

        NodeBlock* findJoinOfSimpleIf(NodeBlock* block, DJumpCondition* jcc);
        ControlBlock* traverseComplexIf(NodeBlock* block, DJumpCondition* jcc);
        ControlBlock* traverseIf(NodeBlock* block, DJumpCondition* jcc);

        struct LoopBodyResult {
            ControlType type = ControlType::WhileLoop;
            NodeBlock* join = nullptr;
            NodeBlock* body = nullptr;
            NodeBlock* cond = nullptr;
        };
        LoopBodyResult findLoopJoinAndBody(NodeBlock* header, NodeBlock* effectiveHeader);

        ControlBlock* traverseLoop(NodeBlock* block);
        ControlBlock* traverseSwitch(NodeBlock* block, DSwitch* switch_);
        ControlBlock* traverseJoin(NodeBlock* block);
        ControlBlock* traverseBlockNoLoop(NodeBlock* block);
        ControlBlock* traverseBlock(NodeBlock* block);
    };

}