#include "types.hpp"

namespace lysis {

    PawnType::PawnType(Tag* tag) {
        if (!tag || tag->name() == "_") { type_ = CellType::None;     tag_ = nullptr; }
        else if (tag->isFloat()) { type_ = CellType::Float;    tag_ = nullptr; }
        else if (tag->isBoolean()) { type_ = CellType::Bool;     tag_ = nullptr; }
        else if (tag->isFunction()) { type_ = CellType::Function; tag_ = nullptr; }
        else { type_ = CellType::Tag;      tag_ = tag; }
    }

    PawnType::PawnType(RttiType* type) : tag_(nullptr) {
        if (!type) { type_ = CellType::None; return; }
        while (type->isArrayType()) type = type->getInnerType();

        switch (type->getTypeFlag()) {
        case TypeFlag::Bool:        type_ = CellType::Bool;      break;
        case TypeFlag::Char8:       type_ = CellType::Character; break;
        case TypeFlag::Float32:     type_ = CellType::Float;     break;
        case TypeFlag::TopFunction: type_ = CellType::Function;  break;
        default:                    type_ = CellType::None;      break;
        }
    }

    TypeUnit TypeUnit::MakeReference(const TypeUnit& inner) {
        TypeUnit t(PawnType{});
        t.kind_ = Kind::Reference;
        t.ref_ = std::make_shared<TypeUnit>(inner);
        return t;
    }

    std::unique_ptr<TypeUnit> TypeUnit::load() const {
        if (kind_ == Kind::Cell) return nullptr;
        if (kind_ == Kind::Reference) {
            if (ref_->kind() == Kind::Array) return ref_->load();
            return std::make_unique<TypeUnit>(*ref_);
        }
        if (dims_ == 1) {
            if (isString()) return std::make_unique<TypeUnit>(PawnType(CellType::Character));
            return std::make_unique<TypeUnit>(type_);
        }
        return std::make_unique<TypeUnit>(TypeUnit::MakeReference(TypeUnit(type_, dims_ - 1)));
    }

    bool TypeUnit::equalTo(const TypeUnit& other) const {
        if (kind_ != other.kind_) return false;
        if (kind_ == Kind::Array && dims_ != other.dims_) return false;
        if (kind_ == Kind::Reference) {
            if ((ref_ == nullptr) != (other.ref_ == nullptr)) return false;
            if (ref_ && !ref_->equalTo(*other.ref_)) return false;
        }
        else {
            if (!type_.equalTo(other.type_)) return false;
        }
        return true;
    }

    bool TypeUnit::isString() const {
        if (type_.type() == CellType::Tag && type_.tag() && type_.tag()->isString()) return true;
        if (type_.type() == CellType::Character && dims_ == 1) return true;
        return false;
    }

    TypeUnit TypeUnit::FromTag(Tag* tag) { return TypeUnit(PawnType(tag)); }
    TypeUnit TypeUnit::FromType(RttiType* t) { return TypeUnit(PawnType(t)); }

    TypeUnit TypeUnit::FromFunction(Function* func) {
        return func->returnType() ? FromType(func->returnType()) : FromTag(func->returnTag());
    }

    std::unique_ptr<TypeUnit> TypeUnit::FromVariable(Variable* var) {
        if (var->rttiType()) {
            RttiType* type = var->rttiType();
            switch (var->type()) {
            case VariableType::Normal:
                return std::make_unique<TypeUnit>(PawnType(type));
            case VariableType::Array:
                return std::make_unique<TypeUnit>(PawnType(type), (int32_t)var->dims().size());
            case VariableType::Reference: {
                TypeUnit tu{ PawnType(type) };
                return std::make_unique<TypeUnit>(TypeUnit::MakeReference(tu));
            }
            case VariableType::ArrayReference: {
                TypeUnit tu{ PawnType(type), (int32_t)var->dims().size() };
                return std::make_unique<TypeUnit>(TypeUnit::MakeReference(tu));
            }
            default: break;
            }
            return nullptr;
        }

        switch (var->type()) {
        case VariableType::Normal:
            return std::make_unique<TypeUnit>(PawnType(var->tag()));
        case VariableType::Array:
            return std::make_unique<TypeUnit>(PawnType(var->tag()), (int32_t)var->dims().size());
        case VariableType::Reference: {
            TypeUnit tu{ PawnType(var->tag()) };
            return std::make_unique<TypeUnit>(TypeUnit::MakeReference(tu));
        }
        case VariableType::ArrayReference: {
            TypeUnit tu{ PawnType(var->tag()), (int32_t)var->dims().size() };
            return std::make_unique<TypeUnit>(TypeUnit::MakeReference(tu));
        }
        default: break;
        }
        return nullptr;
    }

    std::unique_ptr<TypeUnit> TypeUnit::FromArgument(const Argument& arg) {
        switch (arg.type()) {
        case VariableType::Normal:
            return arg.rttiType()
                ? std::make_unique<TypeUnit>(PawnType(arg.rttiType()))
                : std::make_unique<TypeUnit>(PawnType(arg.tag()));
        case VariableType::Array:
        case VariableType::ArrayReference:
            return arg.rttiType()
                ? std::make_unique<TypeUnit>(PawnType(arg.rttiType()), (int32_t)arg.dimensions().size())
                : std::make_unique<TypeUnit>(PawnType(arg.tag()), (int32_t)arg.dimensions().size());
        default: break;
        }
        return nullptr;
    }

    void TypeSet::addType(const TypeUnit& tu) {
        for (const TypeUnit& t : types_) if (t.equalTo(tu)) return;
        types_.push_back(tu);
    }

    void TypeSet::addTypes(const TypeSet& other) {
        for (const TypeUnit& t : other.types_) addType(t);
    }

}