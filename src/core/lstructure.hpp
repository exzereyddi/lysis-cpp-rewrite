#pragma once

#include "util.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lysis {

    class LBlock;
    class LInstruction;
    class LControlInstruction;
    class RttiType;

    enum class Register { Pri, Alt };
    enum class Scope { Global, Local, Static };
    enum class VariableType { Normal, Reference, Array, ArrayReference, Variadic };

    class Tag {
    public:
        static constexpr int64_t FIXED = 0x40000000;
        static constexpr int64_t FUNC = 0x20000000;
        static constexpr int64_t OBJECT = 0x10000000;
        static constexpr int64_t ENUM = 0x08000000;
        static constexpr int64_t METHODMAP = 0x04000000;
        static constexpr int64_t STRUCT = 0x02000000;
        static constexpr int64_t FLAGMASK = FIXED | FUNC | OBJECT | ENUM | METHODMAP | STRUCT;

        Tag(std::string name, int64_t tag_id)
            : tag_id_(tag_id), name_(std::move(name)) {
        }

        int64_t tag_id() const { return tag_id_; }
        int64_t id()     const { return tag_id_ & ~FLAGMASK; }
        int64_t flags()  const { return tag_id_ & FLAGMASK; }
        const std::string& name() const { return name_; }

        bool isFunction() const { return (flags() & FUNC) != 0; }
        bool isFloat()    const { return name_ == "Float"; }
        bool isBoolean()  const { return name_ == "bool"; }
        bool isString()   const { return name_ == "String"; }

        std::string toString() const {
            return Format("Tag %llx (%s)", (unsigned long long)tag_id_, name_.c_str());
        }

    private:
        int64_t tag_id_;
        std::string name_;
    };

    class Dimension {
    public:
        Dimension(int32_t tag_id, Tag* tag, int32_t size)
            : tag_id_(tag_id), tag_(tag), size_(size) {
        }
        explicit Dimension(int32_t size)
            : tag_id_(-1), tag_(nullptr), size_(size) {
        }

        Tag* tag()    const { return tag_; }
        int32_t size() const { return size_; }

    private:
        int32_t tag_id_;
        Tag* tag_;
        int32_t size_;
    };

    class Variable {
    public:
        Variable(int64_t addr, int32_t tag_id, Tag* tag,
            int64_t codeStart, int64_t codeEnd,
            VariableType type, Scope scope,
            std::string name, std::vector<Dimension> dims);

        Variable(int64_t addr, int32_t tag_id, Tag* tag,
            int64_t codeStart, int64_t codeEnd,
            VariableType type, Scope scope, std::string name);

        Variable(int64_t addr, int64_t codeStart, int64_t codeEnd,
            VariableType type, Scope scope,
            std::string name, std::vector<Dimension> dims,
            RttiType* rttiType);

        int64_t address()   const { return addr_; }
        int64_t codeStart() const { return codeStart_; }
        int64_t codeEnd()   const { return codeEnd_; }
        const std::string& name() const { return name_; }
        VariableType type()  const { return type_; }
        Scope        scope() const { return scope_; }
        Tag* tag()   const { return tag_; }
        int64_t      tag_id() const { return tag_id_; }
        const std::vector<Dimension>& dims() const { return dims_; }
        bool hasDims() const { return !dims_.empty(); }

        bool isStateVariable() const { return statevar_; }
        RttiType* rttiType() const { return rtti_type_; }

        void setTag(Tag* tag) { tag_ = tag; if (tag) tag_id_ = tag->tag_id(); }
        void setTagId(int64_t id) { tag_id_ = id; }
        void setName(std::string n) { name_ = std::move(n); }
        void markAsStateVariable() { statevar_ = true; }

        void updateByRef();
        bool isString() const;
        bool isFloat() const;

    private:
        int64_t addr_;
        int64_t tag_id_;
        Tag* tag_;
        int64_t codeStart_;
        int64_t codeEnd_;
        VariableType type_;
        Scope scope_;
        std::string name_;
        std::vector<Dimension> dims_;
        bool statevar_ = false;
        RttiType* rtti_type_ = nullptr;
    };

    class Argument {
    public:
        Argument(VariableType type, std::string name, int32_t tag_id,
            Tag* tag, std::vector<Dimension> dims);

        Argument(VariableType type, std::string name, RttiType* rttiType,
            std::vector<Dimension> dims);

        VariableType type() const { return type_; }
        const std::string& name() const { return name_; }
        Tag* tag() const { return tag_; }
        const std::vector<Dimension>& dimensions() const { return dims_; }
        bool hasDimensions() const { return !dims_.empty(); }
        bool generated() const { return generated_; }
        void markGenerated() { generated_ = true; }
        RttiType* rttiType() const { return rtti_type_; }
        bool isString() const;

    private:
        VariableType type_;
        std::string  name_;
        int32_t      tag_id_;
        Tag* tag_;
        std::vector<Dimension> dims_;
        RttiType* rtti_type_ = nullptr;
        bool         generated_ = false;
    };

    class Signature {
    public:
        explicit Signature(std::string name) : name_(std::move(name)) {}
        virtual ~Signature() = default;

        Tag* returnTag()      const { return tag_; }
        RttiType* returnType() const { return rtti_type_; }
        int64_t tag_id()      const { return tag_id_; }
        const std::string& name() const { return name_; }
        const std::vector<Argument>& args() const { return args_; }
        bool hasArgs() const { return !args_.empty(); }

        void setTag(Tag* tag) { tag_ = tag; }
        void setTagId(int64_t id) { tag_id_ = id; }
        void setName(std::string n) { name_ = std::move(n); }
        void setArguments(std::vector<Argument> from) { args_ = std::move(from); }

        bool isStringReturn() const;

    protected:
        std::string name_;
        int64_t tag_id_ = 0;
        Tag* tag_ = nullptr;
        RttiType* rtti_type_ = nullptr;
        std::vector<Argument> args_;
    };

    class Function : public Signature {
    public:
        Function(int64_t addr, int64_t codeStart, int64_t codeEnd, std::string name);
        Function(int64_t addr, int64_t codeStart, int64_t codeEnd, std::string name, Tag* tag);
        Function(int64_t addr, int64_t codeStart, int64_t codeEnd, std::string name, int64_t tag_id);
        Function(int64_t addr, int64_t codeStart, int64_t codeEnd, std::string name, RttiType* rttiType);

        int64_t address()   const { return addr_; }
        int64_t codeStart() const { return codeStart_; }
        int64_t codeEnd()   const { return codeEnd_; }
        int16_t stateId()   const { return state_id_; }
        int64_t stateAddr() const { return state_addr_; }

        void setCodeEnd(int64_t v) { codeEnd_ = v; }
        void setStateId(int16_t v) { state_id_ = v; }
        void setStateAddr(int64_t v) { state_addr_ = v; }

    private:
        int64_t addr_;
        int64_t codeStart_;
        int64_t codeEnd_;
        int16_t state_id_ = -1;
        int64_t state_addr_ = -1;
    };

    class Native : public Signature {
    public:
        Native(std::string name, int32_t index)
            : Signature(std::move(name)), index_(index) {
        }

        int32_t index() const { return index_; }

        void setDebugInfo(int32_t tag_id, Tag* tag, std::vector<Argument> args) {
            tag_id_ = tag_id;
            tag_ = tag;
            args_ = std::move(args);
        }

        void setDebugInfo(RttiType* type, std::vector<Argument> args) {
            rtti_type_ = type;
            args_ = std::move(args);
        }

    private:
        int32_t index_;
    };

    class LBlock {
    public:
        explicit LBlock(int64_t pc);
        ~LBlock();

        LBlock(const LBlock&) = delete;
        LBlock& operator=(const LBlock&) = delete;
        LBlock(LBlock&&) = delete;
        LBlock& operator=(LBlock&&) = delete;

        void setInstructions(std::vector<std::unique_ptr<LInstruction>> ins);
        void addPredecessor(LBlock* pred);
        void removePredecessor(LBlock* pred);

        void mark() { marked_ = true; }
        void unmark() { marked_ = false; }
        bool marked() const { return marked_; }

        void setId(int32_t id) { id_ = id; }
        int32_t id() const { return id_; }

        void setPC(int64_t pc) { pc_ = pc; }
        int64_t pc() const { return pc_; }

        void setLoopHeader(LBlock* backedge) { backedge_ = backedge; loop_ = this; }
        void setInLoop(LBlock* loop) { loop_ = loop; }
        LBlock* backedge() const { return backedge_; }
        LBlock* loop() const { return loop_; }

        void setImmediateDominator(LBlock* idom) { idom_ = idom; }
        LBlock* idom() const { return idom_; }

        void setDominators(std::vector<LBlock*> d) { dominators_ = std::move(d); }
        const std::vector<LBlock*>& dominators() const { return dominators_; }

        void setImmediateDominated(std::vector<LBlock*> d) { idominated_ = std::move(d); }
        const std::vector<LBlock*>& idominated() const { return idominated_; }

        const std::vector<std::unique_ptr<LInstruction>>& instructions() const { return instructions_; }

        size_t numPredecessors() const { return predecessors_.size(); }
        LBlock* getPredecessor(size_t i) const { return predecessors_[i]; }

        size_t numSuccessors() const;
        LBlock* getSuccessor(size_t i) const;

        void replaceSuccessor(size_t pos, LBlock* split);
        void replacePredecessor(LBlock* from, LBlock* split);

        LControlInstruction* last() const;
        LBlock* getLoopPredecessor() const;

    private:
        int64_t pc_;
        std::vector<std::unique_ptr<LInstruction>> instructions_;
        std::vector<LBlock*> predecessors_;
        bool marked_ = false;
        int32_t id_ = 0;
        LBlock* backedge_ = nullptr;
        LBlock* loop_ = nullptr;
        LBlock* idom_ = nullptr;
        std::vector<LBlock*> dominators_;
        std::vector<LBlock*> idominated_;
    };

    struct LGraph {
        LGraph();
        ~LGraph();

        LGraph(const LGraph&) = delete;
        LGraph& operator=(const LGraph&) = delete;
        LGraph(LGraph&&) = delete;
        LGraph& operator=(LGraph&&) = delete;

        LBlock* entry = nullptr;
        std::vector<LBlock*> blocks;
        std::vector<std::unique_ptr<LBlock>> owned_blocks;
        int32_t nargs = 0;
    };

}
