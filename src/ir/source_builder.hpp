#pragma once

#include "../core/pawn_file.hpp"
#include "../frontend/sourcepawn.hpp"
#include "nodes.hpp"
#include "structure.hpp"
#include "types.hpp"

#include <ostream>
#include <string>

namespace lysis {

    class SourceBuilder {
    public:
        SourceBuilder(PawnFile* file, std::ostream& out) : file_(file), out_(&out) {}

        void writeGlobals();
        void write(ControlBlock* root);
        static std::string spop(SPOpcode op);

    private:
        PawnFile* file_;
        std::ostream* out_;
        std::string   indent_;

        void increaseIndent() { indent_ += "\t"; }
        void decreaseIndent() { if (!indent_.empty()) indent_.pop_back(); }
        void outputLine(const std::string& text) { (*out_) << indent_ << text << "\n"; }
        std::string indentLine(const std::string& text) { return indent_ + text; }
        std::string prepareOutputLine(const std::string& text) { return indent_ + text + "\n"; }

        std::string buildTag(const PawnType& t);
        std::string buildType(Variable* var);
        std::string buildType(const Argument& arg);
        std::string buildType(Tag* tag);
        std::string buildType(RttiType* type);
        std::string buildType(Function* func);

        std::string buildConstant(DConstant* node);
        std::string buildString(const std::string& s);
        std::string buildLocalRef(DLocalRef* lref);
        std::string buildArrayRef(DArrayRef* aref);
        std::string buildUnary(DUnary* unary);
        std::string buildBinary(DBinary* binary);
        std::string buildLoadStoreRef(DNode* node);
        std::string buildLoad(DLoad* load);
        std::string buildSysReq(DSysReq* sysreq);
        std::string buildCall(DCall* call);
        std::string buildStateChange(DStore* store);
        std::string buildStore(DStore* store);
        std::string buildInlineArray(DInlineArray* ia, const TypeUnit& tu, DNode* use);
        std::string buildInlineArray(DInlineArray* ia);
        std::string buildBoolean(DBoolean* node);
        std::string buildFloat(DFloat* node);
        std::string buildCharacter(DCharacter* node);
        std::string buildFunction(DFunction* node);
        std::string buildExpression(DNode* node);

        std::string buildArgDeclaration(const Argument& arg);
        std::string buildVarDeclaration(Variable* var);
        std::string buildStateSignature(Function* func);
        std::string buildStateVarComment(Variable* var);

        void writeSignature(NodeBlock* entry);
        void writeLocal(DDeclareLocal* local);
        void writeGenArray(DGenArray* array);
        void writeStatic(DDeclareStatic* decl);
        void writeSysReq(DSysReq* sysreq);
        void writeCall(DCall* call);
        void writeStore(DStore* store);
        void writeReturn(ReturnBlock* block);
        void writeIncDec(DIncDec* incdec);
        void writeTempName(DTempName* name);
        void writeStatement(DNode* node);
        void writeStatements(NodeBlock* block);
        void writeIf(IfBlock* block);
        void writeWhileLoop(WhileLoop* loop);
        void writeDoWhileLoop(WhileLoop* loop);
        void writeSwitch(SwitchBlock* sw);
        void writeLabel(DLabel* label);
        void writeGoto(GotoBlock* g);
        void writeStatementBlock(StatementBlock* block);
        void writeBlock(ControlBlock* block);
        void writeGlobal(Variable* var);

        std::string replaceChatColorCharacters(std::string s);
        std::string lgop(LogicOperator op) { return op == LogicOperator::And ? "&&" : "||"; }
        std::string buildLogicExpr(const LogicChain::Node& node);
        std::string buildLogicChain(LogicChain* chain);

        bool  isArrayValid(int64_t address, const std::vector<int32_t>& dims, int32_t level);
        bool  isArrayEmpty(int64_t address, int32_t bytes);
        bool  isArrayEmpty(int64_t address, const std::vector<int32_t>& dims, int32_t level);
        bool  isArrayEmpty(Variable* var);
        std::string dumpStringArray(int64_t address, int32_t size);
        std::string dumpStringArray(Variable* var, int64_t address, int32_t level);
        std::string dumpEntireArray(int64_t address, int32_t size);
        std::string dumpArray(int64_t address, int32_t size);
        std::string dumpArray(Variable* var, int64_t address, int32_t level);
    };

}