#include "source_structure_builder.hpp"
#include "block_analysis.hpp"

#include <algorithm>
#include <cassert>

namespace lysis {

    bool SourceStructureBuilder::isJoin(NodeBlock* block) const {
        std::stack<NodeBlock*> tmp = joinStack_;
        while (!tmp.empty()) {
            if (tmp.top() == block) return true;
            tmp.pop();
        }
        return false;
    }

    bool SourceStructureBuilder::HasSharedTarget(NodeBlock* pred, DJumpCondition* jcc) {
        NodeBlock* trueTarget = BlockAnalysis::EffectiveTarget(jcc->trueTarget());
        if (trueTarget->lir()->numPredecessors() == 1) return false;
        if (trueTarget->lir()->loop() && trueTarget->lir()->loop()->backedge() == trueTarget->lir())
            return false;
        if (pred->lir()->idominated().size() > 3) return true;

        const auto& ins = trueTarget->lir()->instructions();
        return ins.size() == 2
            && ins[0]->op() == Opcode::Constant
            && ins[1]->op() == Opcode::Jump;
    }

    LogicOperator SourceStructureBuilder::ToLogicOp(DJumpCondition* jcc) {
        NodeBlock* trueTarget = BlockAnalysis::ConstantSettingTarget(jcc->trueTarget());
        auto* constant = static_cast<LConstant*>(trueTarget->lir()->instructions()[0].get());
        bool targetIsTruthy = (constant->val() == 1);
        return (jcc->spop() == SPOpcode::jnz && targetIsTruthy) ? LogicOperator::Or : LogicOperator::And;
    }

    NodeBlock* SourceStructureBuilder::SingleTarget(NodeBlock* block) {
        return static_cast<DJump*>(block->nodes().last())->target();
    }

    void SourceStructureBuilder::AssertInnerJoinValidity(NodeBlock* join, NodeBlock* earlyExit) {
        auto* jcc = static_cast<DJumpCondition*>(join->nodes().last());
        assert(BlockAnalysis::EffectiveTarget(jcc->trueTarget()) == earlyExit
            || join == SingleTarget(earlyExit));
        (void)jcc; (void)earlyExit;
    }

    SourceStructureBuilder::BuildLogicChainResult
        SourceStructureBuilder::buildLogicChain(NodeBlock* block, NodeBlock* earlyExitStop) {
        BuildLogicChainResult res;

        auto* jcc = static_cast<DJumpCondition*>(block->nodes().last());
        LogicChain* chain = arena_.makeChain(ToLogicOp(jcc));

        NodeBlock* earlyExit = BlockAnalysis::EffectiveTarget(jcc->trueTarget());
        NodeBlock* exprBlock = block;

        while (true) {
            while (true) {
                auto* childJcc = static_cast<DJumpCondition*>(exprBlock->nodes().last());
                if (BlockAnalysis::EffectiveTarget(childJcc->trueTarget()) != earlyExit) {
                    BuildLogicChainResult sub = buildLogicChain(exprBlock, earlyExit);
                    AssertInnerJoinValidity(sub.join, earlyExit);
                    chain->append(sub.chain);
                    exprBlock = sub.join;
                    childJcc = static_cast<DJumpCondition*>(exprBlock->nodes().last());
                }
                else {
                    chain->append(childJcc->getOperand(0));
                }
                exprBlock = childJcc->falseTarget();
                if (exprBlock->nodes().last()->type() != NodeType::JumpCondition) break;
            }

            while (true) {
                assert(exprBlock->lir()->instructions()[0]->op() == Opcode::Constant);

                NodeBlock* condBlock = SingleTarget(exprBlock);
                res.join = condBlock;

                auto ret = [&](LogicChain* c) { res.chain = c; return res; };

                if (earlyExitStop && SingleTarget(earlyExitStop) == condBlock) return ret(chain);
                if (condBlock->lir()->instructions()[0]->op() == Opcode::Jump
                    && earlyExit == SingleTarget(condBlock)) return ret(chain);
                if (condBlock->nodes().first()->type() == NodeType::Store) return ret(chain);
                if (condBlock->nodes().last()->type() == NodeType::Return) return ret(chain);
                if (condBlock->nodes().last()->type() == NodeType::Jump
                    && BlockAnalysis::EffectiveTarget(condBlock) == earlyExit) return ret(chain);
                if (condBlock->nodes().last()->type() == NodeType::Jump
                    && condBlock->lir()->loop()
                    && BlockAnalysis::GetSingleTarget(condBlock)->lir() == condBlock->lir()->loop())
                    return ret(chain);

                auto* condJcc = static_cast<DJumpCondition*>(condBlock->nodes().last());
                if (BlockAnalysis::EffectiveTarget(condJcc->trueTarget()) == earlyExitStop) return ret(chain);
                if (!HasSharedTarget(condBlock, condJcc)) return ret(chain);

                earlyExit = BlockAnalysis::EffectiveTarget(condJcc->trueTarget());
                if (earlyExit->lir()->instructions()[0]->op() != Opcode::Constant) return ret(chain);

                BuildLogicChainResult sub = buildLogicChain(condJcc->falseTarget(), earlyExit);

                LogicChain* root = arena_.makeChain(ToLogicOp(condJcc));
                root->append(chain);
                root->append(sub.chain);
                chain = root;

                if (sub.join->nodes().last()->type() == NodeType::Return) {
                    res.join = sub.join;
                    return ret(chain);
                }

                AssertInnerJoinValidity(sub.join, earlyExit);

                auto* innerJcc = static_cast<DJumpCondition*>(sub.join->nodes().last());
                if (innerJcc->falseTarget()->nodes().last()->type() == NodeType::JumpCondition) {
                    exprBlock = innerJcc->falseTarget();
                    auto* childJcc = static_cast<DJumpCondition*>(exprBlock->nodes().last());
                    NodeBlock* exit = BlockAnalysis::EffectiveTarget(childJcc->trueTarget());
                    if (exit->lir()->instructions()[0]->op() != Opcode::Constant) {
                        res.join = sub.join;
                        return ret(chain);
                    }
                    break;
                }

                exprBlock = earlyExit;
            }
        }
    }

