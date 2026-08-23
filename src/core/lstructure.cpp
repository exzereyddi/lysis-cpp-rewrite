#include "lstructure.hpp"
#include "../ir/instructions.hpp"

#include <algorithm>
#include <cassert>

namespace lysis {

    Variable::Variable(int64_t addr, int32_t tag_id, Tag* tag,
        int64_t codeStart, int64_t codeEnd,
        VariableType type, Scope scope,
        std::string name, std::vector<Dimension> dims)
        : addr_(addr), tag_id_(tag_id), tag_(tag),
        codeStart_(codeStart), codeEnd_(codeEnd),
        type_(type), scope_(scope),
        name_(std::move(name)), dims_(std::move(dims)) {
    }

    Variable::Variable(int64_t addr, int32_t tag_id, Tag* tag,
        int64_t codeStart, int64_t codeEnd,
        VariableType type, Scope scope, std::string name)
        : Variable(addr, tag_id, tag, codeStart, codeEnd, type, scope, std::move(name), {}) {
    }

    Variable::Variable(int64_t addr, int64_t codeStart, int64_t codeEnd,
        VariableType type, Scope scope,
        std::string name, std::vector<Dimension> dims,
        RttiType* rttiType)
        : Variable(addr, -1, nullptr, codeStart, codeEnd, type, scope, std::move(name), std::move(dims)) {
        rtti_type_ = rttiType;
    }

    void Variable::updateByRef() {}

    bool Variable::isString() const { return tag_ && tag_->isString(); }
    bool Variable::isFloat()  const { return tag_ && tag_->isFloat(); }

    Argument::Argument(VariableType type, std::string name, int32_t tag_id,
        Tag* tag, std::vector<Dimension> dims)
        : type_(type), name_(std::move(name)), tag_id_(tag_id),
        tag_(tag), dims_(std::move(dims)) {
    }

    Argument::Argument(VariableType type, std::string name, RttiType* rttiType,
        std::vector<Dimension> dims)
        : type_(type), name_(std::move(name)), tag_id_(-1),
        tag_(nullptr), dims_(std::move(dims)), rtti_type_(rttiType) {
    }

    bool Argument::isString() const { return tag_ && tag_->isString(); }

    bool Signature::isStringReturn() const { return tag_ && tag_->isString(); }

    Function::Function(int64_t addr, int64_t codeStart, int64_t codeEnd, std::string name)
        : Signature(std::move(name)),
        addr_(addr), codeStart_(codeStart), codeEnd_(codeEnd) {
    }

    Function::Function(int64_t addr, int64_t codeStart, int64_t codeEnd, std::string name, Tag* tag)
        : Signature(std::move(name)),
        addr_(addr), codeStart_(codeStart), codeEnd_(codeEnd) {
        tag_ = tag;
    }

    Function::Function(int64_t addr, int64_t codeStart, int64_t codeEnd, std::string name, int64_t tag_id)
        : Signature(std::move(name)),
        addr_(addr), codeStart_(codeStart), codeEnd_(codeEnd) {
        tag_id_ = tag_id;
    }

    Function::Function(int64_t addr, int64_t codeStart, int64_t codeEnd, std::string name, RttiType* rttiType)
        : Signature(std::move(name)),
        addr_(addr), codeStart_(codeStart), codeEnd_(codeEnd) {
        rtti_type_ = rttiType;
    }

    LBlock::LBlock(int64_t pc) : pc_(pc) {}
    LBlock::~LBlock() = default;

    void LBlock::setInstructions(std::vector<std::unique_ptr<LInstruction>> ins) {
        instructions_ = std::move(ins);
    }

    void LBlock::addPredecessor(LBlock* pred) {
        assert(std::find(predecessors_.begin(), predecessors_.end(), pred) == predecessors_.end());
        predecessors_.push_back(pred);
    }

    void LBlock::removePredecessor(LBlock* pred) {
        auto it = std::find(predecessors_.begin(), predecessors_.end(), pred);
        assert(it != predecessors_.end());
        predecessors_.erase(it);
    }

    LControlInstruction* LBlock::last() const {
        assert(!instructions_.empty());
        return static_cast<LControlInstruction*>(instructions_.back().get());
    }

    size_t  LBlock::numSuccessors() const { return last()->numSuccessors(); }
    LBlock* LBlock::getSuccessor(size_t i) const { return last()->getSuccessor(i); }

    void LBlock::replaceSuccessor(size_t pos, LBlock* split) { last()->replaceSuccessor(pos, split); }

    void LBlock::replacePredecessor(LBlock* from, LBlock* split) {
        assert(std::find(predecessors_.begin(), predecessors_.end(), from) != predecessors_.end());
        for (size_t i = 0; i < predecessors_.size(); i++)
            if (predecessors_[i] == from) { predecessors_[i] = split; break; }
        assert(std::find(predecessors_.begin(), predecessors_.end(), from) == predecessors_.end());
    }

    LBlock* LBlock::getLoopPredecessor() const {
        assert(loop_ == this);
        assert(numPredecessors() == 2);
        if (getPredecessor(0)->id() < id()) {
            assert(getPredecessor(1)->id() >= id());
            return getPredecessor(0);
        }
        assert(getPredecessor(1)->id() < id());
        return getPredecessor(1);
    }

    LGraph::LGraph() = default;
    LGraph::~LGraph() = default;

}