#include "method_parser.hpp"
#include "block_analysis.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <stdexcept>

namespace lysis {

    int32_t MethodParser::readInt32() {
        pc_ += 4;
        return BitConverter::ToInt32(file_->code().bytes().data(), (size_t)(pc_ - 4));
    }
    uint32_t MethodParser::readUInt32() {
        pc_ += 4;
        return BitConverter::ToUInt32(file_->code().bytes().data(), (size_t)(pc_ - 4));
    }
    SPOpcode MethodParser::readOp() { return SPOpcodeFromInt((int)readUInt32()); }
    SPOpcode MethodParser::peekOp() {
        uint32_t op = readUInt32();
        pc_ -= 4;
        return SPOpcodeFromInt((int)op);
    }

    LInstruction* MethodParser::add(std::unique_ptr<LInstruction> ins) {
        ins->setPc(current_pc_);
        LInstruction* raw = ins.get();
        lir_->instructions.push_back(std::move(ins));
        return raw;
    }

    LBlock* MethodParser::prepareJumpTarget(int64_t offset) {
        auto it = lir_->targets.find(offset);
        if (it != lir_->targets.end()) return it->second;
        auto owned = std::make_unique<LBlock>(offset);
        LBlock* raw = owned.get();
        lir_->targets[offset] = raw;
        lir_->owned_targets.push_back(std::move(owned));
        return raw;
    }

    int32_t MethodParser::trackStack(int32_t offset) {
        if (offset < 0) return offset;
        if (offset > lir_->argDepth && ((offset - 12) / 4) + 1 <= 32)
            lir_->argDepth = offset;
        return offset;
    }

    int32_t MethodParser::trackGlobal(int32_t addr) {
        file_->addGlobal(addr);
        return addr;
    }