    NodeBlock* SourceStructureBuilder::findJoinOfSimpleIf(NodeBlock* block, DJumpCondition* jcc) {
        assert(block->nodes().last() == jcc);
        (void)jcc;
        if (block->lir()->idominated().size() == 2) {
            NodeBlock* trueTarget = BlockAnalysis::EffectiveTargetNoLoop(jcc->trueTarget());
            if (trueTarget && jcc->trueTarget() != trueTarget) return jcc->trueTarget();
            NodeBlock* falseTarget = BlockAnalysis::EffectiveTargetNoLoop(jcc->falseTarget());
            if (falseTarget && jcc->falseTarget() != falseTarget) return jcc->falseTarget();
            return nullptr;
        }
        return graph_.blocks((size_t)block->lir()->idominated()[2]->id());
    }

    ControlBlock* SourceStructureBuilder::traverseComplexIf(NodeBlock* block, DJumpCondition* jcc) {
        (void)jcc;
        BuildLogicChainResult res = buildLogicChain(block, nullptr);
        LogicChain* chain = res.chain;
        NodeBlock* join = res.join;

        if (join->nodes().last()->type() == NodeType::Jump)
            return arena_.make<IfBlock>(block, false, chain,
                (ControlBlock*)nullptr, (ControlBlock*)nullptr, (ControlBlock*)nullptr);

        if (join->nodes().first()->type() == NodeType::Store) {
            static_cast<DStore*>(join->nodes().first())->setLogicChain(chain);
            return arena_.make<StatementBlock>(block,
                traverseBlock(graph_.blocks((size_t)join->lir()->id())));
        }

        if (join->nodes().last()->type() == NodeType::Return)
            return arena_.make<ReturnBlock>(block, chain);

        auto* finalJcc = static_cast<DJumpCondition*>(join->nodes().last());
        assert(finalJcc->spop() == SPOpcode::jzer);

        NodeBlock* joinBlock = findJoinOfSimpleIf(join, finalJcc);
        NodeBlock* trueBlock = finalJcc->falseTarget();
        NodeBlock* falseBlock = finalJcc->trueTarget();

        if (!joinBlock) joinBlock = falseBlock;
        if (falseBlock == joinBlock) falseBlock = nullptr;

        bool invert = false;
        if (trueBlock == joinBlock) {
            trueBlock = falseBlock;
            falseBlock = nullptr;
            invert = !invert;
        }

        if (join->lir()->idominated().size() == 2
            || BlockAnalysis::EffectiveTarget(falseBlock) == joinBlock) {
            if (join->lir()->idominated().size() == 3)
                joinBlock = BlockAnalysis::EffectiveTarget(falseBlock);

            pushScope(joinBlock);
            ControlBlock* trueArm1 = traverseBlock(trueBlock);
            popScope();

            return arena_.make<IfBlock>(block, invert, chain, trueArm1, traverseJoin(joinBlock));
        }

        pushScope(joinBlock);
        ControlBlock* trueArm2 = traverseBlock(trueBlock);
        ControlBlock* falseArm = traverseBlock(falseBlock);
        popScope();

        return arena_.make<IfBlock>(block, invert, chain, trueArm2, falseArm, traverseJoin(joinBlock));
    }

