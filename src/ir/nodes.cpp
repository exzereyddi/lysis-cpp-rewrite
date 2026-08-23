#include "nodes.hpp"
#include "types.hpp"
#include "../core/pawn_file.hpp"

#include <algorithm>
#include <cassert>

namespace lysis {

    void DNode::initOperand(size_t i, DNode* node) {
        if (node) node->addUse(this, (int32_t)i);
        setOperand(i, node);
    }

    void DNode::replaceOperand(size_t i, DNode* node) {
        if (getOperand(i) == node) return;
        if (getOperand(i)) getOperand(i)->removeUse((int32_t)i, this);
        initOperand(i, node);
    }

    void DNode::replaceAllUsesWith(DNode* node) {
        std::vector<DUse> copies(uses_.begin(), uses_.end());
        for (const DUse& u : copies) u.node()->replaceOperand((size_t)u.index(), node);
    }

    void DNode::removeUse(int32_t index, DNode* node) {
        auto it = std::find_if(uses_.begin(), uses_.end(),
            [&](const DUse& u) { return u.index() == index && u.node() == node; });
        assert(it != uses_.end());
        uses_.erase(it);
    }

    void DNode::removeFromUseChains() {
        for (size_t i = 0; i < numOperands(); i++) replaceOperand(i, nullptr);
    }

    DNode::DNode() = default;
    DNode::~DNode() = default;

    void DNode::addType(const TypeUnit& tu) {
        if (!typeSet_) typeSet_ = std::make_unique<TypeSet>();
        typeSet_->addType(tu);
    }
    void DNode::addTypes(const TypeSet& ts) {
        if (!typeSet_) typeSet_ = std::make_unique<TypeSet>();
        typeSet_->addTypes(ts);
    }
    TypeSet* DNode::typeSet() {
        if (!typeSet_) typeSet_ = std::make_unique<TypeSet>();
        return typeSet_.get();
    }

    DNode* DNode::applyType(SourcePawnFile*, Tag*, VariableType) { return this; }

    void DConstant::accept(NodeVisitor& v) { v.visit(*this); }
    void DBoolean::accept(NodeVisitor& v) { v.visit(*this); }
    void DFloat::accept(NodeVisitor& v) { v.visit(*this); }
    void DCharacter::accept(NodeVisitor& v) { v.visit(*this); }
    void DString::accept(NodeVisitor& v) { v.visit(*this); }
    void DFunction::accept(NodeVisitor& v) { v.visit(*this); }
    void DGlobal::accept(NodeVisitor& v) { v.visit(*this); }
    void DDeclareLocal::accept(NodeVisitor& v) { v.visit(*this); }
    void DDeclareStatic::accept(NodeVisitor& v) { v.visit(*this); }
    void DLocalRef::accept(NodeVisitor& v) { v.visit(*this); }
    void DArrayRef::accept(NodeVisitor& v) { v.visit(*this); }
    void DBinary::accept(NodeVisitor& v) { v.visit(*this); }
    void DUnary::accept(NodeVisitor& v) { v.visit(*this); }
    void DBoundsCheck::accept(NodeVisitor& v) { v.visit(*this); }
    void DCall::accept(NodeVisitor& v) { v.visit(*this); }
    void DSysReq::accept(NodeVisitor& v) { v.visit(*this); }
    void DStore::accept(NodeVisitor& v) { v.visit(*this); }
    void DLoad::accept(NodeVisitor& v) { v.visit(*this); }
    void DReturn::accept(NodeVisitor& v) { v.visit(*this); }
    void DJump::accept(NodeVisitor& v) { v.visit(*this); }
    void DJumpCondition::accept(NodeVisitor& v) { v.visit(*this); }
    void DIncDec::accept(NodeVisitor& v) { v.visit(*this); }
    void DHeap::accept(NodeVisitor& v) { v.visit(*this); }
    void DMemCopy::accept(NodeVisitor& v) { v.visit(*this); }
    void DInlineArray::accept(NodeVisitor& v) { v.visit(*this); }
    void DSwitch::accept(NodeVisitor& v) { v.visit(*this); }
    void DGenArray::accept(NodeVisitor& v) { v.visit(*this); }
    void DLabel::accept(NodeVisitor& v) { v.visit(*this); }
    void DPhi::accept(NodeVisitor& v) { v.visit(*this); }

    int64_t DConstant::value() const {
        return usedAsArrayIndex() ? value_ / 4 : value_;
    }

    DNode* DConstant::applyType(SourcePawnFile*, Tag*, VariableType) { return this; }

    DNode* DDeclareLocal::applyType(SourcePawnFile* file, Tag* tag, VariableType type) {
        if (!value()) return nullptr;
        DNode* replacement = value()->applyType(file, tag, type);
        if (replacement != value()) replaceOperand(0, replacement);
        return this;
    }

    NodeList::NodeList() {
        head_ = std::make_unique<DSentinel>();
        head_->prevSet(head_.get());
        head_->nextSet(head_.get());
    }

    bool NodeList::iterator_base::more() const { return node_->type() != NodeType::Sentinel; }

    void NodeList::insertBefore(DNode* at, DNode* node) {
        node->nextSet(at);
        node->prevSet(at->prev());
        at->prev()->nextSet(node);
        at->prevSet(node);
    }

    void NodeList::insertAfter(DNode* at, DNode* node) {
        node->nextSet(at);
        node->prevSet(at->prev());
        at->prev()->nextSet(node);
        at->prevSet(node);
    }

