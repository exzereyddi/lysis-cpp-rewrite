#pragma once

#include "../core/util.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lysis {

    enum class SPOpcode : int {
        invalid,
        load_pri, load_alt, load_s_pri, load_s_alt,
        lref_pri, lref_alt, lref_s_pri, lref_s_alt,
        load_i, lodb_i,
        const_pri, const_alt, addr_pri, addr_alt,
        stor_pri, stor_alt, stor_s_pri, stor_s_alt,
        sref_pri, sref_alt, sref_s_pri, sref_s_alt,
        stor_i, strb_i,
        lidx, lidx_b, idxaddr, idxaddr_b,
        align_pri, align_alt,
        lctrl, sctrl,
        move_pri, move_alt, xchg,
        push_pri, push_alt, push_r, push_c, push, push_s,
        pop_pri, pop_alt,
        stack, heap,
        proc, ret, retn,
        call, call_pri,
        jump, jrel,
        jzer, jnz, jeq, jneq, jless, jleq, jgrtr, jgeq,
        jsless, jsleq, jsgrtr, jsgeq,
        shl, shr, sshr,
        shl_c_pri, shl_c_alt, shr_c_pri, shr_c_alt,
        smul, sdiv, sdiv_alt, umul, udiv, udiv_alt,
        add, sub, sub_alt, and_, or_, xor_, not_, neg, invert,
        add_c, smul_c,
        zero_pri, zero_alt, zero, zero_s,
        sign_pri, sign_alt,
        eq, neq, less, leq, grtr, geq,
        sless, sleq, sgrtr, sgeq,
        eq_c_pri, eq_c_alt,
        inc_pri, inc_alt, inc, inc_s, inc_i,
        dec_pri, dec_alt, dec, dec_s, dec_i,
        movs, cmps, fill, halt,
        bounds,
        sysreq_pri, sysreq_c,
        file, line, symbol, srange, jump_pri,
        switch_, casetbl,
        swap_pri, swap_alt, push_adr, nop, sysreq_n, symtag, dbreak,
        push2_c, push2, push2_s, push2_adr,
        push3_c, push3, push3_s, push3_adr,
        push4_c, push4, push4_s, push4_adr,
        push5_c, push5, push5_s, push5_adr,
        load_both, load_s_both,
        const_, const_s,
        sysreq_d, sysreq_nd,
        tracker_push_c, tracker_pop_setheap,
        genarray, genarray_z,
        stradjust_pri, stackadjust, endproc,
        ldgfn_pri, rebase,
        initarray_pri, initarray_alt,
        heap_save, heap_restore,
        fabs, float_, floatadd, floatsub, floatmul, floatdiv,
        rnd_to_nearest, rnd_to_floor, rnd_to_ceil, rnd_to_zero,
        floatcmp,
        sdiv_alt_mod
    };

    constexpr SPOpcode SP_and = SPOpcode::and_;
    constexpr SPOpcode SP_or = SPOpcode::or_;
    constexpr SPOpcode SP_xor = SPOpcode::xor_;
    constexpr SPOpcode SP_not = SPOpcode::not_;
    constexpr SPOpcode SP_switch = SPOpcode::switch_;
    constexpr SPOpcode SP_const = SPOpcode::const_;

    inline SPOpcode SPOpcodeFromInt(int v) { return static_cast<SPOpcode>(v); }

    namespace OpcodeHelpers {
        SPOpcode ConditionToJump(SPOpcode spop, bool onTrue);
        SPOpcode Invert(SPOpcode spop);
    }

    namespace TypeFlag {
        constexpr uint8_t Bool = 0x01;
        constexpr uint8_t Int32 = 0x06;
        constexpr uint8_t Float32 = 0x0c;
        constexpr uint8_t Char8 = 0x0e;
        constexpr uint8_t Any = 0x10;
        constexpr uint8_t TopFunction = 0x11;
        constexpr uint8_t FixedArray = 0x30;
        constexpr uint8_t Array = 0x31;
        constexpr uint8_t Function = 0x32;
        constexpr uint8_t Enum = 0x42;
        constexpr uint8_t Typedef = 0x43;
        constexpr uint8_t Typeset = 0x44;
        constexpr uint8_t Classdef = 0x45;
        constexpr uint8_t EnumStruct = 0x46;
        constexpr uint8_t Void = 0x70;
        constexpr uint8_t Variadic = 0x71;
        constexpr uint8_t ByRef = 0x72;
        constexpr uint8_t Const = 0x73;
    }

    enum class VariableType;

    class RttiType {
    public:
        explicit RttiType(uint8_t typeflag) : typeflag_(typeflag) {}

        uint8_t getTypeFlag() const { return typeflag_; }

        bool isArrayType() const { return typeflag_ == TypeFlag::Array || typeflag_ == TypeFlag::FixedArray; }
        bool isString() const;
        bool isFloat()  const;
        RttiType* getArrayBaseType();
        const RttiType* getArrayBaseType() const;

        void setConst() { isConst_ = true; }
        bool isConst()   const { return isConst_; }
        void setVariadic() { isVariadic_ = true; }
        bool isVariadic()const { return isVariadic_; }
        void setByRef() { isByRef_ = true; }
        bool isByRef()   const { return isByRef_; }

        void setInnerType(RttiType* inner) { innerType_ = inner; }
        RttiType* getInnerType() const { return innerType_; }

        void addArgument(RttiType* arg) { arguments_.push_back(arg); }
        const std::vector<RttiType*>& getArguments() const { return arguments_; }

        void setData(int64_t data) { data_ = data; }
        int64_t getData() const { return data_; }

        VariableType toVariableType() const;
        std::string toString() const;

    private:
        bool isConst_ = false;
        bool isVariadic_ = false;
        bool isByRef_ = false;
        uint8_t typeflag_;
        int64_t data_ = 0;
        RttiType* innerType_ = nullptr;
        std::vector<RttiType*> arguments_;
    };

    class SourcePawnFile;

    namespace TypeBuilder {
        RttiType* TypeFromTypeId(SourcePawnFile* file, int32_t typeid_);
        RttiType* FunctionFromOffset(SourcePawnFile* file, int32_t offset);
        RttiType* TypesetFromOffset(SourcePawnFile* file, int32_t offset);
    }

}