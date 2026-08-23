#pragma once

#include "../core/lstructure.hpp"
#include "../frontend/sourcepawn.hpp"
#include "instructions.hpp"

#include <cassert>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace lysis {

    class PawnFile;
    class NodeBlock;
    class NodeGraph;
    class DNode;
    class DUse;
    class NodeVisitor;
    class TypeSet;
    class TypeUnit;

    enum class NodeType {
        Sentinel, Constant, DeclareLocal, DeclareStatic, LocalRef, Jump,
        JumpCondition, SysReq, Binary, BoundsCheck, ArrayRef, Store, Load,
        Return, Global, String, Boolean, Float, Function, Character, Call,
        TempName, Phi, Unary, IncDec, Heap, MemCopy, InlineArray, Switch,
        GenArray, Label
    };

    class DUse {
    public:
        DUse(DNode* node, int32_t index) : node_(node), index_(index) {}
        DNode* node() const { return node_; }
        int32_t index() const { return index_; }
    private:
        DNode* node_;
        int32_t index_;
    };

    class DNode {
    public:
        virtual ~DNode();

        virtual NodeType type() const = 0;
        virtual size_t   numOperands() const = 0;
        virtual DNode* getOperand(size_t i) const = 0;
        virtual void     accept(NodeVisitor& visitor) = 0;

        virtual bool guard() const { return false; }
        virtual bool idempotent() const { return true; }
        virtual bool controlFlow() const { return false; }

        void initOperand(size_t i, DNode* node);
        void replaceOperand(size_t i, DNode* node);
        void replaceAllUsesWith(DNode* node);
        void removeUse(int32_t index, DNode* node);
        void removeFromUseChains();

        std::list<DUse>& uses() { return uses_; }
        const std::list<DUse>& uses() const { return uses_; }

        void setBlock(NodeBlock* b) { block_ = b; }
        NodeBlock* block() const { return block_; }

        DNode* next() const { return next_; }
        DNode* prev() const { return prev_; }
        DNode* nextSet(DNode* v) { next_ = v; return next_; }
        DNode* prevSet(DNode* v) { prev_ = v; return prev_; }

        bool usedAsArrayIndex() const { return usedAsArrayIndex_; }
        void setUsedAsArrayIndex() { usedAsArrayIndex_ = true; }
        bool usedAsReference() const { return usedAsReference_; }
        void setUsedAsReference() { usedAsReference_ = true; }

        void addType(const TypeUnit& tu);
        void addTypes(const TypeSet& ts);
        TypeSet* typeSet();

        virtual DNode* applyType(class SourcePawnFile* file, Tag* tag, VariableType type);

    protected:
        DNode();
        virtual void setOperand(size_t i, DNode* node) = 0;
        void addUse(DNode* other, int32_t i) { uses_.emplace_back(other, i); }

    private:
        NodeBlock* block_ = nullptr;
        std::list<DUse> uses_;
        DNode* next_ = nullptr;
        DNode* prev_ = nullptr;
        bool usedAsArrayIndex_ = false;
        bool usedAsReference_ = false;
        std::unique_ptr<TypeSet> typeSet_;
    };

    class DNullaryNode : public DNode {
    public:
        size_t numOperands() const override { return 0; }
        DNode* getOperand(size_t) const override { assert(false); return nullptr; }
    protected:
        void setOperand(size_t, DNode*) override { assert(false); }
    };

    class DUnaryNode : public DNode {
    public:
        explicit DUnaryNode(DNode* operand) { initOperand(0, operand); }
        size_t numOperands() const override { return 1; }
        DNode* getOperand(size_t i) const override { assert(i == 0); (void)i; return operand_; }
    protected:
        void setOperand(size_t i, DNode* n) override { assert(i == 0); (void)i; operand_ = n; }
    private:
        DNode* operand_ = nullptr;
    };

    class DBinaryNode : public DNode {
    public:
        DBinaryNode(DNode* a, DNode* b) { initOperand(0, a); initOperand(1, b); }
        size_t numOperands() const override { return 2; }
        DNode* getOperand(size_t i) const override { return operands_[i]; }
        DNode* lhs() const { return getOperand(0); }
        DNode* rhs() const { return getOperand(1); }
    protected:
        void setOperand(size_t i, DNode* n) override { operands_[i] = n; }
    private:
        DNode* operands_[2] = { nullptr, nullptr };
    };

    class DCallNode : public DNode {
    public:
        explicit DCallNode(const std::vector<DNode*>& args) {
            args_.resize(args.size(), nullptr);
            for (size_t i = 0; i < args.size(); i++) initOperand(i, args[i]);
        }
        size_t numOperands() const override { return args_.size(); }
        DNode* getOperand(size_t i) const override { return args_[i]; }
        bool idempotent() const override { return false; }
    protected:
        void setOperand(size_t i, DNode* n) override { args_[i] = n; }
    private:
        std::vector<DNode*> args_;
    };

    class DSentinel : public DNullaryNode {
    public:
        NodeType type() const override { return NodeType::Sentinel; }
        void accept(NodeVisitor&) override {}
    };

    class DConstant : public DNullaryNode {
    public:
        explicit DConstant(int64_t v) : value_(v) {}
        DConstant(int64_t v, int64_t pc) : value_(v), pc_(pc) {}
        int64_t value() const;
        int64_t rawValue() const { return value_; }
        int64_t pc() const { return pc_; }
        NodeType type() const override { return NodeType::Constant; }
        void accept(NodeVisitor& v) override;
        DNode* applyType(class SourcePawnFile* file, Tag* tag, VariableType type) override;
    private:
        int64_t value_;
        int64_t pc_ = 0;
    };

    class DBoolean : public DNullaryNode {
    public:
        explicit DBoolean(bool v) : value_(v) {}
        bool value() const { return value_; }
        NodeType type() const override { return NodeType::Boolean; }
        void accept(NodeVisitor& v) override;
    private:
        bool value_;
    };

    class DFloat : public DNullaryNode {
    public:
        explicit DFloat(float v) : value_(v) {}
        float value() const { return value_; }
        NodeType type() const override { return NodeType::Float; }
        void accept(NodeVisitor& v) override;
    private:
        float value_;
    };

    class DCharacter : public DNullaryNode {
    public:
        explicit DCharacter(char v) : value_(v) {}
        char value() const { return value_; }
        NodeType type() const override { return NodeType::Character; }
        void accept(NodeVisitor& v) override;
    private:
        char value_;
    };

    class DString : public DNullaryNode {
    public:
        explicit DString(std::string v) : value_(std::move(v)) {}
        const std::string& value() const { return value_; }
        NodeType type() const override { return NodeType::String; }
        void accept(NodeVisitor& v) override;
    private:
        std::string value_;
    };

    class DFunction : public DNullaryNode {
    public:
        DFunction(int64_t pc, Function* value) : function_(value), pc_(pc) {}
        int64_t pc() const { return pc_; }
        Function* function() const { return function_; }
        NodeType type() const override { return NodeType::Function; }
        void accept(NodeVisitor& v) override;
    private:
        Function* function_;
        int64_t pc_;
    };

    class DGlobal : public DNullaryNode {
    public:
        explicit DGlobal(Variable* var) : var_(var) { assert(var != nullptr); }
        Variable* var() const { return var_; }
        NodeType type() const override { return NodeType::Global; }
        void accept(NodeVisitor& v) override;
    private:
        Variable* var_;
    };

    class DDeclareLocal : public DUnaryNode {
    public:
        DDeclareLocal(int64_t pc, DNode* value) : DUnaryNode(value), pc_(pc) {}
        void setOffset(int64_t o) { offset_ = o; }
        void setVariable(Variable* v) { var_ = v; }
        int64_t pc() const { return pc_; }
        DNode* value() const { return getOperand(0); }
        int64_t offset() const { return offset_; }
        Variable* var() const { return var_; }
        NodeType type() const override { return NodeType::DeclareLocal; }
        void accept(NodeVisitor& v) override;
        bool idempotent() const override { return false; }
        DNode* applyType(class SourcePawnFile* file, Tag* tag, VariableType type) override;
    private:
        int64_t pc_;
        int64_t offset_ = 0;
        Variable* var_ = nullptr;
    };

    class DDeclareStatic : public DNullaryNode {
    public:
        explicit DDeclareStatic(Variable* var) : var_(var) {}
        Variable* var() const { return var_; }
        NodeType type() const override { return NodeType::DeclareStatic; }
        void accept(NodeVisitor& v) override;
        bool idempotent() const override { return false; }
    private:
        Variable* var_;
    };

    class DLocalRef : public DUnaryNode {
    public:
        explicit DLocalRef(DDeclareLocal* local) : DUnaryNode(local) {}
        DDeclareLocal* local() const { return static_cast<DDeclareLocal*>(getOperand(0)); }
        NodeType type() const override { return NodeType::LocalRef; }
        void accept(NodeVisitor& v) override;
    };

    class DArrayRef : public DBinaryNode {
    public:
        DArrayRef(DNode* bas, DNode* index, int64_t shift = 2)
            : DBinaryNode(bas, index), shift_(shift) {
        }
        NodeType type() const override { return NodeType::ArrayRef; }
        void accept(NodeVisitor& v) override;
        DNode* abase() const { return getOperand(0); }
        DNode* index() const { return getOperand(1); }
        int64_t shift() const { return shift_; }
    private:
        int64_t shift_;
    };

    class DBinary : public DBinaryNode {
    public:
        DBinary(SPOpcode op, DNode* lhs, DNode* rhs) : DBinaryNode(lhs, rhs), spop_(op) {}
        SPOpcode spop() const { return spop_; }
        NodeType type() const override { return NodeType::Binary; }
        void accept(NodeVisitor& v) override;
    private:
        SPOpcode spop_;
    };

    class DUnary : public DUnaryNode {
    public:
        DUnary(SPOpcode op, DNode* node) : DUnaryNode(node), spop_(op) {}
        SPOpcode spop() const { return spop_; }
        NodeType type() const override { return NodeType::Unary; }
        void accept(NodeVisitor& v) override;
    private:
        SPOpcode spop_;
    };

    class DBoundsCheck : public DUnaryNode {
    public:
        DBoundsCheck(DNode* index, int64_t amount) : DUnaryNode(index), amount_(amount) {}
        int64_t amount() const { return amount_; }
        NodeType type() const override { return NodeType::BoundsCheck; }
        void accept(NodeVisitor& v) override;
        bool guard() const override { return true; }
    private:
        int64_t amount_;
    };

    class DCall : public DCallNode {
    public:
        DCall(Function* function, const std::vector<DNode*>& args)
            : DCallNode(args), function_(function) {
        }
        Function* function() const { return function_; }
        NodeType type() const override { return NodeType::Call; }
        void accept(NodeVisitor& v) override;
    private:
        Function* function_;
    };

    class DSysReq : public DCallNode {
    public:
        DSysReq(Native* n, const std::vector<DNode*>& args) : DCallNode(args), native_(n) {}
        Native* nativeX() const { return native_; }
        NodeType type() const override { return NodeType::SysReq; }
        void accept(NodeVisitor& v) override;
    private:
        Native* native_;
    };

    class LogicChain;

    class DStore : public DBinaryNode {
    public:
        DStore(DNode* addr, DNode* value) : DBinaryNode(addr, value) { assert(value != nullptr); }
        void makeStoreOp(SPOpcode op) { spop_ = op; }
        void setLogicChain(LogicChain* l) { logic_ = l; }
        LogicChain* logic() const { return logic_; }
        SPOpcode spop() const { return spop_; }
        NodeType type() const override { return NodeType::Store; }
        void accept(NodeVisitor& v) override;
        bool idempotent() const override { return false; }
    private:
        SPOpcode spop_ = SPOpcode::nop;
        LogicChain* logic_ = nullptr;
    };

    class DLoad : public DUnaryNode {
    public:
        explicit DLoad(DNode* addr) : DUnaryNode(addr) {}
        DNode* from() const { return getOperand(0); }
        NodeType type() const override { return NodeType::Load; }
        void accept(NodeVisitor& v) override;
    };

    class DReturn : public DUnaryNode {
    public:
        explicit DReturn(DNode* value) : DUnaryNode(value) {}
        NodeType type() const override { return NodeType::Return; }
        void accept(NodeVisitor& v) override;
        bool idempotent() const override { return false; }
        bool controlFlow() const override { return true; }
    };

    class DJump : public DNullaryNode {
    public:
        explicit DJump(NodeBlock* target) : target_(target) {}
        NodeBlock* target() const { return target_; }
        NodeType type() const override { return NodeType::Jump; }
        void accept(NodeVisitor& v) override;
        bool idempotent() const override { return false; }
        bool controlFlow() const override { return true; }
        void setBreak() { isBreak_ = true; }
        bool isBreak() const { return isBreak_; }
    private:
        NodeBlock* target_;
        bool isBreak_ = false;
    };

    class DJumpCondition : public DUnaryNode {
    public:
        DJumpCondition(SPOpcode spop, DNode* node, NodeBlock* lht, NodeBlock* rht)
            : DUnaryNode(node), spop_(spop), trueTarget_(lht), falseTarget_(rht) {
        }
        void rewrite(SPOpcode spop, DNode* node) { spop_ = spop; replaceOperand(0, node); }
        SPOpcode spop() const { return spop_; }
        NodeBlock* trueTarget()  const { return trueTarget_; }
        NodeBlock* falseTarget() const { return falseTarget_; }
        NodeBlock* joinTarget()  const { return joinTarget_; }
        void setTrueTarget(NodeBlock* b) { trueTarget_ = b; }
        void setFalseTarget(NodeBlock* b) { falseTarget_ = b; }
        void setJoinTarget(NodeBlock* b) { joinTarget_ = b; }
        void setConditional(SPOpcode op) { spop_ = op; }
        NodeType type() const override { return NodeType::JumpCondition; }
        void accept(NodeVisitor& v) override;
        bool idempotent() const override { return false; }
        bool controlFlow() const override { return true; }
    private:
        SPOpcode spop_;
        NodeBlock* trueTarget_;
        NodeBlock* falseTarget_;
        NodeBlock* joinTarget_ = nullptr;
    };

    class DIncDec : public DUnaryNode {
    public:
        DIncDec(DNode* node, int32_t amount) : DUnaryNode(node), amount_(amount) {}
        NodeType type() const override { return NodeType::IncDec; }
        bool idempotent() const override { return false; }
        void accept(NodeVisitor& v) override;
        int64_t amount() const { return amount_; }
    private:
        int64_t amount_;
    };

    class DHeap : public DNullaryNode {
    public:
        explicit DHeap(int64_t amount) : amount_(amount) {}
        int64_t amount() const { return amount_; }
        NodeType type() const override { return NodeType::Heap; }
        void accept(NodeVisitor& v) override;
    private:
        int64_t amount_;
    };

    class DMemCopy : public DBinaryNode {
    public:
        DMemCopy(DNode* to, DNode* from, int64_t bytes) : DBinaryNode(to, from), bytes_(bytes) {}
        NodeType type() const override { return NodeType::MemCopy; }
        void accept(NodeVisitor& v) override;
        bool idempotent() const override { return false; }
        int64_t bytes() const { return bytes_; }
        DNode* from() const { return getOperand(1); }
        DNode* to()   const { return getOperand(0); }
    private:
        int64_t bytes_;
    };

    class DInlineArray : public DNullaryNode {
    public:
        DInlineArray(int64_t addr, int64_t size) : address_(addr), size_(size) {}
        NodeType type() const override { return NodeType::InlineArray; }
        void accept(NodeVisitor& v) override;
        int64_t address() const { return address_; }
        int64_t size()    const { return size_; }
    private:
        int64_t address_, size_;
    };

    class DSwitch : public DUnaryNode {
    public:
        DSwitch(DNode* node, LSwitch* lir) : DUnaryNode(node), lir_(lir) {}
        NodeType type() const override { return NodeType::Switch; }
        void accept(NodeVisitor& v) override;
        bool idempotent() const override { return false; }
        bool controlFlow() const override { return true; }
        LBlock* defaultCase()      const { return lir_->defaultCase(); }
        size_t  numCases()         const { return lir_->numCases(); }
        const SwitchCase& getCase(size_t i) const { return lir_->getCase(i); }
    private:
        LSwitch* lir_;
    };

    class DGenArray : public DNode {
    public:
        DGenArray(int64_t pc, const std::vector<DNode*>& dims, bool autozero)
            : pc_(pc), autozero_(autozero) {
            dims_.resize(dims.size(), nullptr);
            for (size_t i = 0; i < dims.size(); i++) initOperand(i, dims[i]);
        }
        int64_t pc() const { return pc_; }
        bool autozero() const { return autozero_; }
        void setVariable(Variable* v) { var_ = v; }
        Variable* var() const { return var_; }
        void setOffset(int64_t o) { offset_ = o; }
        int64_t offset() const { return offset_; }
        NodeType type() const override { return NodeType::GenArray; }
        void accept(NodeVisitor& v) override;
        size_t numOperands() const override { return dims_.size(); }
        DNode* getOperand(size_t i) const override { return dims_[i]; }
    protected:
        void setOperand(size_t i, DNode* n) override { dims_[i] = n; }
    private:
        int64_t pc_;
        Variable* var_ = nullptr;
        int64_t offset_ = 0;
        bool autozero_;
        std::vector<DNode*> dims_;
    };

    class DTempName : public DUnaryNode {
    public:
        explicit DTempName(std::string name) : DUnaryNode(nullptr), name_(std::move(name)) {}
        void init(DNode* node) { initOperand(0, node); }
        const std::string& name() const { return name_; }
        NodeType type() const override { return NodeType::TempName; }
        void accept(NodeVisitor&) override {}
        bool idempotent() const override { return false; }
    private:
        std::string name_;
    };

    class DPhi : public DNode {
    public:
        explicit DPhi(DNode* node) { addInput(node); }
        void addInput(DNode* node) {
            inputs_.push_back(nullptr);
            initOperand(inputs_.size() - 1, node);
        }
        NodeType type() const override { return NodeType::Phi; }
        size_t numOperands() const override { return inputs_.size(); }
        DNode* getOperand(size_t i) const override { return inputs_[i]; }
        void accept(NodeVisitor& v) override;
    protected:
        void setOperand(size_t i, DNode* n) override { inputs_[i] = n; }
    private:
        std::vector<DNode*> inputs_;
    };

    class DLabel : public DNullaryNode {
    public:
        explicit DLabel(int64_t addr) : address_(addr) {}
        std::string label() const { return Format("Label_%llX", (unsigned long long)address_); }
        NodeType type() const override { return NodeType::Label; }
        void accept(NodeVisitor& v) override;
        bool idempotent() const override { return false; }
    private:
        int64_t address_;
    };

    class NodeVisitor {
    public:
        virtual ~NodeVisitor() = default;
        virtual void visit(DConstant&) {}
        virtual void visit(DDeclareLocal&) {}
        virtual void visit(DDeclareStatic&) {}
        virtual void visit(DLocalRef&) {}
        virtual void visit(DJump&) {}
        virtual void visit(DJumpCondition&) {}
        virtual void visit(DSysReq&) {}
        virtual void visit(DBinary&) {}
        virtual void visit(DBoundsCheck&) {}
        virtual void visit(DArrayRef&) {}
        virtual void visit(DStore&) {}
        virtual void visit(DLoad&) {}
        virtual void visit(DReturn&) {}
        virtual void visit(DGlobal&) {}
        virtual void visit(DString&) {}
        virtual void visit(DCall&) {}
        virtual void visit(DPhi&) {}
        virtual void visit(DBoolean&) {}
        virtual void visit(DCharacter&) {}
        virtual void visit(DFloat&) {}
        virtual void visit(DFunction&) {}
        virtual void visit(DUnary&) {}
        virtual void visit(DIncDec&) {}
        virtual void visit(DHeap&) {}
        virtual void visit(DMemCopy&) {}
        virtual void visit(DInlineArray&) {}
        virtual void visit(DSwitch&) {}
        virtual void visit(DGenArray&) {}
        virtual void visit(DLabel&) {}
    };

    class NodeList {
    public:
        NodeList();
        ~NodeList() = default;
        NodeList(const NodeList&) = delete;
        NodeList& operator=(const NodeList&) = delete;

        void insertBefore(DNode* at, DNode* node);
        void insertAfter(DNode* at, DNode* node);
        void add(DNode* node);
        void remove(DNode* node);
        void replace(DNode* at, DNode* with);

        DNode* first() const { return head_->next(); }
        DNode* last()  const { return head_->prev(); }

        class iterator_base {
        public:
            virtual ~iterator_base() = default;
            DNode* node() const { return node_; }
            void nodeSet(DNode* v) { node_ = v; }
            bool more() const;
            virtual void next() = 0;
        protected:
            explicit iterator_base(DNode* n) : node_(n) {}
            DNode* node_;
        };
        class iterator : public iterator_base {
        public:
            explicit iterator(DNode* n) : iterator_base(n) {}
            void next() override { node_ = node_->next(); }
        };
        class reverse_iterator : public iterator_base {
        public:
            explicit reverse_iterator(DNode* n) : iterator_base(n) {}
            void next() override { node_ = node_->prev(); }
        };

        iterator         begin() { return iterator(head_->next()); }
        reverse_iterator rbegin() { return reverse_iterator(head_->prev()); }

        void remove(iterator_base& it);
        void replace(iterator_base& it, DNode* with);

    private:
        std::unique_ptr<DSentinel> head_;
    };

    class AbstractStack {
    public:
        struct StackEntry {
            DNode* declaration = nullptr;
            DNode* assignment = nullptr;
        };
        class UnbalancedStackException : public std::runtime_error {
        public:
            UnbalancedStackException() : std::runtime_error("unbalanced stack") {}
        };

        explicit AbstractStack(int32_t nargs);
        AbstractStack(const AbstractStack& other);

        void push(DDeclareLocal* local);
        void push(DGenArray* array);
        void   pop();
        DNode* popAsTemp();
        DNode* popName();
        DNode* popValue();
        DNode* peekName();

        DNode* getName(int64_t offset);
        int32_t nargs() const { return (int32_t)args_.size(); }
        int32_t depth() const { return -(int32_t)(stack_.size() * 4); }

        DNode* pri() const { return pri_; }
        DNode* alt() const { return alt_; }
        DNode* reg(Register r) const { return (r == Register::Pri) ? pri_ : alt_; }
        void set(Register r, DNode* n) { if (r == Register::Pri) pri_ = n; else alt_ = n; }
        void set(int64_t offset, DNode* value);
        void init(int64_t offset, DDeclareLocal* local);

    private:
        StackEntry& entry(int64_t offset);
        StackEntry  emptyEntry_;
        std::vector<StackEntry> stack_;
        std::vector<StackEntry> args_;
        DNode* pri_ = nullptr;
        DNode* alt_ = nullptr;
    };

    class NodeBlock {
    public:
        explicit NodeBlock(LBlock* lir) : lir_(lir) {}
        NodeBlock(const NodeBlock&) = delete;
        NodeBlock& operator=(const NodeBlock&) = delete;

        void inherit(LGraph& graph, NodeBlock* other, NodeGraph* nodeGraph);
        void add(DNode* node);
        void prepend(DNode* node);
        void replace(NodeList::iterator_base& where, DNode* with);
        void replace(DNode* where, DNode* with);

        LBlock* lir()   const { return lir_; }
        AbstractStack* stack() { return stack_.get(); }
        NodeList& nodes() { return nodes_; }
        const NodeList& nodes() const { return nodes_; }

    private:
        LBlock* lir_;
        std::unique_ptr<AbstractStack> stack_;
        NodeList nodes_;

        void joinRegs(Register reg, DNode* value, NodeGraph* nodeGraph);
    };

    class NodeGraph {
    public:
        NodeGraph(PawnFile* file, std::vector<std::unique_ptr<NodeBlock>> blocks);
        ~NodeGraph();

        NodeBlock* blocks(size_t i) { return blocks_[i].get(); }
        PawnFile* file() { return file_; }
        Function* function() { return function_; }
        size_t     numBlocks() const { return blocks_.size(); }

        std::string tempName() { return "var" + std::to_string(++nameCounter_); }

        template<typename T, typename... Args>
        T* newNode(Args&&... args) {
            auto n = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = n.get();
            arena_.push_back(std::move(n));
            return raw;
        }

    private:
        PawnFile* file_;
        std::vector<std::unique_ptr<NodeBlock>> blocks_;
        int32_t nameCounter_ = 0;
        Function* function_ = nullptr;
        std::vector<std::unique_ptr<DNode>> arena_;
    };

} 