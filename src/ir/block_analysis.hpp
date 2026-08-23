#pragma once

#include "../core/lstructure.hpp"

#include <vector>

namespace lysis {

    class PawnFile;
    class NodeBlock;

    namespace BlockAnalysis {
        std::vector<LBlock*> Order(LBlock* entry);
        bool IsReducible(const std::vector<LBlock*>& blocks);
        void SplitCriticalEdges(std::vector<LBlock*>& blocks, LGraph& graph);
        void ComputeDominators(std::vector<LBlock*>& blocks);
        void ComputeImmediateDominators(std::vector<LBlock*>& blocks);
        void ComputeDominatorTree(std::vector<LBlock*>& blocks);
        void FindLoops(std::vector<LBlock*>& blocks);
        LBlock* FollowGoto(LBlock* block);
        LBlock* EnforceStackBalance(PawnFile* file, const std::vector<LBlock*>& blocks);

        NodeBlock* GetSingleTarget(NodeBlock* block);
        NodeBlock* GetEmptyTarget(NodeBlock* block);
        NodeBlock* EffectiveTargetNoLoop(NodeBlock* block);
        NodeBlock* EffectiveTarget(NodeBlock* block);
        NodeBlock* ConstantSettingTarget(NodeBlock* block);
    }

}