    ControlBlock* SourceStructureBuilder::traverseIf(NodeBlock* block, DJumpCondition* jcc) {
        if (HasSharedTarget(block, jcc)) return traverseComplexIf(block, jcc);

        bool jzer = (jcc->spop() == SPOpcode::jzer);
        NodeBlock* trueTarget = jzer ? jcc->falseTarget() : jcc->trueTarget();
        NodeBlock* falseTarget = jzer ? jcc->trueTarget() : jcc->falseTarget();
        NodeBlock* joinTarget = findJoinOfSimpleIf(block, jcc);

        if (!joinTarget) joinTarget = falseTarget;

        if (falseTarget == joinTarget || BlockAnalysis::EffectiveTargetNoLoop(falseTarget) == joinTarget)
            falseTarget = nullptr;

        bool invert = false;
        if (trueTarget == joinTarget || BlockAnalysis::EffectiveTargetNoLoop(trueTarget) == joinTarget) {
            trueTarget = falseTarget;
            falseTarget = nullptr;
            invert = !invert;
        }

        if (BlockAnalysis::EffectiveTargetNoLoop(trueTarget) == nullptr)
            trueTarget = nullptr;

        if (BlockAnalysis::EffectiveTargetNoLoop(joinTarget) == nullptr) {
            trueTarget = joinTarget;
            joinTarget = nullptr;
            invert = !invert;
        }

        ControlBlock* trueArm = nullptr;
        if (trueTarget) {
            pushScope(joinTarget);
            trueArm = traverseBlock(trueTarget);
            popScope();
        }

        ControlBlock* joinArm = traverseJoin(joinTarget);
        if (!falseTarget) return arena_.make<IfBlock>(block, invert, trueArm, joinArm);

        pushScope(joinTarget);
        ControlBlock* falseArm = traverseBlock(falseTarget);
        popScope();

        return arena_.make<IfBlock>(block, invert, trueArm, falseArm, joinArm);
    }

    SourceStructureBuilder::LoopBodyResult
        SourceStructureBuilder::findLoopJoinAndBody(NodeBlock* header, NodeBlock* effectiveHeader) {
        assert(effectiveHeader->lir()->numSuccessors() >= 1);

        LoopBodyResult r;

        LBlock* succ1 = effectiveHeader->lir()->getSuccessor(0);
        LBlock* succ2 = (effectiveHeader->lir()->numSuccessors() == 2)
            ? effectiveHeader->lir()->getSuccessor(1) : nullptr;

        bool cond1 = (succ1->loop() == header->lir() && succ2 == nullptr);
        bool cond2 = (succ2 != nullptr && (succ1->loop() != header->lir() || succ2->loop() != header->lir()));

        auto blk = [&](LBlock* b) { return graph_.blocks((size_t)b->id()); };

        if (cond1 || cond2) {
            assert(succ1->loop() == header->lir() || (succ2 != nullptr && succ2->loop() == header->lir()));
            if (succ1->loop() != header->lir()) {
                r.join = blk(succ1);
                r.body = succ2 ? blk(succ2) : nullptr;
            }
            else {
                r.join = succ2 ? blk(succ2) : nullptr;
                r.body = blk(succ1);
            }
            r.cond = header;

            if (header == effectiveHeader && r.body && BlockAnalysis::GetEmptyTarget(r.body) == header) {
                r.body = nullptr;
                r.type = ControlType::DoWhileLoop;
                return r;
            }
            r.type = ControlType::WhileLoop;
            return r;
        }

        LBlock* backedge = header->lir()->backedge();
        if (BlockAnalysis::GetEmptyTarget(blk(backedge)) == header) {
            assert(backedge->numPredecessors() == 1);
            backedge = backedge->getPredecessor(0);
        }

        assert(backedge->numSuccessors() == 1 || backedge->numSuccessors() == 2);
        succ1 = backedge->getSuccessor(0);
        succ2 = (backedge->numSuccessors() > 1) ? backedge->getSuccessor(1) : nullptr;

        r.body = header;
        r.cond = blk(backedge);
        if (succ1->loop() != header->lir()) r.join = blk(succ1);
        else if (succ2) { assert(succ2->loop() != header->lir()); r.join = blk(succ2); }
        else r.join = nullptr;
        r.type = ControlType::DoWhileLoop;
        return r;
    }

