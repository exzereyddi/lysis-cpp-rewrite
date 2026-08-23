#include "sourcepawn.hpp"
#include "../core/lstructure.hpp"

#include <cassert>
#include <sstream>

namespace lysis {

    namespace OpcodeHelpers {

        SPOpcode ConditionToJump(SPOpcode spop, bool onTrue) {
            switch (spop) {
            case SPOpcode::sleq:  return onTrue ? SPOpcode::jsleq : SPOpcode::jsgrtr;
            case SPOpcode::sless: return onTrue ? SPOpcode::jsless : SPOpcode::jsgeq;
            case SPOpcode::sgrtr: return onTrue ? SPOpcode::jsgrtr : SPOpcode::jsleq;
            case SPOpcode::sgeq:  return onTrue ? SPOpcode::jsgeq : SPOpcode::jsless;
            case SPOpcode::eq:    return onTrue ? SPOpcode::jeq : SPOpcode::jneq;
            case SPOpcode::neq:   return onTrue ? SPOpcode::jneq : SPOpcode::jeq;
            case SPOpcode::not_:  return onTrue ? SPOpcode::jzer : SPOpcode::jnz;
            default: assert(false); return spop;
            }
        }

        SPOpcode Invert(SPOpcode spop) {
            switch (spop) {
            case SPOpcode::jsleq:  return SPOpcode::jsgrtr;
            case SPOpcode::jsless: return SPOpcode::jsgeq;
            case SPOpcode::jsgrtr: return SPOpcode::jsleq;
            case SPOpcode::jsgeq:  return SPOpcode::jsless;
            case SPOpcode::jeq:    return SPOpcode::jneq;
            case SPOpcode::jneq:   return SPOpcode::jeq;
            case SPOpcode::jnz:    return SPOpcode::jzer;
            case SPOpcode::jzer:   return SPOpcode::jnz;
            case SPOpcode::sleq:   return SPOpcode::sgrtr;
            case SPOpcode::sless:  return SPOpcode::sgeq;
            case SPOpcode::sgrtr:  return SPOpcode::sleq;
            case SPOpcode::sgeq:   return SPOpcode::sless;
            case SPOpcode::eq:     return SPOpcode::neq;
            case SPOpcode::neq:    return SPOpcode::eq;
            default: assert(false); return spop;
            }
        }

    }

    RttiType* RttiType::getArrayBaseType() {
        RttiType* t = this;
        while (t->isArrayType()) t = t->getInnerType();
        return t;
    }

    const RttiType* RttiType::getArrayBaseType() const {
        const RttiType* t = this;
        while (t->isArrayType()) t = t->getInnerType();
        return t;
    }

    bool RttiType::isString() const { return getArrayBaseType()->getTypeFlag() == TypeFlag::Char8; }
    bool RttiType::isFloat()  const { return getArrayBaseType()->getTypeFlag() == TypeFlag::Float32; }

    VariableType RttiType::toVariableType() const {
        if (isVariadic_) return VariableType::Variadic;
        if (isArrayType()) return isByRef_ ? VariableType::ArrayReference : VariableType::Array;
        return isByRef_ ? VariableType::Reference : VariableType::Normal;
    }

    std::string RttiType::toString() const {
        std::string attr;
        if (isConst_) attr += "const ";
        if (isByRef_) attr += "&";

        switch (typeflag_) {
        case TypeFlag::Bool:        return attr + "bool";
        case TypeFlag::Int32:       return attr + "int";
        case TypeFlag::Float32:     return attr + "float";
        case TypeFlag::Char8:       return attr + "char";
        case TypeFlag::Any:         return attr + "any";
        case TypeFlag::TopFunction: return attr + "Function";
        case TypeFlag::Void:        return attr + "void";
        case TypeFlag::FixedArray: {
            std::ostringstream ss;
            ss << attr << (innerType_ ? innerType_->toString() : "?") << "[" << data_ << "]";
            return ss.str();
        }
        case TypeFlag::Array: {
            std::ostringstream ss;
            ss << attr << (innerType_ ? innerType_->toString() : "?") << "[]";
            return ss.str();
        }
        case TypeFlag::Enum:       return attr + "<enum " + std::to_string(data_) + ">";
        case TypeFlag::Typedef:    return attr + "<typedef " + std::to_string(data_) + ">";
        case TypeFlag::Typeset:    return attr + "<typeset " + std::to_string(data_) + ">";
        case TypeFlag::Classdef:   return attr + "<classdef " + std::to_string(data_) + ">";
        case TypeFlag::EnumStruct: return attr + "<enumstruct " + std::to_string(data_) + ">";
        case TypeFlag::Function: {
            std::ostringstream ss;
            ss << "function " << (innerType_ ? innerType_->toString() : "?") << "(";
            for (size_t i = 0; i < arguments_.size(); i++) {
                if (i > 0) ss << ", ";
                ss << (arguments_[i] ? arguments_[i]->toString() : "?");
            }
            if (isVariadic_) ss << "...";
            ss << ")";
            return ss.str();
        }
        }
        return "<invalid type " + std::to_string((int)typeflag_) + ">";
    }

    namespace TypeBuilder {
        RttiType* TypeFromTypeId(SourcePawnFile*, int32_t) { return nullptr; }
        RttiType* FunctionFromOffset(SourcePawnFile*, int32_t) { return nullptr; }
        RttiType* TypesetFromOffset(SourcePawnFile*, int32_t) { return nullptr; }
    }

}