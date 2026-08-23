#include "block_analysis.hpp"
#include "instructions.hpp"
#include "nodes.hpp"
#include "../core/pawn_file.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <stack>
#include <vector>

namespace lysis {
    namespace BlockAnalysis {

        std::vector<LBlock*> Order(LBlock* entry) {
            std::stack<LBlock*> pending;
            std::stack<size_t>  successors;
            std::stack<LBlock*> done;

            LBlock* current = entry;
            size_t nextSuccessor = 0;

            while (true) {
                if (!current->marked()) {
                    current->mark();
                    if (nextSuccessor < current->numSuccessors()) {
                        pending.push(current);
                        successors.push(nextSuccessor);
                        current = current->getSuccessor(nextSuccessor);
                        nextSuccessor = 0;
                        continue;
                    }
                    done.push(current);
                }
                if (pending.empty()) break;
                current = pending.top(); pending.pop();
                current->unmark();
                nextSuccessor = successors.top() + 1;
                successors.pop();
            }

            std::vector<LBlock*> blocks;
            while (!done.empty()) {
                LBlock* b = done.top(); done.pop();
                b->unmark();
                b->setId((int32_t)blocks.size());
                blocks.push_back(b);
            }
            return blocks;
        }

        void SplitCriticalEdges(std::vector<LBlock*>& blocks, LGraph& graph) {
            for (LBlock* block : blocks) {
                if (block->numSuccessors() < 2) continue;
                for (size_t j = 0; j < block->numSuccessors(); j++) {
                    LBlock* target = block->getSuccessor(j);
                    if (target->numPredecessors() < 2) continue;

                    auto splitOwned = std::make_unique<LBlock>(block->pc());
                    LBlock* split = splitOwned.get();
                    std::vector<std::unique_ptr<LInstruction>> ins;
                    ins.push_back(std::make_unique<LGoto>(target));
                    split->setInstructions(std::move(ins));

                    block->replaceSuccessor(j, split);
                    target->replacePredecessor(block, split);
                    split->addPredecessor(block);
                    graph.owned_blocks.push_back(std::move(splitOwned));
                }
            }
        }

        namespace {
            struct RBlock {
                std::vector<RBlock*> predecessors;
                std::vector<RBlock*> successors;
            };

            bool contains(const std::vector<RBlock*>& v, RBlock* x) {
                return std::find(v.begin(), v.end(), x) != v.end();
            }
            void removeFrom(std::vector<RBlock*>& v, RBlock* x) {
                v.erase(std::remove(v.begin(), v.end(), x), v.end());
            }
        }

        bool IsReducible(const std::vector<LBlock*>& blocks) {
            std::vector<std::unique_ptr<RBlock>> owned;
            std::vector<RBlock*> rblocks;
            owned.reserve(blocks.size());
            rblocks.reserve(blocks.size());
            for (size_t i = 0; i < blocks.size(); i++) {
                owned.push_back(std::make_unique<RBlock>());
                rblocks.push_back(owned.back().get());
            }
            for (size_t i = 0; i < blocks.size(); i++) {
                LBlock* b = blocks[i];
                RBlock* rb = rblocks[i];
                for (size_t j = 0; j < b->numPredecessors(); j++)
                    rb->predecessors.push_back(rblocks[b->getPredecessor(j)->id()]);
                for (size_t j = 0; j < b->numSuccessors(); j++)
                    rb->successors.push_back(rblocks[b->getSuccessor(j)->id()]);
            }

            std::vector<RBlock*> queue(rblocks);
            while (true) {
                std::vector<RBlock*> deleteQueue;
                for (RBlock* rb : queue) {
                    if (contains(rb->predecessors, rb)) removeFrom(rb->predecessors, rb);
                    if (contains(rb->successors, rb))   removeFrom(rb->successors, rb);

                    if (rb->predecessors.size() == 1) {
                        deleteQueue.push_back(rb);
                        RBlock* pred = rb->predecessors[0];
                        removeFrom(pred->successors, rb);
                        for (RBlock* succ : rb->successors) {
                            assert(contains(succ->predecessors, rb));
                            removeFrom(succ->predecessors, rb);
                            if (!contains(succ->predecessors, pred)) succ->predecessors.push_back(pred);
                            if (!contains(pred->successors, succ))   pred->successors.push_back(succ);
                        }
                    }
                }
                if (deleteQueue.empty()) break;
                for (RBlock* rb : deleteQueue)
                    queue.erase(std::remove(queue.begin(), queue.end(), rb), queue.end());
            }
            return queue.size() == 1;
        }

        static bool compareBitSets(const std::vector<bool>& a, const std::vector<bool>& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) return false;
            return true;
        }

