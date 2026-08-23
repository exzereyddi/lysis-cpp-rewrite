#pragma once

#include "../core/lstructure.hpp"
#include "../core/pawn_file.hpp"
#include "../frontend/sourcepawn.hpp"
#include "instructions.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace lysis {

    class MethodParser {
    public:
        struct LIR {
            LBlock* entry = nullptr;
            std::vector<std::unique_ptr<LInstruction>> instructions;
            std::unordered_map<int64_t, LBlock*> targets;
            std::vector<std::unique_ptr<LBlock>> owned_targets;
            int64_t entry_pc = 0;
            int64_t exit_pc = 0;
            int32_t argDepth = 0;

            bool isTarget(int64_t offs) const { return targets.count(offs) != 0; }
            LBlock* blockOfTarget(int64_t offs) const { return targets.at(offs); }
        };

        MethodParser(PawnFile* file, Function* func);
        bool preprocess();
        std::unique_ptr<LGraph> parse();

        int64_t getExitPC() const { return lir_->exit_pc; }
        int32_t getNumArgs() const {
            return (lir_->argDepth > 0) ? ((int32_t)((lir_->argDepth - 12) / 4)) + 1 : 0;
        }

    private:
        MethodParser(PawnFile* file, Function* func, int64_t pc);

        PawnFile* file_;
        Function* func_;
        int64_t pc_;
        int64_t current_pc_;
        std::unique_ptr<LIR> lir_;
        bool need_proc_;

        int32_t  readInt32();
        uint32_t readUInt32();
        SPOpcode readOp();
        SPOpcode peekOp();

        LInstruction* add(std::unique_ptr<LInstruction> ins);
        LBlock* prepareJumpTarget(int64_t offset);
        int32_t trackStack(int32_t offset);
        int32_t trackGlobal(int32_t addr);

        std::unique_ptr<LInstruction> readInstruction(SPOpcode op);
        void readAll();
        void readStateTable();

        std::unique_ptr<LGraph> buildBlocks();
        static bool containsBlock(const std::vector<LBlock*>& blocks, LBlock* needle);
    };

}