    std::unique_ptr<LInstruction> MethodParser::readInstruction(SPOpcode op) {
        using SP = SPOpcode;

        auto regOf = [&](SP a) { return (op == a) ? Register::Pri : Register::Alt; };

        switch (op) {
        case SP::load_pri:
        case SP::load_alt:
            return std::make_unique<LLoadGlobal>(trackGlobal(readInt32()), regOf(SP::load_pri));

        case SP::load_s_pri:
        case SP::load_s_alt:
            return std::make_unique<LLoadLocal>(trackStack(readInt32()), regOf(SP::load_s_pri));

        case SP::lref_s_pri:
        case SP::lref_s_alt:
            return std::make_unique<LLoadLocalRef>(trackStack(readInt32()), regOf(SP::lref_s_pri));

        case SP::stor_s_pri:
        case SP::stor_s_alt:
            return std::make_unique<LStoreLocal>(regOf(SP::stor_s_pri), trackStack(readInt32()));

        case SP::sref_s_pri:
        case SP::sref_s_alt:
            return std::make_unique<LStoreLocalRef>(regOf(SP::sref_s_pri), trackStack(readInt32()));

        case SP::load_i:  return std::make_unique<LLoad>(4);
        case SP::lodb_i:  return std::make_unique<LLoad>(readInt32());

        case SP::const_pri:
        case SP::const_alt:
            return std::make_unique<LConstant>(readInt32(), regOf(SP::const_pri));

        case SP::addr_pri:
        case SP::addr_alt:
            return std::make_unique<LStackAddress>(trackStack(readInt32()), regOf(SP::addr_pri));

        case SP::stor_pri:
        case SP::stor_alt:
            return std::make_unique<LStoreGlobal>(trackGlobal(readInt32()), regOf(SP::stor_pri));

        case SP::stor_i: return std::make_unique<LStore>(4);
        case SP::strb_i: return std::make_unique<LStore>(readInt32());

        case SP::lidx:   return std::make_unique<LLoadIndex>(4);
        case SP::lidx_b: return std::make_unique<LLoadIndex>(readInt32());

        case SP::idxaddr:   return std::make_unique<LIndexAddress>(2);
        case SP::idxaddr_b: return std::make_unique<LIndexAddress>(readInt32());

        case SP::move_pri:
        case SP::move_alt:
            return std::make_unique<LMove>(regOf(SP::move_pri));

        case SP::xchg: return std::make_unique<LExchange>();

        case SP::push_pri:
        case SP::push_alt:
            return std::make_unique<LPushReg>(regOf(SP::push_pri));

        case SP::push_c: return std::make_unique<LPushConstant>(readInt32());
        case SP::push:   return std::make_unique<LPushGlobal>(trackGlobal(readInt32()));
        case SP::push_s: return std::make_unique<LPushLocal>(trackStack(readInt32()));

        case SP::pop_pri:
        case SP::pop_alt:
            return std::make_unique<LPop>(regOf(SP::pop_pri));

        case SP::stack: return std::make_unique<LStack>(readInt32());
        case SP::retn:  return std::make_unique<LReturn>();

        case SP::call: {
            int64_t addr = readInt32();
            file_->addFunction(addr);
            return std::make_unique<LCall>(addr);
        }

        case SP::jump: {
            int64_t offset = readUInt32();
            return std::make_unique<LJump>(prepareJumpTarget(offset), offset);
        }

        case SP::jeq: case SP::jneq: case SP::jnz: case SP::jzer:
        case SP::jsgeq: case SP::jsless: case SP::jsgrtr: case SP::jsleq: {
            int64_t offset = readUInt32();
            if (offset == pc_) return std::make_unique<LJump>(prepareJumpTarget(offset), offset);
            return std::make_unique<LJumpCondition>(op, prepareJumpTarget(offset),
                prepareJumpTarget(pc_), offset);
        }

        case SP::sdiv_alt:
        case SP::sub_alt:
            return std::make_unique<LBinary>(op, Register::Alt, Register::Pri);

        case SP::add: case SP::and_: case SP::or_: case SP::smul:
        case SP::sdiv: case SP::shr: case SP::shl: case SP::sub:
        case SP::sshr: case SP::xor_:
            return std::make_unique<LBinary>(op, Register::Pri, Register::Alt);

        case SP::shl_c_pri:
        case SP::shl_c_alt:
            return std::make_unique<LShiftLeftConstant>(readInt32(), regOf(SP::shl_c_pri));

        case SP::not_: case SP::neg: case SP::invert:
            return std::make_unique<LUnary>(op, Register::Pri);

        case SP::add_c:  return std::make_unique<LAddConstant>(readInt32());
        case SP::smul_c: return std::make_unique<LMulConstant>(readInt32());

        case SP::zero_pri:
        case SP::zero_alt:
            return std::make_unique<LConstant>(0, regOf(SP::zero_pri));

        case SP::zero_s: return std::make_unique<LZeroLocal>(trackStack(readInt32()));
        case SP::zero:   return std::make_unique<LZeroGlobal>(trackGlobal(readInt32()));

        case SP::eq: case SP::neq: case SP::sleq:
        case SP::sgeq: case SP::sgrtr: case SP::sless:
            return std::make_unique<LBinary>(op, Register::Pri, Register::Alt);

        case SP::eq_c_pri:
        case SP::eq_c_alt:
            return std::make_unique<LEqualConstant>(regOf(SP::eq_c_pri), readInt32());

        case SP::inc:   return std::make_unique<LIncGlobal>(trackGlobal(readInt32()));
        case SP::dec:   return std::make_unique<LDecGlobal>(trackGlobal(readInt32()));
        case SP::inc_s: return std::make_unique<LIncLocal>(trackStack(readInt32()));
        case SP::dec_s: return std::make_unique<LDecLocal>(trackStack(readInt32()));
        case SP::inc_i: return std::make_unique<LIncI>();

        case SP::inc_pri:
        case SP::inc_alt:
            return std::make_unique<LIncReg>(regOf(SP::inc_pri));

        case SP::dec_pri:
        case SP::dec_alt:
            return std::make_unique<LDecReg>(regOf(SP::dec_pri));

        case SP::dec_i: return std::make_unique<LDecI>();

        case SP::fill:   return std::make_unique<LFill>(readInt32());
        case SP::bounds: return std::make_unique<LBounds>(readInt32());

        case SP::swap_pri:
        case SP::swap_alt:
            return std::make_unique<LSwap>(regOf(SP::swap_pri));

        case SP::push_adr: return std::make_unique<LPushStackAddress>(trackStack(readInt32()));

        case SP::sysreq_c: {
            uint32_t index = readUInt32();
            int64_t prePeep = pc_;
            SPOpcode nextOp = readOp();
            int32_t nextValue = readInt32();
            auto* lastPush = static_cast<LPushConstant*>(lir_->instructions.back().get());
            int64_t argSize = lastPush->val();
            if (!file_->PassArgCountAsSize()) argSize *= 4;
            argSize += 4;
            assert(nextOp == SP::stack && (int64_t)nextValue == argSize);
            if (nextOp != SP::stack || (int64_t)nextValue != argSize) pc_ = prePeep;
            return std::make_unique<LSysReq>(file_->natives()[index].get());
        }

        case SP::sysreq_n: {
            uint32_t index = readUInt32();
            add(std::make_unique<LPushConstant>((int32_t)readUInt32()));
            return std::make_unique<LSysReq>(file_->natives()[index].get());
        }

        case SP::dbreak: return std::make_unique<LDebugBreak>();

        case SP::push2_s: {
            add(std::make_unique<LPushLocal>(trackStack(readInt32())));
            return std::make_unique<LPushLocal>(trackStack(readInt32()));
        }
        case SP::push2_adr: {
            add(std::make_unique<LPushStackAddress>(trackStack(readInt32())));
            return std::make_unique<LPushStackAddress>(trackStack(readInt32()));
        }
        case SP::push2_c: {
            add(std::make_unique<LPushConstant>(readInt32()));
            return std::make_unique<LPushConstant>(readInt32());
        }
        case SP::push2: {
            add(std::make_unique<LPushGlobal>(trackGlobal(readInt32())));
            return std::make_unique<LPushGlobal>(trackGlobal(readInt32()));
        }
        case SP::push3_s: {
            add(std::make_unique<LPushLocal>(trackStack(readInt32())));
            add(std::make_unique<LPushLocal>(trackStack(readInt32())));
            return std::make_unique<LPushLocal>(trackStack(readInt32()));
        }
        case SP::push3_adr: {
            add(std::make_unique<LPushStackAddress>(trackStack(readInt32())));
            add(std::make_unique<LPushStackAddress>(trackStack(readInt32())));
            return std::make_unique<LPushStackAddress>(trackStack(readInt32()));
        }
        case SP::push3_c: {
            add(std::make_unique<LPushConstant>(readInt32()));
            add(std::make_unique<LPushConstant>(readInt32()));
            return std::make_unique<LPushConstant>(readInt32());
        }
        case SP::push3: {
            add(std::make_unique<LPushGlobal>(trackGlobal(readInt32())));
            add(std::make_unique<LPushGlobal>(trackGlobal(readInt32())));
            return std::make_unique<LPushGlobal>(trackGlobal(readInt32()));
        }
        case SP::push4_s: {
            for (int k = 0; k < 3; k++) add(std::make_unique<LPushLocal>(trackStack(readInt32())));
            return std::make_unique<LPushLocal>(trackStack(readInt32()));
        }
        case SP::push4_adr: {
            for (int k = 0; k < 3; k++) add(std::make_unique<LPushStackAddress>(trackStack(readInt32())));
            return std::make_unique<LPushStackAddress>(trackStack(readInt32()));
        }
        case SP::push4_c: {
            for (int k = 0; k < 3; k++) add(std::make_unique<LPushConstant>(readInt32()));
            return std::make_unique<LPushConstant>(readInt32());
        }
        case SP::push4: {
            for (int k = 0; k < 3; k++) add(std::make_unique<LPushGlobal>(trackGlobal(readInt32())));
            return std::make_unique<LPushGlobal>(trackGlobal(readInt32()));
        }
        case SP::push5_s: {
            for (int k = 0; k < 4; k++) add(std::make_unique<LPushLocal>(trackStack(readInt32())));
            return std::make_unique<LPushLocal>(trackStack(readInt32()));
        }
        case SP::push5_c: {
            for (int k = 0; k < 4; k++) add(std::make_unique<LPushConstant>(readInt32()));
            return std::make_unique<LPushConstant>(readInt32());
        }
        case SP::push5_adr: {
            for (int k = 0; k < 4; k++) add(std::make_unique<LPushStackAddress>(trackStack(readInt32())));
            return std::make_unique<LPushStackAddress>(trackStack(readInt32()));
        }
        case SP::push5: {
            for (int k = 0; k < 4; k++) add(std::make_unique<LPushGlobal>(trackGlobal(readInt32())));
            return std::make_unique<LPushGlobal>(trackGlobal(readInt32()));
        }

        case SP::load_both: {
            add(std::make_unique<LLoadGlobal>(trackGlobal(readInt32()), Register::Pri));
            return std::make_unique<LLoadGlobal>(trackGlobal(readInt32()), Register::Alt);
        }
        case SP::load_s_both: {
            add(std::make_unique<LLoadLocal>(trackStack(readInt32()), Register::Pri));
            return std::make_unique<LLoadLocal>(trackStack(readInt32()), Register::Alt);
        }

        case SP::const_: {
            int64_t a = trackGlobal(readInt32());
            int64_t v = readInt32();
            return std::make_unique<LStoreGlobalConstant>(a, v);
        }
        case SP::const_s: {
            int64_t a = trackStack(readInt32());
            int64_t v = readInt32();
            return std::make_unique<LStoreLocalConstant>(a, v);
        }

        case SP::heap: return std::make_unique<LHeap>(readInt32());
        case SP::movs: return std::make_unique<LMemCopy>(readInt32());

        case SP::switch_: {
            int64_t table = readUInt32();
            int64_t savePc = pc_;
            pc_ = table;
            SPOpcode casetbl = SPOpcodeFromInt((int)readUInt32());
            assert(casetbl == SP::casetbl);
            (void)casetbl;
            int32_t ncases = readInt32();
            int64_t defaultCase = readUInt32();
            std::vector<SwitchCase> cases;
            for (int32_t i = 0; i < ncases; i++) {
                int32_t value = readInt32();
                int64_t pc = readUInt32();
                LBlock* target = prepareJumpTarget(pc);
                bool multiple = false;
                for (auto& c : cases) {
                    if (c.target == target) { c.addValue(value); multiple = true; break; }
                }
                if (!multiple) cases.emplace_back(value, target);
            }
            pc_ = savePc;
            return std::make_unique<LSwitch>(prepareJumpTarget(defaultCase), std::move(cases));
        }

        case SP::casetbl: {
            int32_t ncases = readInt32();
            pc_ += (int64_t)ncases * 8 + 4;
            return std::make_unique<LDebugBreak>();
        }

        case SP::genarray:   return std::make_unique<LGenArray>(readInt32(), false);
        case SP::genarray_z: return std::make_unique<LGenArray>(readInt32(), true);

        case SP::tracker_pop_setheap: return std::make_unique<LTrackerPopSetHeap>();
        case SP::tracker_push_c:      return std::make_unique<LTrackerPushC>(readInt32());
        case SP::stradjust_pri:       return std::make_unique<LStradjustPri>();

        case SP::stackadjust: {
            int32_t v = readInt32();
            assert(v <= 0);
            return std::make_unique<LStackAdjust>(v);
        }

        case SP::heap_save:    return std::make_unique<LHeapSave>();
        case SP::heap_restore: return std::make_unique<LHeapRestore>();

        case SP::initarray_pri:
        case SP::initarray_alt:
            return std::make_unique<LInitArray>(regOf(SP::initarray_pri),
                readInt32(), readInt32(), readInt32(), readInt32(), readInt32());

        case SP::nop: return std::make_unique<LDebugBreak>();
        case SP::halt: readInt32(); return std::make_unique<LDebugBreak>();

        case SP::lctrl: return std::make_unique<LLoadCtrl>(readInt32());
        case SP::sctrl: return std::make_unique<LStoreCtrl>(readInt32());

        case SP::line:   readUInt32(); readUInt32(); return std::make_unique<LDebugBreak>();
        case SP::file: {
            int64_t num = readUInt32();
            readUInt32();
            pc_ += num - 4;
            return std::make_unique<LDebugBreak>();
        }
        case SP::srange: readUInt32(); readUInt32(); return std::make_unique<LDebugBreak>();
        case SP::symtag: readUInt32(); return std::make_unique<LDebugBreak>();
        case SP::symbol: {
            int64_t num = readUInt32();
            readUInt32(); readUInt32();
            pc_ += num - 8;
            return std::make_unique<LDebugBreak>();
        }

        default:
            throw std::runtime_error(std::string("Unrecognized opcode: ") + std::to_string((int)op));
        }
    }

