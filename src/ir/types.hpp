#pragma once

#include "../core/lstructure.hpp"
#include "../frontend/sourcepawn.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace lysis {

    enum class CellType { None, Bool, Float, Character, Tag, Function };

    class PawnType {
    public:
        PawnType() = default;
        explicit PawnType(Tag* tag);
        explicit PawnType(RttiType* type);
        explicit PawnType(CellType type) : type_(type), tag_(nullptr) {}
        PawnType(CellType type, Tag* tag) : type_(type), tag_(tag) {}

        bool equalTo(const PawnType& other) const {
            return type_ == other.type_ && tag_ == other.tag_;
        }
        CellType type() const { return type_; }
        Tag* tag()  const { return tag_; }

    private:
        CellType type_ = CellType::None;
        Tag* tag_ = nullptr;
    };

    class TypeUnit {
    public:
        enum class Kind { Cell, Reference, Array };

        explicit TypeUnit(PawnType type) : kind_(Kind::Cell), type_(type) {}
        TypeUnit(PawnType type, int32_t dims) : kind_(Kind::Array), type_(type), dims_(dims) {}

        TypeUnit(const TypeUnit&) = default;
        TypeUnit& operator=(const TypeUnit&) = default;

        static TypeUnit MakeReference(const TypeUnit& inner);

        Kind kind() const { return kind_; }
        int32_t dims() const { assert(kind_ == Kind::Array); return dims_; }
        PawnType type() const {
            assert(kind_ == Kind::Cell || kind_ == Kind::Array);
            return type_;
        }
        const TypeUnit* inner() const {
            assert(kind_ == Kind::Reference);
            return ref_.get();
        }

        std::unique_ptr<TypeUnit> load() const;
        bool equalTo(const TypeUnit& other) const;
        bool isString() const;

        static TypeUnit FromTag(Tag* tag);
        static TypeUnit FromType(RttiType* type);
        static TypeUnit FromFunction(Function* func);
        static std::unique_ptr<TypeUnit> FromVariable(Variable* var);
        static std::unique_ptr<TypeUnit> FromArgument(const Argument& arg);

    private:
        Kind kind_;
        PawnType type_{};
        int32_t dims_ = 0;
        std::shared_ptr<TypeUnit> ref_;
    };

    class TypeSet {
    public:
        size_t numTypes() const { return types_.size(); }
        const TypeUnit& types(size_t i) const { return types_[i]; }
        void addType(const TypeUnit& tu);
        void addTypes(const TypeSet& other);
    private:
        std::vector<TypeUnit> types_;
    };

}