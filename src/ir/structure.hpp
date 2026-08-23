#pragma once

#include "nodes.hpp"

#include <memory>
#include <vector>

namespace lysis {

    enum class ControlType { If, Return, Statement, WhileLoop, DoWhileLoop, Switch, Goto };
    enum class LogicOperator { Or, And };

    class LogicChain {
    public:
        class Node {
        public:
            explicit Node(DNode* expression) : expression_(expression) {}
            explicit Node(LogicChain* subChain) : subChain_(subChain) {}
            DNode* expression() const { assert(!isSubChain()); return expression_; }
            bool isSubChain() const { return subChain_ != nullptr; }
            LogicChain* subChain() const { return subChain_; }
        private:
            DNode* expression_ = nullptr;
            LogicChain* subChain_ = nullptr;
        };

        explicit LogicChain(LogicOperator op) : op_(op) {}
        void append(DNode* expression) { nodes_.emplace_back(expression); }
        void append(LogicChain* subChain) { nodes_.emplace_back(subChain); }
        LogicOperator op() const { return op_; }
        const std::vector<Node>& nodes() const { return nodes_; }

    private:
        LogicOperator op_;
        std::vector<Node> nodes_;
    };

    class ControlBlock {
    public:
        virtual ~ControlBlock() = default;
        virtual ControlType type() const = 0;
        explicit ControlBlock(NodeBlock* source) : source_(source) {}
        NodeBlock* source() const { return source_; }
    protected:
        NodeBlock* source_;
    };

    class StatementBlock : public ControlBlock {
    public:
        StatementBlock(NodeBlock* source, ControlBlock* next) : ControlBlock(source), next_(next) {}
        ControlType type() const override { return ControlType::Statement; }
        ControlBlock* next() const { return next_; }
    private:
        ControlBlock* next_;
    };

    class ReturnBlock : public ControlBlock {
    public:
        explicit ReturnBlock(NodeBlock* source) : ControlBlock(source) {}
        ReturnBlock(NodeBlock* source, LogicChain* chain) : ControlBlock(source), chain_(chain) {}
        ControlType type() const override { return ControlType::Return; }
        LogicChain* chain() const { return chain_; }
    private:
        LogicChain* chain_ = nullptr;
    };

    class GotoBlock : public ControlBlock {
    public:
        explicit GotoBlock(NodeBlock* source) : ControlBlock(source) {}
        GotoBlock(NodeBlock* source, NodeBlock* target) : ControlBlock(source), target_(target) {}
        ControlType type() const override { return ControlType::Goto; }
        NodeBlock* target() const { return target_; }
    private:
        NodeBlock* target_ = nullptr;
    };

    class IfBlock : public ControlBlock {
    public:
        IfBlock(NodeBlock* source, bool invert, ControlBlock* trueArm, ControlBlock* join)
            : ControlBlock(source), trueArm_(trueArm), join_(join), invert_(invert) {
        }

        IfBlock(NodeBlock* source, bool invert, ControlBlock* trueArm,
            ControlBlock* falseArm, ControlBlock* join)
            : ControlBlock(source), trueArm_(trueArm), falseArm_(falseArm),
            join_(join), invert_(invert) {
        }

        IfBlock(NodeBlock* source, bool invert, LogicChain* chain,
            ControlBlock* trueArm, ControlBlock* join)
            : ControlBlock(source), trueArm_(trueArm), join_(join),
            logic_(chain), invert_(invert) {
        }

        IfBlock(NodeBlock* source, bool invert, LogicChain* chain,
            ControlBlock* trueArm, ControlBlock* falseArm, ControlBlock* join)
            : ControlBlock(source), trueArm_(trueArm), falseArm_(falseArm),
            join_(join), logic_(chain), invert_(invert) {
        }

        ControlType type() const override { return ControlType::If; }
        ControlBlock* trueArm()  const { return trueArm_; }
        ControlBlock* falseArm() const { return falseArm_; }
        ControlBlock* join()     const { return join_; }
        bool invert() const { return invert_; }
        LogicChain* logic() const { return logic_; }

    private:
        ControlBlock* trueArm_ = nullptr;
        ControlBlock* falseArm_ = nullptr;
        ControlBlock* join_ = nullptr;
        LogicChain* logic_ = nullptr;
        bool invert_ = false;
    };

    class WhileLoop : public ControlBlock {
    public:
        WhileLoop(ControlType type, NodeBlock* source, ControlBlock* body, ControlBlock* join)
            : ControlBlock(source), body_(body), join_(join), type_(type) {
        }
        WhileLoop(ControlType type, LogicChain* logic, ControlBlock* body, ControlBlock* join)
            : ControlBlock(nullptr), body_(body), join_(join), logic_(logic), type_(type) {
        }
        ControlType type() const override { return type_; }
        ControlBlock* body() const { return body_; }
        ControlBlock* join() const { return join_; }
        LogicChain* logic() const { return logic_; }
    private:
        ControlBlock* body_;
        ControlBlock* join_;
        LogicChain* logic_ = nullptr;
        ControlType type_;
    };

    class SwitchBlock : public ControlBlock {
    public:
        class Case {
        public:
            Case(std::vector<int64_t> values, ControlBlock* target)
                : values_(std::move(values)), target_(target) {
            }
            int64_t value(size_t i) const { return values_[i]; }
            size_t numValues() const { return values_.size(); }
            ControlBlock* target() const { return target_; }
        private:
            std::vector<int64_t> values_;
            ControlBlock* target_;
        };

        SwitchBlock(NodeBlock* source, ControlBlock* defaultCase,
            std::vector<Case> cases, ControlBlock* join)
            : ControlBlock(source), defaultCase_(defaultCase),
            cases_(std::move(cases)), join_(join) {
        }

        ControlType type() const override { return ControlType::Switch; }
        size_t numCases() const { return cases_.size(); }
        ControlBlock* defaultCase() const { return defaultCase_; }
        const Case& getCase(size_t i) const { return cases_[i]; }
        ControlBlock* join() const { return join_; }

    private:
        ControlBlock* defaultCase_;
        std::vector<Case> cases_;
        ControlBlock* join_;
    };

    class ControlArena {
    public:
        template<typename T, typename... Args>
        T* make(Args&&... args) {
            auto p = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = p.get();
            blocks_.push_back(std::move(p));
            return raw;
        }

        LogicChain* makeChain(LogicOperator op) {
            auto p = std::make_unique<LogicChain>(op);
            LogicChain* raw = p.get();
            chains_.push_back(std::move(p));
            return raw;
        }

    private:
        std::vector<std::unique_ptr<ControlBlock>> blocks_;
        std::vector<std::unique_ptr<LogicChain>>   chains_;
    };

}