    void MethodParser::readAll() {
        lir_->entry_pc = pc_;
        if (need_proc_) {
            while ((size_t)pc_ < file_->code().bytes().size()) {
                current_pc_ = pc_;
                SPOpcode op = readOp();
                if (op == SPOpcode::proc) break;
                if (op == SPOpcode::symtag) { add(readInstruction(op)); continue; }
                throw std::runtime_error(Format("invalid method, first op must be PROC, got %d", (int)op));
            }
        }
        while ((size_t)pc_ < file_->code().bytes().size()) {
            current_pc_ = pc_;
            SPOpcode op = readOp();
            if (op == SPOpcode::proc || op == SPOpcode::endproc) break;
            add(readInstruction(op));
        }
        lir_->exit_pc = pc_;
    }

    void MethodParser::readStateTable() {
        lir_->entry_pc = pc_;
        current_pc_ = pc_;
        SPOpcode op = readOp();
        assert(op == SPOpcode::load_pri);
        add(readInstruction(op));

        current_pc_ = pc_;
        op = readOp();
        assert(op == SPOpcode::switch_);
        add(readInstruction(op));

        while ((size_t)pc_ < file_->code().bytes().size()) {
            current_pc_ = pc_;
            op = readOp();
            if (op != SPOpcode::casetbl) break;
            add(readInstruction(op));
        }
        lir_->exit_pc = current_pc_;

        auto* state_var = static_cast<LLoadGlobal*>(lir_->instructions[0].get());
        Variable* var = file_->lookupGlobal(state_var->address());
        if (var) {
            var->setName("g_statevar_" + std::to_string(var->address()));
            var->markAsStateVariable();
        }

        auto* function_list = static_cast<LSwitch*>(lir_->instructions[1].get());
        Function* default_func = file_->addFunction(function_list->defaultCase()->pc());
        default_func->setName(func_->name());
        default_func->setStateAddr(state_var->address());
        default_func->setTag(func_->returnTag());
        default_func->setTagId(func_->tag_id());

        for (size_t i = 0; i < function_list->numCases(); i++) {
            const SwitchCase& c = function_list->getCase(i);
            assert(c.numValues() == 1);
            int64_t state_id = c.value(0);
            int64_t start_addr = c.target->pc();
            Function* state_func = file_->addFunction(start_addr);
            state_func->setName(func_->name());
            state_func->setStateId((int16_t)state_id);
            state_func->setStateAddr(state_var->address());
            state_func->setTag(func_->returnTag());
            state_func->setTagId(func_->tag_id());
        }
    }

