#include "node_renamer.hpp"

#include <cassert>

namespace lysis {

    void NodeRenamer::rename() {
        for (size_t i = 0; i < graph_.numBlocks(); i++) renameBlock(graph_.blocks(i));
    }

    void NodeRenamer::renameBlock(NodeBlock* block) {
        for (auto it = block->nodes().begin(); it.more(); ) {
            DNode* node = it.node();

            switch (node->type()) {
            case NodeType::TempName:      case NodeType::Jump:
            case NodeType::JumpCondition: case NodeType::Store:
            case NodeType::Return:        case NodeType::IncDec:
            case NodeType::DeclareStatic: case NodeType::Switch:
            case NodeType::GenArray:      case NodeType::Label:
                it.next();
                continue;
            default: break;
            }

            if (node->type() == NodeType::DeclareLocal) {
                auto* decl = static_cast<DDeclareLocal*>(node);
                if (!decl->var()) {
                    if (decl->uses().size() <= 1) {
                        if (decl->uses().size() == 1) {
                            DUse& use = decl->uses().front();
                            use.node()->replaceOperand((size_t)use.index(), decl->value());
                        }
                        block->nodes().remove(it);
                        continue;
                    }
                    auto* name = graph_.newNode<DTempName>(graph_.tempName());
                    node->replaceAllUsesWith(name);
                    name->init(decl->value());
                    block->nodes().replace(it, name);
                }
                it.next();
                continue;
            }

            if (node->type() == NodeType::SysReq || node->type() == NodeType::Call) {
                if (node->uses().size() <= 1) {
                    if (node->uses().size() == 1) block->nodes().remove(it);
                    else it.next();
                    continue;
                }
            }
            else if (node->type() == NodeType::Constant) {
                block->nodes().remove(it);
                continue;
            }
            else if (node->uses().size() <= 1) {
                block->nodes().remove(it);
                continue;
            }

            if (node->uses().size() == 2) {
                DUse& firstUse = *std::next(node->uses().begin(), 0);
                DUse& secondUse = *std::next(node->uses().begin(), 1);

                if (firstUse.node()->type() == NodeType::Store
                    && (secondUse.node()->type() == NodeType::Binary
                        || secondUse.node()->type() == NodeType::JumpCondition)) {
                    secondUse.node()->replaceOperand((size_t)secondUse.index(), firstUse.node());
                    block->nodes().remove(firstUse.node());
                    block->nodes().remove(it);
                    continue;
                }

                if (firstUse.node()->type() == NodeType::Binary
                    && secondUse.node()->type() == NodeType::Binary
                    && firstUse.node()->uses().size() == 1
                    && secondUse.node()->uses().size() == 1
                    && firstUse.node()->uses().front().node() == secondUse.node()->uses().front().node()
                    && firstUse.node()->uses().front().node()->type() == NodeType::Binary) {

                    auto* connector = static_cast<DBinary*>(firstUse.node()->uses().front().node());
                    if (connector->spop() == SPOpcode::and_) {
                        assert(firstUse.index() == 1);
                        auto* leftSide = static_cast<DBinary*>(connector->rhs());
                        auto* rightSide = static_cast<DBinary*>(connector->lhs());

                        leftSide->replaceOperand(1, rightSide);

                        if (secondUse.index() == 1) {
                            SPOpcode inv = rightSide->spop();
                            switch (rightSide->spop()) {
                            case SPOpcode::jsleq:  inv = SPOpcode::jsgeq;  break;
                            case SPOpcode::jsless: inv = SPOpcode::jsgrtr; break;
                            case SPOpcode::jsgrtr: inv = SPOpcode::jsless; break;
                            case SPOpcode::jsgeq:  inv = SPOpcode::jsleq;  break;
                            case SPOpcode::sleq:   inv = SPOpcode::sgeq;   break;
                            case SPOpcode::sless:  inv = SPOpcode::sgrtr;  break;
                            case SPOpcode::sgrtr:  inv = SPOpcode::sless;  break;
                            case SPOpcode::sgeq:   inv = SPOpcode::sleq;   break;
                            default: break;
                            }
                            auto* rightInverted = graph_.newNode<DBinary>(inv, rightSide->rhs(), rightSide->lhs());
                            rightSide->replaceAllUsesWith(rightInverted);
                            rightSide->removeFromUseChains();
                        }

                        connector->replaceAllUsesWith(leftSide);
                        connector->removeFromUseChains();
                        block->nodes().remove(rightSide);
                        block->nodes().remove(connector);
                        block->nodes().remove(it);
                        continue;
                    }
                }
            }

            auto* replacement = graph_.newNode<DTempName>(graph_.tempName());
            node->replaceAllUsesWith(replacement);
            replacement->init(node);
            block->nodes().replace(it, replacement);
            it.next();
        }
    }

}