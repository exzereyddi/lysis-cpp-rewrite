#pragma once

#include "../core/lstructure.hpp"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace lysis {

    enum class Opcode {
        LoadLocal, StoreLocal, LoadLocalRef, StoreLocalRef, Load, Constant,
        StackAddress, Store, IndexAddress, Move, PushReg, PushConstant, Pop,
        Stack, Return, Jump, JumpCondition, AddConstant, MulConstant,
        ZeroGlobal, IncGlobal, DecGlobal, IncLocal, DecLocal, IncI, IncReg,
        DecI, DecReg, Fill, Bounds, SysReq, Swap, PushStackAddress, DebugBreak,
        Goto, PushLocal, Exchange, Binary, ShiftLeftConstant, PushGlobal,
        StoreGlobal, LoadGlobal, Call, EqualConstant, LoadIndex, Unary,
        StoreGlobalConstant, StoreLocalConstant, ZeroLocal, Heap, MemCopy,
        Switch, GenArray, StackAdjust, LoadCtrl, StoreCtrl, InitArray
    };

    enum class SPOpcode : int;
    class LBlock;

    class LInstruction {
    public:
        virtual ~LInstruction() = default;
        virtual Opcode op() const = 0;
        virtual void print(std::ostream& out) const = 0;
        virtual bool isControl() const { return false; }
        void setPc(int64_t pc) { pc_ = pc; }
        int64_t pc() const { return pc_; }
        static const char* RegisterName(Register r) { return r == Register::Pri ? "pri" : "alt"; }
    private:
        int64_t pc_ = 0;
    };

    class LInstructionReg : public LInstruction {
    public:
        explicit LInstructionReg(Register r) : reg_(r) {}
        Register reg() const { return reg_; }
    private:
        Register reg_;
    };

    class LInstructionStack : public LInstruction {
    public:
        explicit LInstructionStack(int64_t offset) : offs_(offset) {}
        int64_t offset() const { return offs_; }
    private:
        int64_t offs_;
    };

    class LInstructionRegStack : public LInstruction {
    public:
        LInstructionRegStack(Register r, int64_t offset) : reg_(r), offs_(offset) {}
        Register reg() const { return reg_; }
        int64_t  offset() const { return offs_; }
    private:
        Register reg_;
        int64_t  offs_;
    };

    class LControlInstruction : public LInstruction {
    public:
        LControlInstruction() = default;
        explicit LControlInstruction(std::vector<LBlock*> succ) : successors_(std::move(succ)) {}
        virtual void replaceSuccessor(size_t i, LBlock* b) { successors_[i] = b; }
        virtual size_t numSuccessors() const { return successors_.size(); }
        virtual LBlock* getSuccessor(size_t i) const { return successors_[i]; }
        bool isControl() const override { return true; }
    protected:
        std::vector<LBlock*> successors_;
    };

    class LInstructionJump : public LControlInstruction {
    public:
        LInstructionJump(int64_t target_offs, std::vector<LBlock*> targets)
            : LControlInstruction(std::move(targets)), target_offs_(target_offs) {
        }
        int64_t target_offs() const { return target_offs_; }
    private:
        int64_t target_offs_;
    };

    class LAddConstant : public LInstruction {
    public:
        explicit LAddConstant(int64_t a) : amount_(a) {}
        int64_t amount() const { return amount_; }
        Opcode op() const override { return Opcode::AddConstant; }
        void print(std::ostream& out) const override;
    private:
        int64_t amount_;
    };

    class LBinary : public LInstruction {
    public:
        LBinary(SPOpcode op, Register lhs, Register rhs) : spop_(op), lhs_(lhs), rhs_(rhs) {}
        SPOpcode spop() const { return spop_; }
        Register lhs() const { return lhs_; }
        Register rhs() const { return rhs_; }
        Opcode op() const override { return Opcode::Binary; }
        void print(std::ostream& out) const override;
    private:
        SPOpcode spop_;
        Register lhs_, rhs_;
    };

    class LBounds : public LInstruction {
    public:
        explicit LBounds(int64_t a) : amount_(a) {}
        int64_t amount() const { return amount_; }
        Opcode op() const override { return Opcode::Bounds; }
        void print(std::ostream& out) const override;
    private:
        int64_t amount_;
    };

    class LCall : public LInstruction {
    public:
        explicit LCall(int64_t a) : address_(a) {}
        int64_t address() const { return address_; }
        Opcode op() const override { return Opcode::Call; }
        void print(std::ostream& out) const override;
    private:
        int64_t address_;
    };

    class LConstant : public LInstructionReg {
    public:
        LConstant(int64_t val, Register r) : LInstructionReg(r), val_(val) {}
        int64_t val() const { return val_; }
        Opcode op() const override { return Opcode::Constant; }
        void print(std::ostream& out) const override;
    private:
        int64_t val_;
    };

    class LDebugBreak : public LInstruction {
    public:
        Opcode op() const override { return Opcode::DebugBreak; }
        void print(std::ostream& out) const override;
    };

    class LDecGlobal : public LInstruction {
    public:
        explicit LDecGlobal(int64_t a) : address_(a) {}
        int64_t address() const { return address_; }
        Opcode op() const override { return Opcode::DecGlobal; }
        void print(std::ostream& out) const override;
    private:
        int64_t address_;
    };

    class LDecI : public LInstruction {
    public:
        Opcode op() const override { return Opcode::DecI; }
        void print(std::ostream& out) const override;
    };

    class LDecLocal : public LInstructionStack {
    public:
        explicit LDecLocal(int64_t o) : LInstructionStack(o) {}
        Opcode op() const override { return Opcode::DecLocal; }
        void print(std::ostream& out) const override;
    };

    class LDecReg : public LInstructionReg {
    public:
        explicit LDecReg(Register r) : LInstructionReg(r) {}
        Opcode op() const override { return Opcode::DecReg; }
        void print(std::ostream& out) const override;
    };

    class LEqualConstant : public LInstructionReg {
    public:
        LEqualConstant(Register r, int64_t v) : LInstructionReg(r), value_(v) {}
        int64_t value() const { return value_; }
        Opcode op() const override { return Opcode::EqualConstant; }
        void print(std::ostream& out) const override;
    private:
        int64_t value_;
    };

    class LExchange : public LInstruction {
    public:
        Opcode op() const override { return Opcode::Exchange; }
        void print(std::ostream& out) const override;
    };

    class LFill : public LInstruction {
    public:
        explicit LFill(int64_t a) : amount_(a) {}
        int64_t amount() const { return amount_; }
        Opcode op() const override { return Opcode::Fill; }
        void print(std::ostream& out) const override;
    private:
        int64_t amount_;
    };

    class LGenArray : public LInstruction {
    public:
        LGenArray(int32_t dims, bool autozero) : dims_(dims), autozero_(autozero) {}
        bool autozero() const { return autozero_; }
        int32_t dims() const { return dims_; }
        Opcode op() const override { return Opcode::GenArray; }
        void print(std::ostream& out) const override;
    private:
        int32_t dims_;
        bool autozero_;
    };

    class LGoto : public LControlInstruction {
    public:
        explicit LGoto(LBlock* t) : LControlInstruction({ t }) {}
        LBlock* target() const { return getSuccessor(0); }
        Opcode op() const override { return Opcode::Goto; }
        void print(std::ostream& out) const override;
    };

    class LHeap : public LInstruction {
    public:
        explicit LHeap(int64_t a) : amount_(a) {}
        int64_t amount() const { return amount_; }
        Opcode op() const override { return Opcode::Heap; }
        void print(std::ostream& out) const override;
    private:
        int64_t amount_;
    };

    class LHeapRestore : public LInstruction {
    public:
        Opcode op() const override { return Opcode::DebugBreak; }
        void print(std::ostream& out) const override;
    };

    class LHeapSave : public LInstruction {
    public:
        Opcode op() const override { return Opcode::DebugBreak; }
        void print(std::ostream& out) const override;
    };

    class LIncGlobal : public LInstruction {
    public:
        explicit LIncGlobal(int64_t a) : address_(a) {}
        int64_t address() const { return address_; }
        Opcode op() const override { return Opcode::IncGlobal; }
        void print(std::ostream& out) const override;
    private:
        int64_t address_;
    };

    class LIncI : public LInstruction {
    public:
        Opcode op() const override { return Opcode::IncI; }
        void print(std::ostream& out) const override;
    };

    class LIncLocal : public LInstructionStack {
    public:
        explicit LIncLocal(int64_t o) : LInstructionStack(o) {}
        Opcode op() const override { return Opcode::IncLocal; }
        void print(std::ostream& out) const override;
    };

    class LIncReg : public LInstructionReg {
    public:
        explicit LIncReg(Register r) : LInstructionReg(r) {}
        Opcode op() const override { return Opcode::IncReg; }
        void print(std::ostream& out) const override;
    };

    class LIndexAddress : public LInstruction {
    public:
        explicit LIndexAddress(int64_t s) : shift_(s) {}
        int64_t shift() const { return shift_; }
        Opcode op() const override { return Opcode::IndexAddress; }
        void print(std::ostream& out) const override;
    private:
        int64_t shift_;
    };

    class LInitArray : public LInstruction {
    public:
        LInitArray(Register r, int32_t t, int32_t iv, int32_t copy, int32_t fill, int32_t fillv)
            : reg_(r), template_(t), ivcount_(iv), copycount_(copy),
            fillcount_(fill), fillvalue_(fillv) {
        }
        Register reg() const { return reg_; }
        int32_t templ() const { return template_; }
        int32_t ivcount() const { return ivcount_; }
        int32_t copycount() const { return copycount_; }
        int32_t fillcount() const { return fillcount_; }
        int32_t fillvalue() const { return fillvalue_; }
        Opcode op() const override { return Opcode::InitArray; }
        void print(std::ostream& out) const override;
    private:
        Register reg_;
        int32_t template_, ivcount_, copycount_, fillcount_, fillvalue_;
    };

    class LJump : public LInstructionJump {
    public:
        LJump(LBlock* t, int64_t off) : LInstructionJump(off, { t }) {}
        LBlock* target() const { return getSuccessor(0); }
        Opcode op() const override { return Opcode::Jump; }
        void print(std::ostream& out) const override;
    };

    class LJumpCondition : public LInstructionJump {
    public:
        LJumpCondition(SPOpcode op, LBlock* tt, LBlock* ft, int64_t off)
            : LInstructionJump(off, { tt, ft }), op_(op) {
        }
        SPOpcode spop() const { return op_; }
        LBlock* trueTarget()  const { return getSuccessor(0); }
        LBlock* falseTarget() const { return getSuccessor(1); }
        Opcode op() const override { return Opcode::JumpCondition; }
        void print(std::ostream& out) const override;
    private:
        SPOpcode op_;
    };

    class LLoad : public LInstruction {
    public:
        explicit LLoad(int64_t b) : bytes_(b) {}
        int64_t bytes() const { return bytes_; }
        Opcode op() const override { return Opcode::Load; }
        void print(std::ostream& out) const override;
    private:
        int64_t bytes_;
    };

    class LLoadCtrl : public LInstruction {
    public:
        explicit LLoadCtrl(int32_t i) : idx_(i) {}
        int32_t ctrlregindex() const { return idx_; }
        Opcode op() const override { return Opcode::LoadCtrl; }
        void print(std::ostream& out) const override;
    private:
        int32_t idx_;
    };

    class LLoadGlobal : public LInstructionReg {
    public:
        LLoadGlobal(int64_t a, Register r) : LInstructionReg(r), address_(a) {}
        int64_t address() const { return address_; }
        Opcode op() const override { return Opcode::LoadGlobal; }
        void print(std::ostream& out) const override;
    private:
        int64_t address_;
    };

    class LLoadIndex : public LInstruction {
    public:
        explicit LLoadIndex(int64_t s) : shift_(s) {}
        int64_t shift() const { return shift_; }
        Opcode op() const override { return Opcode::LoadIndex; }
        void print(std::ostream& out) const override;
    private:
        int64_t shift_;
    };

    class LLoadLocal : public LInstructionRegStack {
    public:
        LLoadLocal(int64_t o, Register r) : LInstructionRegStack(r, o) {}
        Opcode op() const override { return Opcode::LoadLocal; }
        void print(std::ostream& out) const override;
    };

    class LLoadLocalRef : public LInstructionRegStack {
    public:
        LLoadLocalRef(int64_t o, Register r) : LInstructionRegStack(r, o) {}
        Opcode op() const override { return Opcode::LoadLocalRef; }
        void print(std::ostream& out) const override;
    };

    class LMemCopy : public LInstruction {
    public:
        explicit LMemCopy(int64_t b) : bytes_(b) {}
        int64_t bytes() const { return bytes_; }
        Opcode op() const override { return Opcode::MemCopy; }
        void print(std::ostream& out) const override;
    private:
        int64_t bytes_;
    };

    class LMove : public LInstructionReg {
    public:
        explicit LMove(Register r) : LInstructionReg(r) {}
        Opcode op() const override { return Opcode::Move; }
        void print(std::ostream& out) const override;
    };

    class LMulConstant : public LInstruction {
    public:
        explicit LMulConstant(int64_t a) : amount_(a) {}
        int64_t amount() const { return amount_; }
        Opcode op() const override { return Opcode::MulConstant; }
        void print(std::ostream& out) const override;
    private:
        int64_t amount_;
    };

    class LPop : public LInstructionReg {
    public:
        explicit LPop(Register r) : LInstructionReg(r) {}
        Opcode op() const override { return Opcode::Pop; }
        void print(std::ostream& out) const override;
    };

    class LPushConstant : public LInstruction {
    public:
        explicit LPushConstant(int64_t v) : val_(v) {}
        int64_t val() const { return val_; }
        Opcode op() const override { return Opcode::PushConstant; }
        void print(std::ostream& out) const override;
    private:
        int64_t val_;
    };

    class LPushGlobal : public LInstruction {
    public:
        explicit LPushGlobal(int64_t a) : address_(a) {}
        int64_t address() const { return address_; }
        Opcode op() const override { return Opcode::PushGlobal; }
        void print(std::ostream& out) const override;
    private:
        int64_t address_;
    };

    class LPushLocal : public LInstruction {
    public:
        explicit LPushLocal(int64_t o) : offset_(o) {}
        int64_t offset() const { return offset_; }
        Opcode op() const override { return Opcode::PushLocal; }
        void print(std::ostream& out) const override;
    private:
        int64_t offset_;
    };

    class LPushReg : public LInstructionReg {
    public:
        explicit LPushReg(Register r) : LInstructionReg(r) {}
        Opcode op() const override { return Opcode::PushReg; }
        void print(std::ostream& out) const override;
    };

    class LPushStackAddress : public LInstructionStack {
    public:
        explicit LPushStackAddress(int64_t o) : LInstructionStack(o) {}
        Opcode op() const override { return Opcode::PushStackAddress; }
        void print(std::ostream& out) const override;
    };

    class LReturn : public LControlInstruction {
    public:
        Opcode op() const override { return Opcode::Return; }
        void print(std::ostream& out) const override;
    };

    class LShiftLeftConstant : public LInstructionReg {
    public:
        LShiftLeftConstant(int64_t v, Register r) : LInstructionReg(r), val_(v) {}
        int64_t val() const { return val_; }
        Opcode op() const override { return Opcode::ShiftLeftConstant; }
        void print(std::ostream& out) const override;
    private:
        int64_t val_;
    };

    class LStack : public LInstruction {
    public:
        explicit LStack(int64_t v) : val_(v) {}
        int64_t amount() const { return val_; }
        Opcode op() const override { return Opcode::Stack; }
        void print(std::ostream& out) const override;
    private:
        int64_t val_;
    };

    class LStackAddress : public LInstructionRegStack {
    public:
        LStackAddress(int64_t o, Register r) : LInstructionRegStack(r, o) {}
        Opcode op() const override { return Opcode::StackAddress; }
        void print(std::ostream& out) const override;
    };

    class LStackAdjust : public LInstruction {
    public:
        explicit LStackAdjust(int32_t v) : value_(v) {}
        int32_t value() const { return value_; }
        Opcode op() const override { return Opcode::StackAdjust; }
        void print(std::ostream& out) const override;
    private:
        int32_t value_;
    };

    class LStore : public LInstruction {
    public:
        explicit LStore(int64_t b) : bytes_(b) {}
        int64_t bytes() const { return bytes_; }
        Opcode op() const override { return Opcode::Store; }
        void print(std::ostream& out) const override;
    private:
        int64_t bytes_;
    };

    class LStoreCtrl : public LInstruction {
    public:
        explicit LStoreCtrl(int32_t i) : idx_(i) {}
        int32_t ctrlregindex() const { return idx_; }
        Opcode op() const override { return Opcode::StoreCtrl; }
        void print(std::ostream& out) const override;
    private:
        int32_t idx_;
    };

    class LStoreGlobal : public LInstructionReg {
    public:
        LStoreGlobal(int64_t a, Register r) : LInstructionReg(r), address_(a) {}
        int64_t address() const { return address_; }
        Opcode op() const override { return Opcode::StoreGlobal; }
        void print(std::ostream& out) const override;
    private:
        int64_t address_;
    };

    class LStoreGlobalConstant : public LInstruction {
    public:
        LStoreGlobalConstant(int64_t a, int64_t v) : address_(a), value_(v) {}
        int64_t address() const { return address_; }
        int64_t value() const { return value_; }
        Opcode op() const override { return Opcode::StoreGlobalConstant; }
        void print(std::ostream& out) const override;
    private:
        int64_t address_, value_;
    };

    class LStoreLocal : public LInstructionRegStack {
    public:
        LStoreLocal(Register r, int64_t o) : LInstructionRegStack(r, o) {}
        Opcode op() const override { return Opcode::StoreLocal; }
        void print(std::ostream& out) const override;
    };

    class LStoreLocalConstant : public LInstruction {
    public:
        LStoreLocalConstant(int64_t a, int64_t v) : address_(a), value_(v) {}
        int64_t address() const { return address_; }
        int64_t value() const { return value_; }
        Opcode op() const override { return Opcode::StoreLocalConstant; }
        void print(std::ostream& out) const override;
    private:
        int64_t address_, value_;
    };

    class LStoreLocalRef : public LInstructionRegStack {
    public:
        LStoreLocalRef(Register r, int64_t o) : LInstructionRegStack(r, o) {}
        Opcode op() const override { return Opcode::StoreLocalRef; }
        void print(std::ostream& out) const override;
    };

    class LStradjustPri : public LInstruction {
    public:
        Opcode op() const override { return Opcode::DebugBreak; }
        void print(std::ostream& out) const override;
    };

    class LSwap : public LInstructionReg {
    public:
        explicit LSwap(Register r) : LInstructionReg(r) {}
        Opcode op() const override { return Opcode::Swap; }
        void print(std::ostream& out) const override;
    };

    class SwitchCase {
    public:
        SwitchCase(int64_t v, LBlock* t) : target(t) { values_.push_back(v); }
        int64_t value(size_t i) const { return values_[i]; }
        void addValue(int64_t v) { values_.push_back(v); }
        size_t numValues() const { return values_.size(); }
        const std::vector<int64_t>& values() const { return values_; }
        LBlock* target;
    private:
        std::vector<int64_t> values_;
    };

    class LSwitch : public LControlInstruction {
    public:
        LSwitch(LBlock* def, std::vector<SwitchCase> cases)
            : defaultCase_(def), cases_(std::move(cases)) {
        }
        LBlock* defaultCase() const { return defaultCase_; }
        void replaceSuccessor(size_t i, LBlock* b) override {
            if (i == 0) defaultCase_ = b;
            else cases_[i - 1].target = b;
        }
        size_t numSuccessors() const override { return cases_.size() + 1; }
        LBlock* getSuccessor(size_t i) const override {
            return (i == 0) ? defaultCase_ : cases_[i - 1].target;
        }
        size_t numCases() const { return cases_.size(); }
        SwitchCase& getCase(size_t i) { return cases_[i]; }
        const SwitchCase& getCase(size_t i) const { return cases_[i]; }
        Opcode op() const override { return Opcode::Switch; }
        void print(std::ostream& out) const override;
    private:
        LBlock* defaultCase_;
        std::vector<SwitchCase> cases_;
    };

    class LSysReq : public LInstruction {
    public:
        explicit LSysReq(Native* n) : native_(n) {}
        Native* nativeX() const { return native_; }
        Opcode op() const override { return Opcode::SysReq; }
        void print(std::ostream& out) const override;
    private:
        Native* native_;
    };

    class LTrackerPopSetHeap : public LInstruction {
    public:
        Opcode op() const override { return Opcode::DebugBreak; }
        void print(std::ostream& out) const override;
    };

    class LTrackerPushC : public LInstruction {
    public:
        explicit LTrackerPushC(int32_t v) : value_(v) {}
        Opcode op() const override { return Opcode::DebugBreak; }
        void print(std::ostream& out) const override;
    private:
        int32_t value_;
    };

    class LUnary : public LInstruction {
    public:
        LUnary(SPOpcode op, Register r) : spop_(op), reg_(r) {}
        SPOpcode spop() const { return spop_; }
        Register reg() const { return reg_; }
        Opcode op() const override { return Opcode::Unary; }
        void print(std::ostream& out) const override;
    private:
        SPOpcode spop_;
        Register reg_;
    };

    class LZeroGlobal : public LInstruction {
    public:
        explicit LZeroGlobal(int64_t a) : address_(a) {}
        int64_t address() const { return address_; }
        Opcode op() const override { return Opcode::ZeroGlobal; }
        void print(std::ostream& out) const override;
    private:
        int64_t address_;
    };

    class LZeroLocal : public LInstruction {
    public:
        explicit LZeroLocal(int64_t a) : address_(a) {}
        int64_t address() const { return address_; }
        Opcode op() const override { return Opcode::ZeroLocal; }
        void print(std::ostream& out) const override;
    private:
        int64_t address_;
    };

} 