    namespace {
        class BlockBuilder {
        public:
            BlockBuilder(MethodParser::LIR* lir) : lir_(lir) { block_ = lir_->entry; }

            LBlock* parse() {
                for (size_t i = 0; i < lir_->instructions.size(); i++) {
                    LInstruction* ins = lir_->instructions[i].get();

                    if (lir_->isTarget(ins->pc())) {
                        LBlock* next = lir_->blockOfTarget(ins->pc());
                        if (block_ != next) {
                            if (block_ != nullptr) {
                                assert(!pending_.back()->isControl());
                                pending_.push_back(std::make_unique<LGoto>(next));
                                next->addPredecessor(block_);
                            }
                            transitionBlocks(next);
                        }
                    }
                    if (block_ == nullptr) continue;

                    auto insOwned = std::move(lir_->instructions[i]);
                    LInstruction* raw = insOwned.get();
                    pending_.push_back(std::move(insOwned));

                    switch (raw->op()) {
                    case Opcode::Return:
                        transitionBlocks(nullptr);
                        break;
                    case Opcode::Jump: {
                        auto* j = static_cast<LJump*>(raw);
                        j->target()->addPredecessor(block_);
                        transitionBlocks(nullptr);
                        break;
                    }
                    case Opcode::JumpCondition: {
                        auto* jcc = static_cast<LJumpCondition*>(raw);
                        jcc->trueTarget()->addPredecessor(block_);
                        jcc->falseTarget()->addPredecessor(block_);
                        assert(i + 1 < lir_->instructions.size());
                        (void)i;
                        transitionBlocks(nullptr);
                        break;
                    }
                    case Opcode::Switch: {
                        auto* sw = static_cast<LSwitch*>(raw);
                        for (size_t k = 0; k < sw->numSuccessors(); k++)
                            sw->getSuccessor(k)->addPredecessor(block_);
                        transitionBlocks(nullptr);
                        break;
                    }
                    default: break;
                    }
                }
                return lir_->entry;
            }

