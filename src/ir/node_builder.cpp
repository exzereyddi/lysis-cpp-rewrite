#include "node_builder.hpp"

#include <cassert>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace lysis {

    NodeBuilder::NodeBuilder(PawnFile* file, LGraph* graph) : file_(file), graph_(graph) {
        blocks_.reserve(graph_->blocks.size());
        for (size_t i = 0; i < graph_->blocks.size(); i++)
            blocks_.push_back(std::make_unique<NodeBlock>(graph_->blocks[i]));
    }

    std::unique_ptr<NodeGraph> NodeBuilder::buildNodes() {
        std::vector<NodeBlock*> rawBlocks;
        rawBlocks.reserve(blocks_.size());
        for (auto& b : blocks_) rawBlocks.push_back(b.get());

        auto graph = std::make_unique<NodeGraph>(file_, std::move(blocks_));
        nodeGraph_ = graph.get();

        rawBlocks[0]->inherit(*graph_, nullptr, nodeGraph_);
        traverse(rawBlocks[0]);
        return graph;
    }

    void NodeBuilder::traverse(NodeBlock* block) {
        for (size_t i = 0; i < block->lir()->numPredecessors(); i++) {
            LBlock* pred = block->lir()->getPredecessor(i);
            if (pred->id() >= block->lir()->id()) continue;
            block->inherit(*graph_, nodeGraph_->blocks(pred->id()), nodeGraph_);
        }

        NodeGraph* ng = nodeGraph_;

        for (auto& insPtr : block->lir()->instructions()) {
            LInstruction* uins = insPtr.get();

            if (uins->op() != Opcode::Goto) {
                size_t i = (size_t)-1;
                while (Variable* var = file_->lookupDeclarations(uins->pc(), i, Scope::Static))
                    block->add(ng->newNode<DDeclareStatic>(var));
            }

            auto lookupVar = [&](int64_t addr, int64_t pc) -> Variable* {
                Variable* v = file_->lookupGlobal(addr);
                if (!v) v = file_->lookupVariable(pc, addr, Scope::Static);
                return v;
                };

            switch (uins->op()) {

            case Opcode::DebugBreak: break;

            case Opcode::Stack: {
                auto* ins = static_cast<LStack*>(uins);
                if (ins->amount() < 0) {
                    for (int64_t k = 0; k < -ins->amount() / 4; k++) {
                        auto* local = ng->newNode<DDeclareLocal>(ins->pc(), (DNode*)nullptr);
                        block->stack()->push(local);
                        block->add(local);
                    }
                }
                else {
                    for (int64_t k = 0; k < ins->amount() / 4; k++) block->stack()->pop();
                }
                break;
            }

            case Opcode::Fill: {
                auto* ins = static_cast<LFill*>(uins);
                auto* local = static_cast<DDeclareLocal*>(block->stack()->alt());
                assert(block->stack()->pri()->type() == NodeType::Constant);
                for (int64_t k = 0; k < ins->amount(); k += 4)
                    block->stack()->set(local->offset() + k, block->stack()->pri());

                auto* con = static_cast<DConstant*>(block->stack()->pri());
                if (!local->value() && con->rawValue() == 0) local->initOperand(0, con);

                if (ins->amount() == 4 && con->rawValue() != 0) {
                    uint32_t v = (uint32_t)con->rawValue();
                    uint8_t bytes[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
                    int need = 4;
                    while (need > 0 && bytes[need - 1] == 0) need--;
                    if (need == 0) need = 1;
                    std::string s((const char*)bytes, (size_t)need);
                    block->add(ng->newNode<DStore>(local, ng->newNode<DString>(std::move(s))));
                }
                break;
            }

            case Opcode::Constant: {
                auto* ins = static_cast<LConstant*>(uins);
                auto* v = ng->newNode<DConstant>(ins->val(), ins->pc());
                block->stack()->set(ins->reg(), v);
                block->add(v);
                break;
            }

            case Opcode::PushConstant: {
                auto* ins = static_cast<LPushConstant*>(uins);
                auto* v = ng->newNode<DConstant>(ins->val(), ins->pc());
                auto* local = ng->newNode<DDeclareLocal>(ins->pc(), v);
                block->stack()->push(local);
                block->add(v); block->add(local);
                break;
            }

            case Opcode::PushReg: {
                auto* ins = static_cast<LPushReg*>(uins);
                auto* local = ng->newNode<DDeclareLocal>(ins->pc(), block->stack()->reg(ins->reg()));
                block->stack()->push(local);
                block->add(local);
                break;
            }

            case Opcode::Pop: {
                auto* ins = static_cast<LPop*>(uins);
                block->stack()->set(ins->reg(), block->stack()->popAsTemp());
                break;
            }

            case Opcode::StackAddress: {
                auto* ins = static_cast<LStackAddress*>(uins);
                block->stack()->set(ins->reg(), static_cast<DDeclareLocal*>(block->stack()->getName(ins->offset())));
                break;
            }

            case Opcode::PushStackAddress: {
                auto* ins = static_cast<LPushStackAddress*>(uins);
                auto* target = static_cast<DDeclareLocal*>(block->stack()->getName(ins->offset()));
                auto* lref = ng->newNode<DLocalRef>(target);
                auto* local = ng->newNode<DDeclareLocal>(ins->pc(), lref);
                block->stack()->push(local);
                block->add(lref); block->add(local);
                break;
            }

            case Opcode::Goto: {
                auto* ins = static_cast<LGoto*>(uins);
                block->add(ng->newNode<DJump>(nodeGraph_->blocks(ins->target()->id())));
                break;
            }

            case Opcode::Jump: {
                auto* ins = static_cast<LJump*>(uins);
                block->add(ng->newNode<DJump>(nodeGraph_->blocks(ins->target()->id())));
                break;
            }

            case Opcode::JumpCondition: {
                auto* ins = static_cast<LJumpCondition*>(uins);
                NodeBlock* lht = nodeGraph_->blocks(ins->trueTarget()->id());
                NodeBlock* rht = nodeGraph_->blocks(ins->falseTarget()->id());
                DNode* cmp = block->stack()->pri();
                SPOpcode jmp = ins->spop();
                if (jmp != SPOpcode::jzer && jmp != SPOpcode::jnz) {
                    SPOpcode newop;
                    switch (ins->spop()) {
                    case SPOpcode::jeq:    newop = SPOpcode::neq;   jmp = SPOpcode::jzer; break;
                    case SPOpcode::jneq:   newop = SPOpcode::eq;    jmp = SPOpcode::jzer; break;
                    case SPOpcode::jsgeq:  newop = SPOpcode::sless; jmp = SPOpcode::jzer; break;
                    case SPOpcode::jsgrtr: newop = SPOpcode::sleq;  jmp = SPOpcode::jzer; break;
                    case SPOpcode::jsleq:  newop = SPOpcode::sgrtr; jmp = SPOpcode::jzer; break;
                    case SPOpcode::jsless: newop = SPOpcode::sgeq;  jmp = SPOpcode::jzer; break;
                    default: assert(false); return;
                    }
                    cmp = ng->newNode<DBinary>(newop, block->stack()->pri(), block->stack()->alt());
                    block->add(cmp);
                }
                block->add(ng->newNode<DJumpCondition>(jmp, cmp, lht, rht));
                break;
            }

            case Opcode::LoadLocal: {
                auto* ins = static_cast<LLoadLocal*>(uins);
                auto* load = ng->newNode<DLoad>(block->stack()->getName(ins->offset()));
                block->stack()->set(ins->reg(), load);
                block->add(load);
                break;
            }

            case Opcode::LoadLocalRef: {
                auto* ins = static_cast<LLoadLocalRef*>(uins);
                auto* inner = ng->newNode<DLoad>(block->stack()->getName(ins->offset()));
                auto* load = ng->newNode<DLoad>(inner);
                block->stack()->set(ins->reg(), load);
                block->add(load);
                break;
            }

            case Opcode::StoreLocal: {
                auto* ins = static_cast<LStoreLocal*>(uins);
                DNode* regNode = block->stack()->reg(ins->reg());
                if (!regNode) {
                    DNode* prev = block->nodes().last();
                    assert(prev->type() == NodeType::Store);
                    if (prev->type() == NodeType::Store) regNode = prev->getOperand(1);
                }
                block->add(ng->newNode<DStore>(block->stack()->getName(ins->offset()), regNode));
                break;
            }

            case Opcode::StoreLocalRef: {
                auto* ins = static_cast<LStoreLocalRef*>(uins);
                auto* load = ng->newNode<DLoad>(block->stack()->getName(ins->offset()));
                block->add(ng->newNode<DStore>(load, block->stack()->reg(ins->reg())));
                break;
            }

            case Opcode::SysReq: {
                auto* sysreq = static_cast<LSysReq*>(uins);
                auto* ins = static_cast<DConstant*>(block->stack()->popValue());
                int64_t argslength = ins->value();
                if (file_->PassArgCountAsSize()) argslength /= 4;

                std::vector<DNode*> arguments;
                arguments.reserve((size_t)argslength);
                for (int64_t k = 0; k < argslength; k++) arguments.push_back(block->stack()->popName());
                auto* call = ng->newNode<DSysReq>(sysreq->nativeX(), arguments);
                block->stack()->set(Register::Pri, call);
                block->add(call);
                break;
            }

            case Opcode::AddConstant: {
                auto* ins = static_cast<LAddConstant*>(uins);
                auto* val = ng->newNode<DConstant>(ins->amount());
                auto* node = ng->newNode<DBinary>(SPOpcode::add, block->stack()->pri(), val);
                block->stack()->set(Register::Pri, node);
                block->add(val); block->add(node);
                break;
            }

            case Opcode::MulConstant: {
                auto* ins = static_cast<LMulConstant*>(uins);
                auto* val = ng->newNode<DConstant>(ins->amount());
                auto* node = ng->newNode<DBinary>(SPOpcode::smul, block->stack()->pri(), val);
                block->stack()->set(Register::Pri, node);
                block->add(val); block->add(node);
                break;
            }

            case Opcode::Bounds:
                block->add(ng->newNode<DBoundsCheck>(block->stack()->pri(),
                    static_cast<LBounds*>(uins)->amount()));
                break;

            case Opcode::IndexAddress: {
                auto* ins = static_cast<LIndexAddress*>(uins);
                auto* node = ng->newNode<DArrayRef>(block->stack()->alt(), block->stack()->pri(), ins->shift());
                block->stack()->set(Register::Pri, node);
                block->add(node);
                break;
            }

            case Opcode::Move: {
                auto* ins = static_cast<LMove*>(uins);
                if (ins->reg() == Register::Pri) block->stack()->set(Register::Pri, block->stack()->alt());
                else                             block->stack()->set(Register::Alt, block->stack()->pri());
                break;
            }

            case Opcode::Store:
                block->add(ng->newNode<DStore>(block->stack()->alt(), block->stack()->pri()));
                break;

            case Opcode::Load: {
                auto* load = ng->newNode<DLoad>(block->stack()->pri());
                block->stack()->set(Register::Pri, load);
                block->add(load);
                break;
            }

            case Opcode::Swap: {
                auto* ins = static_cast<LSwap*>(uins);
                DNode* lhs = block->stack()->popAsTemp();
                DNode* rhs = block->stack()->reg(ins->reg());
                auto* local = ng->newNode<DDeclareLocal>(ins->pc(), rhs);
                block->stack()->set(ins->reg(), lhs);
                block->stack()->push(local);
                block->add(local);
                break;
            }

            case Opcode::IncI: block->add(ng->newNode<DIncDec>(block->stack()->pri(), 1));  break;
            case Opcode::DecI: block->add(ng->newNode<DIncDec>(block->stack()->pri(), -1)); break;

            case Opcode::IncLocal: {
                auto* ins = static_cast<LIncLocal*>(uins);
                auto* local = static_cast<DDeclareLocal*>(block->stack()->getName(ins->offset()));
                block->add(ng->newNode<DIncDec>(local, 1));
                break;
            }
            case Opcode::IncReg: {
                auto* ins = static_cast<LIncReg*>(uins);
                block->add(ng->newNode<DIncDec>(block->stack()->reg(ins->reg()), 1));
                break;
            }
            case Opcode::DecLocal: {
                auto* ins = static_cast<LDecLocal*>(uins);
                auto* local = static_cast<DDeclareLocal*>(block->stack()->getName(ins->offset()));
                block->add(ng->newNode<DIncDec>(local, -1));
                break;
            }
            case Opcode::DecReg: {
                auto* ins = static_cast<LDecReg*>(uins);
                block->add(ng->newNode<DIncDec>(block->stack()->reg(ins->reg()), -1));
                break;
            }

            case Opcode::Return:
                block->add(ng->newNode<DReturn>(block->stack()->pri()));
                break;

            case Opcode::PushLocal: {
                auto* ins = static_cast<LPushLocal*>(uins);
                auto* load = ng->newNode<DLoad>(block->stack()->getName(ins->offset()));
                auto* local = ng->newNode<DDeclareLocal>(ins->pc(), load);
                block->stack()->push(local);
                block->add(load); block->add(local);
                break;
            }

            case Opcode::Exchange: {
                DNode* n = block->stack()->alt();
                block->stack()->set(Register::Alt, block->stack()->pri());
                block->stack()->set(Register::Pri, n);
                break;
            }

            case Opcode::Unary: {
                auto* ins = static_cast<LUnary*>(uins);
                auto* unary = ng->newNode<DUnary>(ins->spop(), block->stack()->reg(ins->reg()));
                block->stack()->set(Register::Pri, unary);
                block->add(unary);
                break;
            }

            case Opcode::Binary: {
                auto* ins = static_cast<LBinary*>(uins);
                DNode* nL = block->stack()->reg(ins->lhs());
                DNode* nR = block->stack()->reg(ins->rhs());
                auto* binary = ng->newNode<DBinary>(ins->spop(), nL, nR);
                block->stack()->set(Register::Pri, binary);
                block->add(binary);

                if (ins->spop() == SPOpcode::sdiv_alt || ins->spop() == SPOpcode::sdiv) {
                    auto* mod = ng->newNode<DBinary>(SPOpcode::sdiv_alt_mod, nL, nR);
                    block->stack()->set(Register::Alt, mod);
                    block->add(mod);
                }
                break;
            }

            case Opcode::ShiftLeftConstant: {
                auto* ins = static_cast<LShiftLeftConstant*>(uins);
                auto* val = ng->newNode<DConstant>(ins->val());
                auto* node = ng->newNode<DBinary>(SPOpcode::shl, block->stack()->reg(ins->reg()), val);
                block->stack()->set(ins->reg(), node);
                block->add(val); block->add(node);
                break;
            }

            case Opcode::PushGlobal: {
                auto* ins = static_cast<LPushGlobal*>(uins);
                auto* dglobal = ng->newNode<DGlobal>(lookupVar(ins->address(), ins->pc()));
                auto* node = ng->newNode<DLoad>(dglobal);
                auto* local = ng->newNode<DDeclareLocal>(ins->pc(), node);
                block->stack()->push(local);
                block->add(dglobal); block->add(node); block->add(local);
                break;
            }

            case Opcode::LoadGlobal: {
                auto* ins = static_cast<LLoadGlobal*>(uins);
                auto* dglobal = ng->newNode<DGlobal>(lookupVar(ins->address(), ins->pc()));
                auto* node = ng->newNode<DLoad>(dglobal);
                block->stack()->set(ins->reg(), node);
                block->add(dglobal); block->add(node);
                break;
            }

            case Opcode::StoreGlobal: {
                auto* ins = static_cast<LStoreGlobal*>(uins);
                auto* node = ng->newNode<DGlobal>(lookupVar(ins->address(), ins->pc()));
                auto* store = ng->newNode<DStore>(node, block->stack()->reg(ins->reg()));
                block->add(node); block->add(store);
                break;
            }

            case Opcode::Call: {
                auto* ins = static_cast<LCall*>(uins);
                Function* f = file_->lookupFunction((int64_t)ins->address());
                auto* args = static_cast<DConstant*>(block->stack()->popValue());
                int64_t argslength = args->value();
                if (file_->PassArgCountAsSize()) argslength /= 4;

                std::vector<DNode*> arguments;
                arguments.reserve((size_t)argslength);
                for (int64_t k = 0; k < argslength; k++) arguments.push_back(block->stack()->popName());
                auto* call = ng->newNode<DCall>(f, arguments);
                block->stack()->set(Register::Pri, call);
                block->add(call);
                break;
            }

            case Opcode::EqualConstant: {
                auto* ins = static_cast<LEqualConstant*>(uins);
                auto* c = ng->newNode<DConstant>(ins->value());
                auto* node = ng->newNode<DBinary>(SPOpcode::eq, block->stack()->reg(ins->reg()), c);
                block->stack()->set(Register::Pri, node);
                block->add(c); block->add(node);
                break;
            }

            case Opcode::LoadIndex: {
                auto* ins = static_cast<LLoadIndex*>(uins);
                auto* aref = ng->newNode<DArrayRef>(block->stack()->alt(), block->stack()->pri(), ins->shift());
                auto* load = ng->newNode<DLoad>(aref);
                block->stack()->set(Register::Pri, load);
                block->add(aref); block->add(load);
                break;
            }

            case Opcode::ZeroGlobal: {
                auto* ins = static_cast<LZeroGlobal*>(uins);
                auto* dglobal = ng->newNode<DGlobal>(lookupVar(ins->address(), ins->pc()));
                auto* rhs = ng->newNode<DConstant>(0);
                auto* lhs = ng->newNode<DStore>(dglobal, rhs);
                block->add(dglobal); block->add(rhs); block->add(lhs);
                break;
            }

            case Opcode::IncGlobal: {
                auto* ins = static_cast<LIncGlobal*>(uins);
                auto* dglobal = ng->newNode<DGlobal>(lookupVar(ins->address(), ins->pc()));
                auto* load = ng->newNode<DLoad>(dglobal);
                auto* val = ng->newNode<DConstant>(1);
                auto* add = ng->newNode<DBinary>(SPOpcode::add, load, val);
                auto* store = ng->newNode<DStore>(dglobal, add);
                block->add(load); block->add(val); block->add(add); block->add(store);
                break;
            }

            case Opcode::DecGlobal: {
                auto* ins = static_cast<LDecGlobal*>(uins);
                auto* dglobal = ng->newNode<DGlobal>(lookupVar(ins->address(), ins->pc()));
                auto* load = ng->newNode<DLoad>(dglobal);
                auto* val = ng->newNode<DConstant>(1);
                auto* sub = ng->newNode<DBinary>(SPOpcode::sub, load, val);
                auto* store = ng->newNode<DStore>(dglobal, sub);
                block->add(load); block->add(val); block->add(sub); block->add(store);
                break;
            }

            case Opcode::StoreGlobalConstant: {
                auto* lstore = static_cast<LStoreGlobalConstant*>(uins);
                auto* val = ng->newNode<DConstant>(lstore->value());
                auto* global = ng->newNode<DGlobal>(lookupVar(lstore->address(), lstore->pc()));
                auto* store = ng->newNode<DStore>(global, val);
                block->add(val); block->add(global); block->add(store);
                break;
            }

            case Opcode::StoreLocalConstant: {
                auto* lstore = static_cast<LStoreLocalConstant*>(uins);
                auto* var = static_cast<DDeclareLocal*>(block->stack()->getName(lstore->address()));
                auto* val = ng->newNode<DConstant>(lstore->value());
                block->add(val);
                block->add(ng->newNode<DStore>(var, val));
                break;
            }

            case Opcode::ZeroLocal: {
                auto* lstore = static_cast<LZeroLocal*>(uins);
                auto* var = static_cast<DDeclareLocal*>(block->stack()->getName(lstore->address()));
                auto* val = ng->newNode<DConstant>(0);
                block->add(val);
                block->add(ng->newNode<DStore>(var, val));
                break;
            }

            case Opcode::Heap: {
                auto* ins = static_cast<LHeap*>(uins);
                auto* heap = ng->newNode<DHeap>(ins->amount());
                block->add(heap);
                block->stack()->set(Register::Alt, heap);
                break;
            }

            case Opcode::MemCopy: {
                auto* ins = static_cast<LMemCopy*>(uins);
                block->add(ng->newNode<DMemCopy>(block->stack()->alt(), block->stack()->pri(), ins->bytes()));
                break;
            }

            case Opcode::Switch: {
                auto* ins = static_cast<LSwitch*>(uins);
                block->add(ng->newNode<DSwitch>(block->stack()->pri(), ins));
                break;
            }

            case Opcode::GenArray: {
                auto* ins = static_cast<LGenArray*>(uins);
                std::vector<DNode*> dims(ins->dims());
                for (int32_t k = 0; k < ins->dims(); k++) dims[k] = block->stack()->popValue();
                auto* ga = ng->newNode<DGenArray>(ins->pc() + 4 * ins->dims() + 4, dims, ins->autozero());
                block->stack()->push(ga);
                block->add(ga);
                break;
            }

            case Opcode::InitArray: break;

            case Opcode::StackAdjust: {
                auto* ins = static_cast<LStackAdjust*>(uins);
                assert(ins->value() % 4 == 0);
                if (ins->value() < block->stack()->depth()) {
                    int32_t amt = (ins->value() - block->stack()->depth()) / -4;
                    for (int32_t k = 0; k < amt; k++) {
                        auto* local = ng->newNode<DDeclareLocal>(ins->pc(), (DNode*)nullptr);
                        block->stack()->push(local);
                        block->add(local);
                    }
                }
                else {
                    int32_t amt = (block->stack()->depth() - ins->value()) / -4;
                    for (int32_t k = 0; k < amt; k++) block->stack()->pop();
                }
                if (ins->value() > 0 || block->nodes().first() == block->nodes().last())
                    block->add(ng->newNode<DLabel>(ins->pc()));
                break;
            }

            case Opcode::LoadCtrl: {
                auto* ins = static_cast<LLoadCtrl*>(uins);
                assert(ins->ctrlregindex() == 5);
                (void)ins;
                block->stack()->set(Register::Pri, ng->newNode<DConstant>(0));
                break;
            }

            case Opcode::StoreCtrl: {
                auto* ins = static_cast<LStoreCtrl*>(uins);
                assert(ins->ctrlregindex() == 4);
                (void)ins;
                DNode* pri = block->stack()->pri();
                assert(pri->type() == NodeType::Binary);
                auto* add = static_cast<DBinary*>(pri);
                assert(add->lhs()->type() == NodeType::Constant && add->rhs()->type() == NodeType::Constant);
                auto* frm = static_cast<DConstant*>(add->lhs());
                (void)frm;
                assert(frm->rawValue() == 0);
                auto* stkadjust = static_cast<DConstant*>(add->rhs());
                int64_t v = stkadjust->rawValue();
                assert(v <= 0 && v % 4 == 0);
                if (v < block->stack()->depth()) {
                    int64_t amt = (v - block->stack()->depth()) / -4;
                    for (int64_t k = 0; k < amt; k++) {
                        auto* local = ng->newNode<DDeclareLocal>(ins->pc(), (DNode*)nullptr);
                        block->stack()->push(local);
                        block->add(local);
                    }
                }
                else {
                    int64_t amt = (block->stack()->depth() - v) / -4;
                    for (int64_t k = 0; k < amt; k++) block->stack()->pop();
                }
                block->add(ng->newNode<DLabel>(ins->pc()));
                break;
            }

            default:
                throw std::runtime_error("unhandled opcode " + std::to_string((int)uins->op()));
            }
        }

        for (LBlock* lir : block->lir()->idominated())
            if (lir) traverse(nodeGraph_->blocks(lir->id()));
    }

}