#include "instructions.hpp"

#include <cstdio>
#include <sstream>

namespace lysis {

    static std::string hex64(uint64_t v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llx", (unsigned long long)v);
        return buf;
    }

    void LAddConstant::print(std::ostream& o) const { o << "add.pri " << amount_; }
    void LBinary::print(std::ostream& o) const { o << "binary <spop:" << (int)spop_ << ">"; }
    void LBounds::print(std::ostream& o) const { o << "bounds.pri " << amount_; }
    void LCall::print(std::ostream& o) const { o << "call 0x" << hex64((uint64_t)address_); }
    void LConstant::print(std::ostream& o) const { o << "const." << RegisterName(reg()) << " " << val_; }
    void LDebugBreak::print(std::ostream& o) const { o << "break"; }
    void LDecGlobal::print(std::ostream& o) const { o << "dec " << address_; }
    void LDecI::print(std::ostream& o) const { o << "dec.i    ; [pri] = [pri] + 1"; }
    void LDecLocal::print(std::ostream& o) const { o << "dec.s " << offset(); }
    void LDecReg::print(std::ostream& o) const { o << "dec." << RegisterName(reg()); }
    void LEqualConstant::print(std::ostream& o) const { o << "eq.c." << RegisterName(reg()) << " " << value_; }
    void LExchange::print(std::ostream& o) const { o << "xchg"; }
    void LFill::print(std::ostream& o) const { o << "fill alt, " << amount_; }
    void LGenArray::print(std::ostream& o) const { o << "genarray" << (autozero_ ? ".z " : " ") << dims_; }
    void LGoto::print(std::ostream& o) const { o << "goto block" << target()->id(); }
    void LHeap::print(std::ostream& o) const { o << "heap " << amount_; }
    void LHeapRestore::print(std::ostream& o) const { o << "heap.restore"; }
    void LHeapSave::print(std::ostream& o) const { o << "heap.save"; }
    void LIncGlobal::print(std::ostream& o) const { o << "inc " << address_; }
    void LIncI::print(std::ostream& o) const { o << "inc.i    ; [pri] = [pri] + 1"; }
    void LIncLocal::print(std::ostream& o) const { o << "inc.s " << offset(); }
    void LIncReg::print(std::ostream& o) const { o << "inc." << RegisterName(reg()); }
    void LIndexAddress::print(std::ostream& o) const { o << "idxaddr " << shift_ << " ; pri=alt+(pri<<" << shift_ << ")"; }
    void LJump::print(std::ostream& o) const { o << "jump block" << target()->id(); }
    void LLoad::print(std::ostream& o) const { o << "load.i." << bytes_ << "   ; pri = [pri]"; }
    void LLoadGlobal::print(std::ostream& o) const { o << "load." << RegisterName(reg()) << " " << address_; }
    void LLoadIndex::print(std::ostream& o) const { o << "lidx." << shift_ << " ; [pri=alt+(pri<<" << shift_ << ")]"; }
    void LLoadLocal::print(std::ostream& o) const { o << "load.s." << RegisterName(reg()) << " " << offset(); }
    void LLoadLocalRef::print(std::ostream& o) const { o << "lref.s." << RegisterName(reg()) << " " << offset(); }
    void LMemCopy::print(std::ostream& o) const { o << "movs " << bytes_; }
    void LMulConstant::print(std::ostream& o) const { o << "mul.pri " << amount_; }
    void LPop::print(std::ostream& o) const { o << "pop." << RegisterName(reg()); }
    void LPushConstant::print(std::ostream& o) const { o << "push.c " << val_; }
    void LPushGlobal::print(std::ostream& o) const { o << "push " << address_; }
    void LPushLocal::print(std::ostream& o) const { o << "push.s " << offset_; }
    void LPushReg::print(std::ostream& o) const { o << "push." << RegisterName(reg()); }
    void LPushStackAddress::print(std::ostream& o) const { o << "push.adr " << offset(); }
    void LReturn::print(std::ostream& o) const { o << "return"; }
    void LShiftLeftConstant::print(std::ostream& o) const { o << "shl.c." << RegisterName(reg()) << " " << val_; }
    void LStack::print(std::ostream& o) const { o << "stack " << val_; }
    void LStackAddress::print(std::ostream& o) const { o << "addr." << RegisterName(reg()) << " " << offset(); }
    void LStackAdjust::print(std::ostream& o) const { o << "stackadjust " << value_; }
    void LStore::print(std::ostream& o) const { o << "stor.i." << bytes_ << "   ; [alt] = pri"; }
    void LStoreGlobal::print(std::ostream& o) const { o << "stor." << RegisterName(reg()) << " " << address_; }
    void LStoreGlobalConstant::print(std::ostream& o) const { o << "const [" << address_ << "] = value"; }
    void LStoreLocal::print(std::ostream& o) const { o << "stor.s." << RegisterName(reg()) << " " << offset(); }
    void LStoreLocalConstant::print(std::ostream& o) const { o << "const.s [" << address_ << "] = value"; }
    void LStoreLocalRef::print(std::ostream& o) const { o << "sref.s." << RegisterName(reg()) << " " << offset(); }
    void LStradjustPri::print(std::ostream& o) const { o << "stradjust.pri"; }
    void LSwap::print(std::ostream& o) const { o << "swap." << RegisterName(reg()); }
    void LSysReq::print(std::ostream& o) const { o << "sysreq " << (native_ ? native_->name() : "<null>"); }
    void LTrackerPopSetHeap::print(std::ostream& o) const { o << "tracker.pop.setheap"; }
    void LTrackerPushC::print(std::ostream& o) const { o << "tracker.push.c " << value_; }
    void LUnary::print(std::ostream& o) const { o << "unary <spop:" << (int)spop_ << ">"; }
    void LZeroGlobal::print(std::ostream& o) const { o << "zero " << address_; }
    void LZeroLocal::print(std::ostream& o) const { o << "zero.s " << address_; }

    void LInitArray::print(std::ostream& o) const {
        o << "initarray." << RegisterName(reg()) << " "
            << template_ << " " << ivcount_ << " " << copycount_ << " "
            << fillcount_ << " " << fillvalue_;
    }

    void LJumpCondition::print(std::ostream& o) const {
        o << "jcc <spop:" << (int)op_ << "> block" << trueTarget()->id()
            << " (block" << falseTarget()->id() << ")";
    }

    void LLoadCtrl::print(std::ostream& o) const {
        static const char* names[] = { "COD","DAT","HEA","STP","STK","FRM","CIP" };
        const char* n = (idx_ >= 0 && idx_ < 7) ? names[idx_] : "?";
        o << "lctrl " << idx_ << "   ; PRI = " << n;
    }

    void LMove::print(std::ostream& o) const {
        Register other = (reg() == Register::Pri) ? Register::Alt : Register::Pri;
        o << "move." << RegisterName(reg()) << ", " << RegisterName(other);
    }

    void LStoreCtrl::print(std::ostream& o) const {
        static const char* names[] = { "COD","DAT","HEA","STP","STK","FRM","CIP" };
        const char* n = (idx_ >= 0 && idx_ < 7) ? names[idx_] : "?";
        o << "sctrl " << idx_ << "   ; " << n << " = PRI";
    }

    void LSwitch::print(std::ostream& o) const {
        o << "switch.pri -> " << defaultCase_->id();
        if (!cases_.empty()) o << ",";
        for (size_t i = 0; i < cases_.size(); i++) {
            o << cases_[i].target->id();
            if (i != cases_.size() - 1) o << ",";
        }
    }

} 