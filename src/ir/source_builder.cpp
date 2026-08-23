#include "source_builder.hpp"
#include "block_analysis.hpp"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace lysis {

    std::string SourceBuilder::spop(SPOpcode op) {
        switch (op) {
        case SPOpcode::add: return "+";
        case SPOpcode::sub: case SPOpcode::sub_alt: case SPOpcode::neg: return "-";
        case SPOpcode::less: case SPOpcode::sless: case SPOpcode::jsless: return "<";
        case SPOpcode::grtr: case SPOpcode::sgrtr: case SPOpcode::jsgrtr: return ">";
        case SPOpcode::leq: case SPOpcode::sleq: case SPOpcode::jsleq: return "<=";
        case SPOpcode::geq: case SPOpcode::sgeq: case SPOpcode::jsgeq: return ">=";
        case SPOpcode::eq: case SPOpcode::jeq: case SPOpcode::jzer: return "==";
        case SPOpcode::jnz: case SPOpcode::jneq: case SPOpcode::neq: return "!=";
        case SPOpcode::and_: return "&";
        case SPOpcode::not_: return "!";
        case SPOpcode::or_: return "|";
        case SPOpcode::sdiv: case SPOpcode::sdiv_alt: return "/";
        case SPOpcode::sdiv_alt_mod: return "%";
        case SPOpcode::smul: return "*";
        case SPOpcode::shr: return ">>";
        case SPOpcode::shl: return "<<";
        case SPOpcode::invert: return "~";
        case SPOpcode::xor_: return "^";
        case SPOpcode::sshr: return ">>>";
        default: throw std::runtime_error("NYI spop");
        }
    }

    std::string SourceBuilder::buildTag(const PawnType& t) {
        if (t.type() == CellType::Bool)  return "bool:";
        if (t.type() == CellType::Float) return "Float:";
        if (t.type() == CellType::Tag)   return buildType(t.tag());
        return "";
    }

    std::string SourceBuilder::buildType(Tag* tag) {
        return (!tag || tag->name() == "_") ? "" : tag->name() + ":";
    }

    std::string SourceBuilder::buildType(RttiType* type) {
        if (!type) return "";
        switch (type->getTypeFlag()) {
        case TypeFlag::Bool:        return "bool:";
        case TypeFlag::Int32:       return "";
        case TypeFlag::Float32:     return "Float:";
        case TypeFlag::Char8:       return "String:";
        case TypeFlag::Any:         return "any:";
        case TypeFlag::TopFunction: return "Function:";
        case TypeFlag::Void:        return "void:";
        case TypeFlag::FixedArray:
        case TypeFlag::Array:       return buildType(type->getArrayBaseType());
        case TypeFlag::Enum:        return "<enum " + std::to_string(type->getData()) + ">:";
        case TypeFlag::Typedef:     return "<typedef " + std::to_string(type->getData()) + ">:";
        case TypeFlag::Typeset:     return "<typeset " + std::to_string(type->getData()) + ">:";
        case TypeFlag::Classdef:    return "<classdef " + std::to_string(type->getData()) + ">:";
        case TypeFlag::EnumStruct:  return "<enumstruct" + std::to_string(type->getData()) + ">:";
        case TypeFlag::Function:    return buildType(type->getInnerType());
        }
        return "";
    }

    std::string SourceBuilder::buildType(Variable* var) {
        if (!var->tag() && !var->rttiType()) return "";
        std::string prefix = (var->type() == VariableType::Reference) ? "&" : "";
        return prefix + (var->tag() ? buildType(var->tag()) : buildType(var->rttiType()));
    }

    std::string SourceBuilder::buildType(const Argument& arg) {
        if (!arg.tag() && !arg.rttiType()) return "";
        std::string prefix = (arg.type() == VariableType::Reference) ? "&" : "";
        return prefix + (arg.tag() ? buildType(arg.tag()) : buildType(arg.rttiType()));
    }

    std::string SourceBuilder::buildType(Function* func) {
        return func->returnType() ? buildType(func->returnType()) : buildType(func->returnTag());
    }

    std::string SourceBuilder::buildString(const std::string& in) {
        std::string s;
        s.reserve(in.size());
        for (char c : in) {
            if (c == '\r') s += "\\r";
            else if (c == '\n') s += "\\n";
            else if (c == '"')  s += "\\\"";
            else s += c;
        }
        s = replaceChatColorCharacters(std::move(s));
        return "\"" + s + "\"";
    }

    std::string SourceBuilder::replaceChatColorCharacters(std::string s) {
        std::string out; out.reserve(s.size());
        for (unsigned char c : s) {
            if (c == '\n') { out += "\\n"; continue; }
            if (c <= 0x10) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\x%02X", (int)c);
                out += buf;
            }
            else out += (char)c;
        }
        return out;
    }

    std::string SourceBuilder::buildConstant(DConstant* node) {
        std::string prefix;
        if (node->typeSet()->numTypes() == 1) {
            const TypeUnit& tu = node->typeSet()->types(0);
            if (tu.kind() == TypeUnit::Kind::Cell && tu.type().type() == CellType::Tag) {
                prefix = tu.type().tag() ? tu.type().tag()->name() + ":" : "MissingTAG:";
            }
        }
        return prefix + std::to_string(node->value());
    }

    std::string SourceBuilder::buildLocalRef(DLocalRef* lref) {
        if (lref->getOperand(0)->type() == NodeType::TempName)
            return static_cast<DTempName*>(lref->getOperand(0))->name();
        if (lref->getOperand(0)->type() == NodeType::Constant) return "_unused_temp_";
        if (!lref->local() || !lref->local()->var()) return "_NULLVAR_";
        return lref->local()->var()->name();
    }

    std::string SourceBuilder::buildArrayRef(DArrayRef* aref) {
        return buildExpression(aref->getOperand(0)) + "[" + buildExpression(aref->getOperand(1)) + "]";
    }

    std::string SourceBuilder::buildUnary(DUnary* unary) {
        return spop(unary->spop()) + buildExpression(unary->getOperand(0));
    }

    std::string SourceBuilder::buildBinary(DBinary* binary) {
        return buildExpression(binary->getOperand(0)) + " " + spop(binary->spop())
            + " " + buildExpression(binary->getOperand(1));
    }

    std::string SourceBuilder::buildLoadStoreRef(DNode* node) {
        if (!node) return "unk__";
        switch (node->type()) {
        case NodeType::TempName: return static_cast<DTempName*>(node)->name();
        case NodeType::DeclareLocal: {
            auto* local = static_cast<DDeclareLocal*>(node);
            return local->var() ? local->var()->name() : std::string("_unnamed_local_");
        }
        case NodeType::ArrayRef: return buildArrayRef(static_cast<DArrayRef*>(node));
        case NodeType::LocalRef: {
            auto* lref = static_cast<DLocalRef*>(node);
            auto* local = lref->local();
            if (!local || !local->var()) return "_NULLVAR_";
            auto t = local->var()->type();
            if (t == VariableType::ArrayReference || t == VariableType::Array)
                return local->var()->name() + "[0]";
            if (t == VariableType::Reference) return local->var()->name();
            throw std::runtime_error("unknown local ref");
        }
        case NodeType::Global: {
            auto* g = static_cast<DGlobal*>(node);
            return g->var() ? g->var()->name() : std::string("__unk");
        }
        case NodeType::Load: return buildLoadStoreRef(static_cast<DLoad*>(node)->from());
        case NodeType::Binary:   return buildBinary(static_cast<DBinary*>(node)) + "/* ERR load Binary */";
        case NodeType::Constant: return buildConstant(static_cast<DConstant*>(node)) + "/* ERR load Constant */";
        case NodeType::GenArray: {
            auto* ga = static_cast<DGenArray*>(node);
            return ga->var() ? ga->var()->name() : std::string("_unnamed_genarray_");
        }
        case NodeType::Call:  return buildCall(static_cast<DCall*>(node)) + "/* ERR load Call */";
        case NodeType::Unary: return buildUnary(static_cast<DUnary*>(node)) + "/* ERR load Unary */";
        default: return "/* ERR unknown load */";
        }
    }

    std::string SourceBuilder::buildLoad(DLoad* load) {
        return buildLoadStoreRef(load->getOperand(0));
    }

    std::string SourceBuilder::buildSysReq(DSysReq* sysreq) {
        std::string args;
        for (size_t i = 0; i < sysreq->numOperands(); i++) {
            args += buildExpression(sysreq->getOperand(i));
            if (i != sysreq->numOperands() - 1) args += ", ";
        }
        return std::string(sysreq->nativeX() ? sysreq->nativeX()->name() : "<native?>") + "(" + args + ")";
    }

    std::string SourceBuilder::buildCall(DCall* call) {
        std::string args;
        for (size_t i = 0; i < call->numOperands(); i++) {
            args += buildExpression(call->getOperand(i));
            if (i != call->numOperands() - 1) args += ", ";
        }
        return std::string(call->function() ? call->function()->name() : "<func?>") + "(" + args + ")";
    }

    std::string SourceBuilder::buildStateChange(DStore* store) {
        if (store->getOperand(0)->type() != NodeType::Global) return "";
        auto* g = static_cast<DGlobal*>(store->getOperand(0));
        if (!g->var() || !g->var()->isStateVariable()) return "";
        assert(store->getOperand(1)->type() == NodeType::Constant);
        auto* state = static_cast<DConstant*>(store->getOperand(1));

        auto* automation = file_->lookupAutomation(g->var()->address());
        if (!automation) return "state " + std::to_string(state->value());

        std::string expr = "state ";
        if (automation->automation_id() > 0) expr += automation->name() + ":";
        const std::string* stateName = file_->lookupState((int16_t)state->value(), automation->automation_id());
        expr += stateName ? *stateName : std::to_string(state->value());
        return expr;
    }

    std::string SourceBuilder::buildStore(DStore* store) {
        std::string sc = buildStateChange(store);
        if (!sc.empty()) return sc;
        std::string lhs = buildLoadStoreRef(store->getOperand(0));
        std::string rhs = store->logic() ? buildLogicChain(store->logic()) : buildExpression(store->getOperand(1));
        std::string eq = store->spop() == SPOpcode::nop ? "=" : (spop(store->spop()) + "=");
        return lhs + " " + eq + " " + rhs;
    }

    std::string SourceBuilder::buildInlineArray(DInlineArray* ia, const TypeUnit& tu, DNode* use) {
        Variable* var = nullptr;
        if (use->type() == NodeType::DeclareLocal)
            var = static_cast<DDeclareLocal*>(use)->var();
        else if (use->type() == NodeType::DeclareStatic)
            var = static_cast<DDeclareStatic*>(use)->var();
        if (!var) return "";

        std::string text = "{\n";
        increaseIndent();
        text += tu.isString() ? dumpStringArray(var, ia->address(), 0) : dumpArray(var, ia->address(), 0);
        decreaseIndent();
        text += indentLine("}");
        return text;
    }

    std::string SourceBuilder::buildInlineArray(DInlineArray* ia) {
        TypeUnit tu = (ia->typeSet()->numTypes() == 0)
            ? TypeUnit(PawnType(CellType::None), 1)
            : ia->typeSet()->types(0);

        if (tu.kind() == TypeUnit::Kind::Array && tu.dims() > 1 && !ia->uses().empty()) {
            std::string text = buildInlineArray(ia, tu, ia->uses().front().node());
            if (!text.empty()) return text;
        }

        if (tu.isString())
            return buildString(file_->stringFromData(ia->address(), (int32_t)ia->size() - 1));

        std::string text = "{";
        int64_t count = ia->size() / 4;
        for (int64_t i = 0; i < count; i++) {
            if (tu.kind() == TypeUnit::Kind::Cell && tu.type().type() == CellType::Float) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%g", file_->floatFromData(ia->address() + i * 4));
                text += buf;
            }
            else {
                int32_t v = file_->int32FromData(ia->address() + i * 4);
                if (tu.kind() == TypeUnit::Kind::Cell || tu.kind() == TypeUnit::Kind::Array)
                    text += buildTag(tu.type());
                text += std::to_string(v);
            }
            if (i != count - 1) text += ",";
        }
        text += "}";
        return text;
    }

    std::string SourceBuilder::buildBoolean(DBoolean* node) { return node->value() ? "true" : "false"; }

    std::string SourceBuilder::buildFloat(DFloat* node) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", node->value());
        return buf;
    }

    std::string SourceBuilder::buildCharacter(DCharacter* node) {
        std::string s(1, node->value());
        s = replaceChatColorCharacters(std::move(s));
        return "'" + s + "'";
    }

    std::string SourceBuilder::buildFunction(DFunction* node) {
        return node->function() ? node->function()->name() : "<null_func>";
    }

    std::string SourceBuilder::buildExpression(DNode* node) {
        switch (node->type()) {
        case NodeType::Constant:  return buildConstant(static_cast<DConstant*>(node));
        case NodeType::Boolean:   return buildBoolean(static_cast<DBoolean*>(node));
        case NodeType::Float:     return buildFloat(static_cast<DFloat*>(node));
        case NodeType::Character: return buildCharacter(static_cast<DCharacter*>(node));
        case NodeType::Function:  return buildFunction(static_cast<DFunction*>(node));
        case NodeType::Load:      return buildLoad(static_cast<DLoad*>(node));
        case NodeType::String:    return buildString(static_cast<DString*>(node)->value());
        case NodeType::LocalRef:  return buildLocalRef(static_cast<DLocalRef*>(node));
        case NodeType::ArrayRef:  return buildArrayRef(static_cast<DArrayRef*>(node));
        case NodeType::Unary:     return buildUnary(static_cast<DUnary*>(node));
        case NodeType::Binary:    return buildBinary(static_cast<DBinary*>(node));
        case NodeType::SysReq:    return buildSysReq(static_cast<DSysReq*>(node));
        case NodeType::Call:      return buildCall(static_cast<DCall*>(node));
        case NodeType::DeclareLocal: {
            auto* local = static_cast<DDeclareLocal*>(node);
            return local->var() ? local->var()->name() : std::string("_unnamed_local_");
        }
        case NodeType::TempName: return static_cast<DTempName*>(node)->name();
        case NodeType::Global: {
            auto* g = static_cast<DGlobal*>(node);
            return g->var() ? g->var()->name() : std::string("__unk");
        }
        case NodeType::InlineArray: return buildInlineArray(static_cast<DInlineArray*>(node));
        case NodeType::GenArray: {
            auto* ga = static_cast<DGenArray*>(node);
            return ga->var() ? ga->var()->name() : std::string("_unnamed_genarray_");
        }
        case NodeType::Store: return "(" + buildStore(static_cast<DStore*>(node)) + ")";
        default: return "/* ERR expr " + std::to_string((int)node->type()) + " */";
        }
    }

    std::string SourceBuilder::buildArgDeclaration(const Argument& arg) {
        std::string decl = buildType(arg) + arg.name();
        for (const auto& dim : arg.dimensions()) {
            decl += "[";
            if (dim.size() >= 1)
                decl += std::to_string(arg.isString() ? dim.size() * 4 : dim.size());
            decl += "]";
        }
        return decl;
    }

    std::string SourceBuilder::buildVarDeclaration(Variable* var) {
        std::string decl = buildType(var) + var->name();
        for (size_t i = 0; i < var->dims().size(); i++) {
            const auto& dim = var->dims()[i];
            decl += "[";
            if (dim.size() >= 1) {
                bool lastAndStr = var->isString() && i == var->dims().size() - 1;
                decl += std::to_string(lastAndStr ? dim.size() * 4 : dim.size());
            }
            decl += "]";
        }
        return decl;
    }

    std::string SourceBuilder::buildStateSignature(Function* func) {
        if (func->stateAddr() == -1) return "";
        auto* automation = file_->lookupAutomation(func->stateAddr());
        if (!automation) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "<%llx:%d>",
                (unsigned long long)func->stateAddr(), (int)func->stateId());
            return buf;
        }
        std::string ret = " <";
        if (automation->automation_id() > 0) ret += automation->name() + ":";
        if (func->stateId() != -1) {
            const std::string* stateName = file_->lookupState(func->stateId(), automation->automation_id());
            ret += stateName ? *stateName : std::to_string(func->stateId());
        }
        ret += ">";
        return ret;
    }

    std::string SourceBuilder::buildStateVarComment(Variable* var) {
        auto* automation = file_->lookupAutomation(var->address());
        if (!automation) return " // Internal state variable";
        std::string comment = " // Internal state variable for ";
        comment += (automation->automation_id() > 0)
            ? "automation " + automation->name()
            : std::string("default automation");
        return comment;
    }

    std::string SourceBuilder::buildLogicExpr(const LogicChain::Node& n) {
        if (n.isSubChain()) {
            std::string text = buildLogicChain(n.subChain());
            return n.subChain()->nodes().size() == 1 ? text : "(" + text + ")";
        }
        return buildExpression(n.expression());
    }

    std::string SourceBuilder::buildLogicChain(LogicChain* chain) {
        if (chain->nodes().empty()) return "";
        std::string text = buildLogicExpr(chain->nodes()[0]);
        for (size_t i = 1; i < chain->nodes().size(); i++)
            text += " " + lgop(chain->op()) + " " + buildLogicExpr(chain->nodes()[i]);
        return text;
    }

    bool SourceBuilder::isArrayValid(int64_t address, const std::vector<int32_t>& dims, int32_t level) {
        if (level == (int32_t)dims.size() - 1) return file_->isValidDataAddress(address);
        for (int32_t i = 0; i < dims[level]; i++) {
            int64_t abase = address + i * 4;
            int64_t inner = file_->int32FromData(abase);
            if (!isArrayValid(abase + inner, dims, level + 1)) return false;
        }
        return true;
    }

    bool SourceBuilder::isArrayEmpty(int64_t address, int32_t bytes) {
        for (int64_t i = address; i < address + bytes; i++) {
            if ((int64_t)file_->DAT().size() > i && file_->DAT()[(size_t)i] != 0) return false;
        }
        return true;
    }

    bool SourceBuilder::isArrayEmpty(int64_t address, const std::vector<int32_t>& dims, int32_t level) {
        if (level == (int32_t)dims.size() - 1) return isArrayEmpty(address, dims[level] * 4);
        for (int32_t i = 0; i < dims[level]; i++) {
            int64_t abase = address + i * 4;
            int64_t inner = file_->int32FromData(abase);
            if (inner == 0) return true;
            if (!isArrayEmpty(abase + inner, dims, level + 1)) return false;
        }
        return true;
    }

    bool SourceBuilder::isArrayEmpty(Variable* var) {
        std::vector<int32_t> dims;
        dims.reserve(var->dims().size());
        for (const auto& d : var->dims()) dims.push_back(d.size());
        if (var->isString() && !dims.empty()) dims.back() /= 4;
        if (!isArrayValid(var->address(), dims, 0)) return true;
        if (!var->dims().empty() && var->dims().back().size() == 0) return false;
        return isArrayEmpty(var->address(), dims, 0);
    }

    std::string SourceBuilder::dumpStringArray(int64_t address, int32_t size) {
        std::string text;
        for (int32_t i = 0; i < size; i++) {
            int64_t abase = address + i * 4;
            int64_t inner = file_->int32FromData(abase);
            std::string s = file_->stringFromData(abase + inner);
            std::string printStr = buildString(s);
            if (i != size - 1) printStr += ",";
            text += prepareOutputLine(printStr);
        }
        return text;
    }

    std::string SourceBuilder::dumpStringArray(Variable* var, int64_t address, int32_t level) {
        if (level == (int32_t)var->dims().size() - 2)
            return dumpStringArray(address, var->dims()[level].size());

        std::string text;
        int32_t sz = var->dims()[level].size();
        for (int32_t i = 0; i < sz; i++) {
            int64_t abase = address + i * 4;
            int64_t inner = file_->int32FromData(abase);
            text += prepareOutputLine("{");
            increaseIndent();
            text += dumpStringArray(var, abase + inner, level + 1);
            decreaseIndent();
            text += prepareOutputLine(i == sz - 1 ? "}" : "},");
        }
        return text;
    }

    std::string SourceBuilder::dumpEntireArray(int64_t address, int32_t size) {
        std::string text;
        for (int32_t i = 0; i < size; i++) {
            text += std::to_string(file_->int32FromData(address + i * 4));
            if (i != size - 1) text += ", ";
        }
        return prepareOutputLine(text);
    }

    std::string SourceBuilder::dumpArray(int64_t address, int32_t size) {
        int32_t first = file_->int32FromData(address);
        for (int32_t i = 1; i < size; i++) {
            if (file_->int32FromData(address + i * 4) != first)
                return dumpEntireArray(address, size);
        }
        return prepareOutputLine(std::to_string(first) + ", ...");
    }

    std::string SourceBuilder::dumpArray(Variable* var, int64_t address, int32_t level) {
        if (level == (int32_t)var->dims().size() - 1)
            return dumpArray(address, var->dims()[level].size());

        std::string text;
        int32_t sz = var->dims()[level].size();
        for (int32_t i = 0; i < sz; i++) {
            int64_t abase = address + i * 4;
            int64_t inner = file_->int32FromData(abase);
            text += prepareOutputLine("{");
            increaseIndent();
            text += dumpArray(var, abase + inner, level + 1);
            decreaseIndent();
            text += prepareOutputLine(i == sz - 1 ? "}" : "},");
        }
        return text;
    }

    void SourceBuilder::writeGlobal(Variable* var) {
        std::string decl = (var->scope() == Scope::Global) ? "new" : "static";

        auto safeStr = [&](int32_t off) {
            return file_->isValidDataAddress(off)
                ? file_->stringFromData(off)
                : std::string("[INVALID_STRING]");
            };

        if (var->name() == "myinfo") {
            int32_t offs[5];
            for (int i = 0; i < 5; i++) offs[i] = file_->int32FromData(var->address() + i * 4);
            outputLine("public Plugin:myinfo =");
            outputLine("{");
            increaseIndent();
            outputLine("name = " + buildString(safeStr(offs[0])) + ",");
            outputLine("description = " + buildString(safeStr(offs[1])) + ",");
            outputLine("author = " + buildString(safeStr(offs[2])) + ",");
            outputLine("version = " + buildString(safeStr(offs[3])) + ",");
            outputLine("url = " + buildString(safeStr(offs[4])));
            decreaseIndent();
            outputLine("};");
            return;
        }

        if (var->name().rfind("__ext_", 0) == 0) {
            int32_t nameOff = file_->int32FromData(var->address() + 0);
            int32_t fileOff = file_->int32FromData(var->address() + 4);
            int32_t autoload = file_->int32FromData(var->address() + 8);
            int32_t required = file_->int32FromData(var->address() + 12);
            outputLine("public Extension:" + var->name() + " =");
            outputLine("{");
            increaseIndent();
            outputLine("name = " + buildString(safeStr(nameOff)) + ",");
            outputLine("file = " + buildString(safeStr(fileOff)) + ",");
            outputLine("autoload = " + std::to_string(autoload) + ",");
            outputLine("required = " + std::to_string(required) + ",");
            decreaseIndent();
            outputLine("};");
            return;
        }

        if (var->name().rfind("__pl_", 0) == 0) {
            int32_t nameOff = file_->int32FromData(var->address() + 0);
            int32_t fileOff = file_->int32FromData(var->address() + 4);
            int32_t required = file_->int32FromData(var->address() + 8);
            outputLine("public SharedPlugin:" + var->name() + " =");
            outputLine("{");
            increaseIndent();
            outputLine("name = " + buildString(safeStr(nameOff)) + ",");
            outputLine("file = " + buildString(safeStr(fileOff)) + ",");
            outputLine("required = " + std::to_string(required) + ",");
            decreaseIndent();
            outputLine("};");
            return;
        }

        if (var->name() == "__version") {
            int32_t version = file_->int32FromData(var->address() + 0);
            int32_t filev = file_->int32FromData(var->address() + 4);
            int32_t dateOff = file_->int32FromData(var->address() + 8);
            int32_t timeOff = file_->int32FromData(var->address() + 12);
            outputLine("public PlVers:__version =");
            outputLine("{");
            increaseIndent();
            outputLine("version = " + std::to_string(version) + ",");
            outputLine("filevers = " + buildString(safeStr(filev)) + ",");
            outputLine("date = " + buildString(safeStr(dateOff)) + ",");
            outputLine("time = " + buildString(safeStr(timeOff)));
            decreaseIndent();
            outputLine("};");
            return;
        }

        if (var->isString() && var->dims().size() == 1) {
            std::string text = decl + " String:" + var->name()
                + "[" + std::to_string(var->dims()[0].size() * 4) + "]";
            std::string primer = file_->stringFromData(var->address());
            if (!primer.empty()) text += " = " + buildString(primer);
            outputLine(text + ";");
            return;
        }

        if (var->dims().size() == 2) {
            int32_t rows = var->dims()[0].size();
            int32_t cols = var->dims()[1].size();

            auto tryIndirection = [&]() -> bool {
                if (rows <= 0 || rows > 4096) return false;
                std::vector<std::string> strings;
                strings.reserve(rows);
                int32_t minValidOffset = rows * 4;
                for (int32_t r = 0; r < rows; r++) {
                    int64_t offsetAddr = var->address() + (int64_t)r * 4;
                    if (!file_->isValidDataAddress(offsetAddr)) return false;
                    int32_t byteOffset = file_->int32FromData(offsetAddr);
                    if (byteOffset < minValidOffset || byteOffset > 65536) return false;
                    int64_t strAddr = var->address() + byteOffset;
                    if (!file_->isValidDataAddress(strAddr)) return false;

                    std::string s;
                    bool ok = true;
                    for (int j = 0; j < 1024; j++) {
                        int64_t ca = strAddr + j * 4;
                        if (!file_->isValidDataAddress(ca)) { ok = false; break; }
                        int32_t cell = file_->int32FromData(ca);
                        if (cell == 0) break;
                        if ((cell < 32 || cell > 126) && cell != 9) { ok = false; break; }
                        s += (char)cell;
                    }
                    if (!ok) return false;
                    strings.push_back(std::move(s));
                }
                bool anyNonEmpty = false;
                for (const auto& s : strings) if (!s.empty()) { anyNonEmpty = true; break; }
                if (!anyNonEmpty) return false;

                std::string colsStr = (cols > 0) ? std::to_string(cols * 4) : std::string();
                std::string text = decl + " String:" + var->name()
                    + "[" + std::to_string(rows) + "][" + colsStr + "]";
                outputLine(text + " =");
                outputLine("{");
                increaseIndent();
                for (size_t i = 0; i < strings.size(); i++) {
                    std::string line = buildString(strings[i]);
                    if (i != strings.size() - 1) line += ",";
                    outputLine(line);
                }
                decreaseIndent();
                outputLine("};");
                return true;
                };
            if (tryIndirection()) return;

            if (cols > 0 && rows > 0) {
                bool allStrings = true;
                std::vector<std::string> strings;
                strings.reserve(rows);
                for (int32_t r = 0; r < rows && allStrings; r++) {
                    int64_t rowAddr = var->address() + (int64_t)r * cols * 4;
                    std::string s;
                    bool hasContent = false;
                    for (int32_t c = 0; c < cols; c++) {
                        int32_t cell = file_->int32FromData(rowAddr + c * 4);
                        if (cell == 0) break;
                        if (cell < 32 || cell > 126) {
                            if (hasContent) break;
                            allStrings = false;
                            break;
                        }
                        s += (char)cell;
                        hasContent = true;
                    }
                    if (!allStrings) break;
                    strings.push_back(std::move(s));
                }
                if (allStrings && !strings.empty()) {
                    bool anyNonEmpty = false;
                    for (const auto& s : strings) if (!s.empty()) { anyNonEmpty = true; break; }
                    if (anyNonEmpty) {
                        std::string text = decl + " String:" + var->name()
                            + "[" + std::to_string(rows) + "][" + std::to_string(cols * 4) + "]";
                        outputLine(text + " =");
                        outputLine("{");
                        increaseIndent();
                        for (size_t i = 0; i < strings.size(); i++) {
                            std::string line = buildString(strings[i]);
                            if (i != strings.size() - 1) line += ",";
                            outputLine(line);
                        }
                        decreaseIndent();
                        outputLine("};");
                        return;
                    }
                }
            }
        }

        if (var->isString()) {
            std::string text = decl + " " + buildType(var) + var->name();
            for (size_t i = 0; i < var->dims().size(); i++) {
                int32_t size = var->dims()[i].size();
                if (i == var->dims().size() - 1) size *= 4;
                text += "[";
                if (size != 0) text += std::to_string(size);
                text += "]";
            }
            if (isArrayEmpty(var)) { outputLine(text + ";"); return; }
            outputLine(text + " =");
            outputLine("{");
            increaseIndent();
            (*out_) << dumpStringArray(var, var->address(), 0);
            decreaseIndent();
            outputLine("};");
            return;
        }

        if (var->dims().empty()) {
            std::string text = decl + " " + buildType(var) + var->name();
            int64_t value = file_->int32FromData(var->address());
            if (value != 0) text += " = " + std::to_string(value);
            text += ";";
            if (var->isStateVariable()) text += buildStateVarComment(var);
            outputLine(text);
            return;
        }

        if (isArrayEmpty(var)) {
            std::string text = decl + " " + buildType(var) + var->name();
            for (const auto& d : var->dims()) text += "[" + std::to_string(d.size()) + "]";
            outputLine(text + ";");
            return;
        }

        std::string text = decl + " " + buildType(var) + var->name();
        for (const auto& d : var->dims()) text += "[" + std::to_string(d.size()) + "]";
        outputLine(text + " =");
        outputLine("{");
        increaseIndent();
        (*out_) << dumpArray(var, var->address(), 0);
        decreaseIndent();
        outputLine("};");
    }

    void SourceBuilder::writeGlobals() {
        for (auto& g : file_->globals()) writeGlobal(g.get());
    }

    void SourceBuilder::writeLocal(DDeclareLocal* local) {
        if (local->offset() >= 0) return;
        std::string decl = buildVarDeclaration(local->var());
        if (!local->value()) { outputLine("decl " + decl + ";"); return; }
        if (local->value()->type() == NodeType::Constant) {
            auto* con = static_cast<DConstant*>(local->value());
            if (con->value() == 0) { outputLine("new " + decl + ";"); return; }
        }
        outputLine("new " + decl + " = " + buildExpression(local->value()) + ";");
    }

    void SourceBuilder::writeGenArray(DGenArray* array) {
        std::string decl = (array->autozero() ? "new " : "decl ")
            + std::string(array->var() ? array->var()->name() : "_anon_");
        for (int i = (int)array->numOperands() - 1; i >= 0; i--)
            decl += "[" + buildExpression(array->getOperand((size_t)i)) + "]";
        outputLine(decl + ";");
    }

    void SourceBuilder::writeStatic(DDeclareStatic* decl) { writeGlobal(decl->var()); }
    void SourceBuilder::writeSysReq(DSysReq* s) { outputLine(buildSysReq(s) + ";"); }
    void SourceBuilder::writeCall(DCall* c) { outputLine(buildCall(c) + ";"); }
    void SourceBuilder::writeStore(DStore* s) { outputLine(buildStore(s) + ";"); }

    void SourceBuilder::writeReturn(ReturnBlock* block) {
        std::string operand;
        if (block->chain()) operand = buildLogicChain(block->chain());
        else {
            auto* ret = static_cast<DReturn*>(block->source()->nodes().last());
            operand = buildExpression(ret->getOperand(0));
        }
        outputLine("return " + operand + ";");
    }

    void SourceBuilder::writeIncDec(DIncDec* i) {
        outputLine(buildLoadStoreRef(i->getOperand(0)) + (i->amount() == 1 ? "++" : "--") + ";");
    }

    void SourceBuilder::writeTempName(DTempName* n) {
        if (n->getOperand(0))
            outputLine("new " + n->name() + " = " + buildExpression(n->getOperand(0)) + ";");
        else
            outputLine("new " + n->name() + ";");
    }

    void SourceBuilder::writeLabel(DLabel* label) { outputLine(label->label() + ":"); }

    void SourceBuilder::writeStatement(DNode* node) {
        switch (node->type()) {
        case NodeType::DeclareLocal:  writeLocal(static_cast<DDeclareLocal*>(node)); break;
        case NodeType::DeclareStatic: writeStatic(static_cast<DDeclareStatic*>(node)); break;
        case NodeType::Jump:
        case NodeType::JumpCondition:
        case NodeType::Return:
        case NodeType::Switch:        break;
        case NodeType::SysReq:        writeSysReq(static_cast<DSysReq*>(node)); break;
        case NodeType::Call:          writeCall(static_cast<DCall*>(node)); break;
        case NodeType::Store:         writeStore(static_cast<DStore*>(node)); break;
        case NodeType::BoundsCheck:   break;
        case NodeType::TempName:      writeTempName(static_cast<DTempName*>(node)); break;
        case NodeType::IncDec:        writeIncDec(static_cast<DIncDec*>(node)); break;
        case NodeType::GenArray:      writeGenArray(static_cast<DGenArray*>(node)); break;
        case NodeType::Label:         writeLabel(static_cast<DLabel*>(node)); break;
        default: outputLine("/* unknown op " + std::to_string((int)node->type()) + " */"); break;
        }
    }

    void SourceBuilder::writeStatements(NodeBlock* block) {
        for (auto it = block->nodes().begin(); it.more(); it.next()) writeStatement(it.node());
    }

    void SourceBuilder::writeIf(IfBlock* block) {
        writeStatements(block->source());

        std::string cond;
        if (!block->logic()) {
            auto* jcc = static_cast<DJumpCondition*>(block->source()->nodes().last());
            if (block->invert()) {
                auto* op = jcc->getOperand(0);
                if (op->type() == NodeType::Unary && static_cast<DUnary*>(op)->spop() == SPOpcode::not_)
                    cond = buildExpression(op->getOperand(0));
                else if (op->type() == NodeType::Load)
                    cond = "!" + buildExpression(op);
                else
                    cond = "!(" + buildExpression(op) + ")";
            }
            else {
                cond = buildExpression(jcc->getOperand(0));
            }
        }
        else {
            cond = block->invert()
                ? "!(" + buildLogicChain(block->logic()) + ")"
                : buildLogicChain(block->logic());
        }

        outputLine("if (" + cond + ")");
        outputLine("{");
        if (block->trueArm()) {
            increaseIndent();
            writeBlock(block->trueArm());
            decreaseIndent();
        }
        if (block->falseArm() && BlockAnalysis::GetEmptyTarget(block->falseArm()->source()) == nullptr) {
            outputLine("}");
            outputLine("else");
            outputLine("{");
            increaseIndent();
            writeBlock(block->falseArm());
            decreaseIndent();
        }
        outputLine("}");
        if (block->join()) writeBlock(block->join());
    }

    void SourceBuilder::writeWhileLoop(WhileLoop* loop) {
        std::string cond;
        if (!loop->logic()) {
            writeStatements(loop->source());
            DNode* jcc = loop->source()->nodes().last();
            cond = (jcc->type() == NodeType::JumpCondition)
                ? buildExpression(jcc->getOperand(0)) : "true";
        }
        else {
            cond = buildLogicChain(loop->logic());
        }
        outputLine("while (" + cond + ")");
        outputLine("{");
        increaseIndent();
        if (loop->body()) writeBlock(loop->body());
        decreaseIndent();
        outputLine("}");
        if (loop->join()) writeBlock(loop->join());
    }

    void SourceBuilder::writeDoWhileLoop(WhileLoop* loop) {
        outputLine("do {");
        increaseIndent();
        if (loop->body()) writeBlock(loop->body());

        std::string cond;
        if (!loop->logic()) {
            writeStatements(loop->source());
            decreaseIndent();
            DNode* last = loop->source()->nodes().last();
            if (last->type() == NodeType::JumpCondition) {
                cond = buildExpression(static_cast<DJumpCondition*>(last)->getOperand(0));
            }
            else {
                if (last->type() == NodeType::Jump)
                    writeStatements(static_cast<DJump*>(last)->target());
                cond = "true";
            }
        }
        else {
            decreaseIndent();
            cond = buildLogicChain(loop->logic());
        }
        outputLine("} while (" + cond + ");");
        if (loop->join()) writeBlock(loop->join());
    }

    void SourceBuilder::writeSwitch(SwitchBlock* sw) {
        writeStatements(sw->source());
        auto* last = static_cast<DSwitch*>(sw->source()->nodes().last());
        outputLine("switch (" + buildExpression(last->getOperand(0)) + ")");
        outputLine("{");
        increaseIndent();
        for (size_t i = 0; i < sw->numCases(); i++) {
            const auto& c = sw->getCase(i);
            std::string values;
            for (size_t j = 0; j < c.numValues(); j++) {
                if (j > 0) values += ", ";
                values += std::to_string(c.value(j));
            }
            outputLine("case " + values + ":");
            outputLine("{");
            increaseIndent();
            writeBlock(c.target());
            decreaseIndent();
            outputLine("}");
        }
        outputLine("default:");
        outputLine("{");
        increaseIndent();
        writeBlock(sw->defaultCase());
        decreaseIndent();
        outputLine("}");
        decreaseIndent();
        outputLine("}");
        if (sw->join()) writeBlock(sw->join());
    }

    void SourceBuilder::writeGoto(GotoBlock* g) {
        writeStatements(g->source());
        auto* label = static_cast<DLabel*>(g->target()->nodes().first());
        outputLine("goto " + label->label() + ";");
    }

    void SourceBuilder::writeStatementBlock(StatementBlock* block) {
        writeStatements(block->source());
        if (block->next()) writeBlock(block->next());
    }

    void SourceBuilder::writeBlock(ControlBlock* block) {
        if (!block) return;
        switch (block->type()) {
        case ControlType::If:          writeIf(static_cast<IfBlock*>(block)); break;
        case ControlType::WhileLoop:   writeWhileLoop(static_cast<WhileLoop*>(block)); break;
        case ControlType::DoWhileLoop: writeDoWhileLoop(static_cast<WhileLoop*>(block)); break;
        case ControlType::Statement:   writeStatementBlock(static_cast<StatementBlock*>(block)); break;
        case ControlType::Return:
            writeStatements(block->source());
            writeReturn(static_cast<ReturnBlock*>(block));
            break;
        case ControlType::Switch: writeSwitch(static_cast<SwitchBlock*>(block)); break;
        case ControlType::Goto:   writeGoto(static_cast<GotoBlock*>(block)); break;
        default: break;
        }
    }

    void SourceBuilder::writeSignature(NodeBlock* entry) {
        Function* f = file_->lookupFunction(entry->lir()->pc());
        if (!f) return;

        Public* pub = file_->lookupPublic(entry->lir()->pc());
        if (pub) {
            const std::string& n = pub->name();
            bool isCallback = false;
            if (!n.empty() && n[0] == '.') {
                size_t p = 1;
                while (p < n.size() && n[p] >= '0' && n[p] <= '9') p++;
                if (p > 1 && p < n.size() && n[p] == '.') isCallback = true;
            }
            if (!isCallback) (*out_) << "public ";
        }

        (*out_) << buildType(f) << f->name() << "(";
        for (size_t i = 0; i < f->args().size(); i++) {
            (*out_) << buildArgDeclaration(f->args()[i]);
            if (i != f->args().size() - 1) (*out_) << ", ";
        }
        (*out_) << ")" << buildStateSignature(f) << "\n";
    }

    void SourceBuilder::write(ControlBlock* root) {
        writeSignature(root->source());
        outputLine("{");
        increaseIndent();
        writeBlock(root);
        decreaseIndent();
        outputLine("}");
    }

}