            std::vector<std::unique_ptr<LBlock>> takeOwnedBlocks() { return std::move(owned_extra_); }

        private:
            void transitionBlocks(LBlock* next) {
                assert(pending_.empty() || block_ != nullptr);
                if (block_ != nullptr) {
                    assert(pending_.back()->isControl());
                    block_->setInstructions(std::move(pending_));
                    pending_.clear();
                }
                block_ = next;
            }

            MethodParser::LIR* lir_;
            LBlock* block_ = nullptr;
            std::vector<std::unique_ptr<LInstruction>> pending_;
            std::vector<std::unique_ptr<LBlock>> owned_extra_;
        };
    }

    bool MethodParser::containsBlock(const std::vector<LBlock*>& blocks, LBlock* needle) {
        for (LBlock* b : blocks) if (b == needle) return true;
        return false;
    }

    std::unique_ptr<LGraph> MethodParser::buildBlocks() {
        auto entryOwned = std::make_unique<LBlock>(lir_->entry_pc);
        lir_->entry = entryOwned.get();

        BlockBuilder builder(lir_.get());
        builder.parse();

        auto graph = std::make_unique<LGraph>();
        graph->owned_blocks.push_back(std::move(entryOwned));
        for (auto& b : lir_->owned_targets) graph->owned_blocks.push_back(std::move(b));
        for (auto& b : builder.takeOwnedBlocks()) graph->owned_blocks.push_back(std::move(b));

        auto blocks = BlockAnalysis::Order(lir_->entry);

        for (LBlock* block : blocks) {
            size_t numPred = block->numPredecessors();
            for (size_t i = 0; i < numPred; i++) {
                if (!containsBlock(blocks, block->getPredecessor(i))) {
                    block->removePredecessor(block->getPredecessor(i));
                    numPred--;
                }
            }
        }

        if (!BlockAnalysis::IsReducible(blocks))
            throw std::runtime_error("control flow graph is not reducible");

        BlockAnalysis::SplitCriticalEdges(blocks, *graph);
        blocks = BlockAnalysis::Order(lir_->entry);
        assert(BlockAnalysis::IsReducible(blocks));

        BlockAnalysis::ComputeDominators(blocks);
        BlockAnalysis::ComputeImmediateDominators(blocks);
        BlockAnalysis::ComputeDominatorTree(blocks);

        LBlock* newEntry = BlockAnalysis::EnforceStackBalance(file_, blocks);
        if (newEntry) {
            newEntry = BlockAnalysis::FollowGoto(newEntry);
            MethodParser subParser(file_, func_, newEntry->pc());
            subParser.readAll();
            auto subGraph = subParser.buildBlocks();
            subGraph->entry->setPC(lir_->entry_pc);
            return subGraph;
        }
        BlockAnalysis::FindLoops(blocks);

        graph->entry = blocks[0];
        graph->blocks = std::move(blocks);
        graph->nargs = getNumArgs();
        return graph;
    }

    MethodParser::MethodParser(PawnFile* file, Function* func, int64_t pc)
        : file_(file), func_(func), pc_(pc), current_pc_(0),
        lir_(std::make_unique<LIR>()), need_proc_(false) {
    }

    MethodParser::MethodParser(PawnFile* file, Function* func)
        : file_(file), func_(func), pc_(func->address()), current_pc_(0),
        lir_(std::make_unique<LIR>()), need_proc_(true) {
    }

    bool MethodParser::preprocess() {
        SPOpcode op = peekOp();
        if (op == SPOpcode::load_pri) {
            readStateTable();
            func_->setCodeEnd(getExitPC());
            return false;
        }
        readAll();
        return true;
    }

    std::unique_ptr<LGraph> MethodParser::parse() {
        if (!preprocess()) return nullptr;
        return buildBlocks();
    }

}