        void ComputeDominators(std::vector<LBlock*>& blocks) {
            size_t n = blocks.size();
            std::vector<std::vector<bool>> doms(n, std::vector<bool>(n, false));

            doms[0][0] = true;
            for (size_t i = 1; i < n; i++)
                for (size_t j = 0; j < n; j++) doms[i][j] = true;

            bool changed;
            do {
                changed = false;
                for (size_t i = 1; i < n; i++) {
                    LBlock* block = blocks[i];
                    for (size_t j = 0; j < block->numPredecessors(); j++) {
                        LBlock* pred = block->getPredecessor(j);
                        std::vector<bool> u = doms[i];
                        for (size_t k = 0; k < n; k++)
                            doms[block->id()][k] = doms[block->id()][k] && doms[pred->id()][k];
                        doms[block->id()][block->id()] = true;
                        if (!compareBitSets(doms[block->id()], u)) changed = true;
                    }
                }
            } while (changed);

            for (size_t i = 0; i < n; i++) {
                std::vector<LBlock*> list;
                for (size_t j = 0; j < n; j++) if (doms[i][j]) list.push_back(blocks[j]);
                blocks[i]->setDominators(std::move(list));
            }
        }

        static bool strictlyDominatesADominator(LBlock* from, LBlock* dom) {
            for (LBlock* other : from->dominators()) {
                if (other == from || other == dom) continue;
                for (LBlock* d : other->dominators()) if (d == dom) return true;
            }
            return false;
        }

        static void computeImmediateDominator(LBlock* block) {
            for (LBlock* dom : block->dominators()) {
                if (dom == block) continue;
                if (!strictlyDominatesADominator(block, dom)) { block->setImmediateDominator(dom); return; }
            }
            assert(false);
        }

        void ComputeImmediateDominators(std::vector<LBlock*>& blocks) {
            blocks[0]->setImmediateDominator(blocks[0]);
            for (size_t i = 1; i < blocks.size(); i++) computeImmediateDominator(blocks[i]);
        }

        void ComputeDominatorTree(std::vector<LBlock*>& blocks) {
            std::vector<std::vector<LBlock*>> idominated(blocks.size());
            for (size_t i = 1; i < blocks.size(); i++)
                idominated[blocks[i]->idom()->id()].push_back(blocks[i]);
            for (size_t i = 0; i < blocks.size(); i++)
                blocks[i]->setImmediateDominated(std::move(idominated[i]));
        }

        LBlock* FollowGoto(LBlock* block) {
            if (block->instructions().size() > 1) return block;
            LControlInstruction* last = block->last();
            if (last->op() != Opcode::Goto) return block;
            return FollowGoto(static_cast<LGoto*>(last)->target());
        }

        namespace {
            LBlock* skipContainedLoop(LBlock* block, LBlock* header) {
                while (block->loop() && block->loop() == block) {
                    if (block->loop()) block = block->loop();
                    if (block == header) break;
                    block = block->getLoopPredecessor();
                }
                return block;
            }

            class LoopBodyWorklist {
            public:
                explicit LoopBodyWorklist(LBlock* backedge) : backedge_(backedge) {}
                void scan(LBlock* block) {
                    for (size_t i = 0; i < block->numPredecessors(); i++) {
                        LBlock* pred = block->getPredecessor(i);
                        if (pred->loop() == backedge_->loop()) continue;
                        pred = skipContainedLoop(pred, backedge_->loop());
                        assert(!pred->loop() || pred->loop() == backedge_->loop());
                        if (pred->loop()) continue;
                        stack_.push_back(pred);
                    }
                }
                bool empty() const { return stack_.empty(); }
                LBlock* pop() { LBlock* b = stack_.back(); stack_.pop_back(); return b; }
            private:
                std::vector<LBlock*> stack_;
                LBlock* backedge_;
            };

            void markLoop(LBlock* backedge) {
                LoopBodyWorklist wl(backedge);
                wl.scan(backedge);
                while (!wl.empty()) {
                    LBlock* b = wl.pop();
                    wl.scan(b);
                    b->setInLoop(backedge->loop());
                }
            }
        }

        void FindLoops(std::vector<LBlock*>& blocks) {
            for (size_t i = 1; i < blocks.size(); i++) {
                LBlock* b = blocks[i];
                for (size_t j = 0; j < b->numSuccessors(); j++) {
                    LBlock* succ = b->getSuccessor(j);
                    if (succ->id() < b->id()) {
                        succ->setLoopHeader(b);
                        b->setInLoop(succ);
                        break;
                    }
                }
            }
            for (LBlock* b : blocks) if (b->backedge()) markLoop(b->backedge());
        }

        namespace {
            LBlock* findSkippingParent(const std::vector<LBlock*>& blocks, int unbalancedId, LBlock* block) {
                LBlock* idomBlock = blocks[block->idom()->id()];
                LControlInstruction* lastIns = idomBlock->last();
                if (lastIns->op() != Opcode::JumpCondition) return nullptr;

                auto* jcc = static_cast<LJumpCondition*>(lastIns);
                LBlock* target;
                if (jcc->trueTarget() == block)       target = jcc->falseTarget();
                else if (jcc->falseTarget() == block) target = jcc->trueTarget();
                else return findSkippingParent(blocks, unbalancedId, idomBlock);

                return (target->id() > unbalancedId) ? target : findSkippingParent(blocks, unbalancedId, idomBlock);
            }