    void NodeList::add(DNode* node) { insertBefore(head_.get(), node); }

    void NodeList::remove(DNode* node) {
        node->prev()->nextSet(node->next());
        node->next()->prevSet(node->prev());
        node->nextSet(nullptr);
        node->prevSet(nullptr);
    }

    void NodeList::remove(iterator_base& it) {
        DNode* n = it.node();
        it.next();
        remove(n);
    }

    void NodeList::replace(DNode* at, DNode* with) {
        with->prevSet(at->prev());
        with->nextSet(at->next());
        at->prev()->nextSet(with);
        at->next()->prevSet(with);
        at->prevSet(nullptr);
        at->nextSet(nullptr);
    }

    void NodeList::replace(iterator_base& it, DNode* with) {
        replace(it.node(), with);
        it.nodeSet(with);
    }

    AbstractStack::AbstractStack(int32_t nargs) { args_.resize(nargs); }

    AbstractStack::AbstractStack(const AbstractStack& other)
        : stack_(other.stack_), args_(other.args_),
        pri_(other.pri_), alt_(other.alt_) {
    }

    void AbstractStack::push(DDeclareLocal* local) {
        stack_.push_back({ local, local->value() });
        local->setOffset(depth());
    }

    void AbstractStack::push(DGenArray* array) {
        stack_.push_back({ array, nullptr });
        array->setOffset(depth());
    }

    void AbstractStack::pop() {
        if (stack_.empty()) throw UnbalancedStackException();
        stack_.pop_back();
    }

    DNode* AbstractStack::popAsTemp() {
        if (stack_.empty()) throw UnbalancedStackException();
        StackEntry e = stack_.back();
        stack_.pop_back();
        if (e.declaration && e.declaration->uses().empty()) return e.assignment;
        assert(false && "popAsTemp: not yet handled");
        return nullptr;
    }

    DNode* AbstractStack::popName() {
        if (stack_.empty()) throw UnbalancedStackException();
        DNode* v = stack_.back().declaration;
        stack_.pop_back();
        return v;
    }

    DNode* AbstractStack::popValue() {
        if (stack_.empty()) throw UnbalancedStackException();
        DNode* v = stack_.back().assignment;
        stack_.pop_back();
        return v;
    }

    DNode* AbstractStack::peekName() { return stack_.back().declaration; }

    AbstractStack::StackEntry& AbstractStack::entry(int64_t offset) {
        if (offset < 0) return stack_[(size_t)((-offset / 4) - 1)];
        int32_t argidx = (int32_t)((offset - 12) / 4);
        if (argidx < 0 || (size_t)argidx >= args_.size()) {
            emptyEntry_ = StackEntry{};
            return emptyEntry_;
        }
        return args_[argidx];
    }

    DNode* AbstractStack::getName(int64_t offset) { return entry(offset).declaration; }
    void AbstractStack::set(int64_t offset, DNode* value) { entry(offset).assignment = value; }

    void AbstractStack::init(int64_t offset, DDeclareLocal* local) {
        StackEntry& e = entry(offset);
        e.declaration = local;
        e.assignment = nullptr;
    }

    void NodeBlock::joinRegs(Register reg, DNode* value, NodeGraph* nodeGraph) {
        if (!value || stack_->reg(reg) == value) return;
        if (!stack_->reg(reg)) { stack_->set(reg, value); return; }

        DPhi* phi;
        DNode* node = stack_->reg(reg);
        if (node->type() != NodeType::Phi || node->block() != this) {
            phi = nodeGraph->newNode<DPhi>(node);
            stack_->set(reg, phi);
            add(phi);
        }
        else {
            phi = static_cast<DPhi*>(node);
        }
        phi->addInput(value);
    }

    void NodeBlock::inherit(LGraph& graph, NodeBlock* other, NodeGraph* nodeGraph) {
        if (!other) {
            stack_ = std::make_unique<AbstractStack>(graph.nargs);
            for (int32_t i = 0; i < graph.nargs; i++) {
                DDeclareLocal* local = nodeGraph->newNode<DDeclareLocal>(lir_->pc(), (DNode*)nullptr);
                local->setOffset((int64_t)(i * 4) + 12);
                add(local);
                stack_->init((int64_t)(i * 4) + 12, local);
            }
        }
        else if (!stack_) {
            assert(other->stack_);
            stack_ = std::make_unique<AbstractStack>(*other->stack_);
        }
        else {
            joinRegs(Register::Pri, other->stack_->pri(), nodeGraph);
            joinRegs(Register::Alt, other->stack_->alt(), nodeGraph);
        }
    }

    void NodeBlock::add(DNode* node) { node->setBlock(this); nodes_.add(node); }
    void NodeBlock::prepend(DNode* node) { node->setBlock(this); nodes_.insertBefore(nodes_.last(), node); }
    void NodeBlock::replace(NodeList::iterator_base& where, DNode* with) { with->setBlock(this); nodes_.replace(where, with); }
    void NodeBlock::replace(DNode* where, DNode* with) { with->setBlock(this); nodes_.replace(where, with); }

    NodeGraph::NodeGraph(PawnFile* file, std::vector<std::unique_ptr<NodeBlock>> blocks)
        : file_(file), blocks_(std::move(blocks)) {
        if (!blocks_.empty()) function_ = file_->lookupFunction(blocks_[0]->lir()->pc());
    }

    NodeGraph::~NodeGraph() = default;

}