    ControlBlock* SourceStructureBuilder::traverseLoop(NodeBlock* block) {
        DNode* last = block->nodes().last();

        NodeBlock* effectiveHeader = block;
        LogicChain* chain = nullptr;
        if (last->type() == NodeType::JumpCondition) {
            auto* jcc = static_cast<DJumpCondition*>(last);
            if (HasSharedTarget(block, jcc)) {
                BuildLogicChainResult r = buildLogicChain(block, nullptr);
                chain = r.chain;
                effectiveHeader = r.join;
            }
        }

        last = effectiveHeader->nodes().last();

        if (last->type() == NodeType::Switch) {
            SwitchBlock* sb = static_cast<SwitchBlock*>(traverseSwitch(block, static_cast<DSwitch*>(last)));
            return arena_.make<WhileLoop>(ControlType::DoWhileLoop, effectiveHeader, sb, (ControlBlock*)nullptr);
        }

        assert(last->type() == NodeType::JumpCondition || last->type() == NodeType::Jump);

        if (last->type() == NodeType::JumpCondition || last->type() == NodeType::Jump) {
            assert(BlockAnalysis::GetSingleTarget(graph_.blocks((size_t)block->lir()->backedge()->id())) == block);

            LoopBodyResult r = findLoopJoinAndBody(block, effectiveHeader);

            auto traverseInScope = [&](NodeBlock* nb, auto fn) -> ControlBlock* {
                if (!nb) return nullptr;
                pushScope(block);
                pushScope(r.cond);
                ControlBlock* result = fn(nb);
                popScope();
                popScope();
                return result;
                };

            ControlBlock* joinArm = traverseInScope(r.join, [this](NodeBlock* b) { return traverseJoin(b); });
            ControlBlock* bodyArm = traverseInScope(r.body, [this](NodeBlock* b) { return traverseBlockNoLoop(b); });

            if (chain && r.type != ControlType::DoWhileLoop)
                return arena_.make<WhileLoop>(r.type, chain, bodyArm, joinArm);
            return arena_.make<WhileLoop>(r.type, r.cond, bodyArm, joinArm);
        }

        return nullptr;
    }

    ControlBlock* SourceStructureBuilder::traverseSwitch(NodeBlock* block, DSwitch* switch_) {
        std::vector<LBlock*> dominators(block->lir()->idominated().begin(),
            block->lir()->idominated().end());

        auto rm = [&](LBlock* b) {
            dominators.erase(std::remove(dominators.begin(), dominators.end(), b), dominators.end());
            };
        rm(switch_->defaultCase());
        for (size_t i = 0; i < switch_->numCases(); i++) rm(switch_->getCase(i).target);

        NodeBlock* join = nullptr;
        if (!dominators.empty()) {
            assert(dominators.size() == 1);
            join = graph_.blocks((size_t)dominators.back()->id());
        }

        ControlBlock* joinArm = nullptr;
        if (join && BlockAnalysis::EffectiveTarget(join)->lir()->id() > block->lir()->id())
            joinArm = traverseJoin(join);

        pushScope(block);
        pushScope(join);
        std::vector<SwitchBlock::Case> cases;
        NodeBlock* defaultBlock = graph_.blocks((size_t)switch_->defaultCase()->id());
        ControlBlock* defaultArm = isJoin(defaultBlock) ? nullptr : traverseBlock(defaultBlock);
        pushScope(defaultBlock);
        for (size_t i = 0; i < switch_->numCases(); i++) {
            ControlBlock* arm = traverseBlock(graph_.blocks((size_t)switch_->getCase(i).target->id()));
            cases.emplace_back(std::vector<int64_t>(switch_->getCase(i).values()), arm);
        }
        popScope();
        popScope();
        popScope();

        return arena_.make<SwitchBlock>(block, defaultArm, std::move(cases), joinArm);
    }

    ControlBlock* SourceStructureBuilder::traverseJoin(NodeBlock* block) {
        if (!block || isJoin(block)) return nullptr;
        return traverseBlock(block);
    }

    ControlBlock* SourceStructureBuilder::traverseBlockNoLoop(NodeBlock* block) {
        DNode* last = block->nodes().last();

        if (last->type() == NodeType::JumpCondition)
            return traverseIf(block, static_cast<DJumpCondition*>(last));

        if (last->type() == NodeType::Jump) {
            auto* jump = static_cast<DJump*>(last);
            NodeBlock* target = jump->target();

            if (target->nodes().first()->type() == NodeType::Label) {
                const auto& ins = block->lir()->instructions();
                if (!ins.empty() && ins.back()->op() == Opcode::Jump)
                    return arena_.make<GotoBlock>(block, target);
            }

            ControlBlock* next = isJoin(target) ? nullptr : traverseBlock(target);
            return arena_.make<StatementBlock>(block, next);
        }

        if (last->type() == NodeType::Switch)
            return traverseSwitch(block, static_cast<DSwitch*>(last));

        assert(last->type() == NodeType::Return);
        return arena_.make<ReturnBlock>(block);
    }

    ControlBlock* SourceStructureBuilder::traverseBlock(NodeBlock* block) {
        return block->lir()->backedge() ? traverseLoop(block) : traverseBlockNoLoop(block);
    }

    ControlBlock* SourceStructureBuilder::build() {
        return traverseBlock(graph_.blocks(0));
    }

}