            class StackBalanceValidator {
            public:
                StackBalanceValidator(PawnFile* file, const std::vector<LBlock*>& blocks)
                    : file_(file), stack_levels_(blocks.size(), 0) {
                }

                LBlock* findUnbalancedBlock(LBlock* block) {
                    for (size_t i = 0; i < block->numPredecessors(); i++) {
                        LBlock* pred = block->getPredecessor(i);
                        if (pred->id() >= block->id()) continue;
                        if (stack_levels_[block->id()] == 0)
                            stack_levels_[block->id()] = stack_levels_[pred->id()];
                    }

                    int64_t lastConstant = 0;
                    bool hasLastConstant = false;
                    int id = block->id();

                    for (auto& insPtr : block->instructions()) {
                        LInstruction* ins = insPtr.get();
                        switch (ins->op()) {
                        case Opcode::Stack: {
                            auto* stk = static_cast<LStack*>(ins);
                            stack_levels_[id] += (stk->amount() < 0)
                                ? (int)(stk->amount() / -4) : -(int)(stk->amount() / 4);
                            break;
                        }
                        case Opcode::PushConstant:
                            lastConstant = static_cast<LPushConstant*>(ins)->val();
                            hasLastConstant = true;
                            stack_levels_[id]++;
                            continue;
                        case Opcode::PushGlobal: case Opcode::PushLocal:
                        case Opcode::PushReg:    case Opcode::PushStackAddress:
                            stack_levels_[id]++;
                            break;
                        case Opcode::AddConstant:
                            lastConstant = static_cast<LAddConstant*>(ins)->amount();
                            hasLastConstant = true;
                            continue;
                        case Opcode::StoreCtrl:
                            assert(hasLastConstant);
                            stack_levels_[id] = (int)(lastConstant / -4);
                            break;
                        case Opcode::StackAdjust:
                            stack_levels_[id] = static_cast<LStackAdjust*>(ins)->value() / -4;
                            break;
                        case Opcode::Call:
                        case Opcode::SysReq: {
                            assert(hasLastConstant);
                            int64_t c = lastConstant;
                            if (file_->PassArgCountAsSize()) c /= 4;
                            stack_levels_[id] -= (int)c;
                            stack_levels_[id]--;
                            break;
                        }
                        case Opcode::GenArray:
                            stack_levels_[id] -= static_cast<LGenArray*>(ins)->dims();
                            stack_levels_[id]++;
                            break;
                        case Opcode::Pop:
                            stack_levels_[id]--;
                            break;
                        default: break;
                        }
                        hasLastConstant = false;
                        lastConstant = 0;
                        if (stack_levels_[id] < 0) return block;
                    }

                    for (LBlock* dom : block->idominated()) {
                        if (!dom) continue;
                        if (LBlock* u = findUnbalancedBlock(dom)) return u;
                    }
                    return nullptr;
                }

            private:
                PawnFile* file_;
                std::vector<int> stack_levels_;
            };
        }

        LBlock* EnforceStackBalance(PawnFile* file, const std::vector<LBlock*>& blocks) {
            StackBalanceValidator val(file, blocks);
            LBlock* unbalanced = val.findUnbalancedBlock(blocks[0]);
            if (!unbalanced) return nullptr;

            LBlock* idomBlock = blocks[unbalanced->idom()->id()];
            assert(idomBlock->last()->op() == Opcode::JumpCondition);
            (void)idomBlock;
            return findSkippingParent(blocks, unbalanced->id(), unbalanced);
        }

        NodeBlock* GetSingleTarget(NodeBlock* block) {
            if (block->nodes().last()->type() != NodeType::Jump) return nullptr;
            return static_cast<DJump*>(block->nodes().last())->target();
        }

        NodeBlock* GetEmptyTarget(NodeBlock* block) {
            if (block->nodes().last() != block->nodes().first()) return nullptr;
            return GetSingleTarget(block);
        }

        NodeBlock* EffectiveTargetNoLoop(NodeBlock* block) {
            if (!block) return nullptr;
            NodeBlock* target = block;
            std::vector<NodeBlock*> seen;
            seen.push_back(block);
            while (true) {
                block = GetEmptyTarget(block);
                if (!block) return target;
                for (NodeBlock* s : seen) if (s == block) return nullptr;
                seen.push_back(block);
                target = block;
            }
        }

        NodeBlock* EffectiveTarget(NodeBlock* block) {
            NodeBlock* target = block;
            while (true) {
                block = GetEmptyTarget(block);
                if (!block) return target;
                target = block;
            }
        }

        NodeBlock* ConstantSettingTarget(NodeBlock* block) {
            NodeBlock* target = block;
            while (true) {
                const auto& ins = target->lir()->instructions();
                if (ins.size() == 2
                    && ins[0]->op() == Opcode::Constant
                    && (ins[1]->op() == Opcode::Jump || ins[1]->op() == Opcode::Goto))
                    return target;
                block = GetEmptyTarget(block);
                if (!block) return nullptr;
                target = block;
            }
        }

    }
}