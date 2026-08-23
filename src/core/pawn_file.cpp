#include "pawn_file.hpp"
#include "../frontend/amxmodx.hpp"
#include "../frontend/sourcepawn.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace lysis {

    std::unique_ptr<PawnFile> PawnFile::FromFile(const std::string& path) {
        return FromBytes(ReadWholeFile(path));
    }

    std::unique_ptr<PawnFile> PawnFile::FromBytes(std::vector<uint8_t> bytes) {
        if (bytes.size() < 6) throw std::runtime_error("File too small");

        uint32_t magic = BitConverter::ToUInt32(bytes.data(), 0);
        uint16_t amx_magic = BitConverter::ToUInt16(bytes.data(), 4);

        constexpr uint32_t SP_MAGIC = 0x53504646;
        constexpr uint32_t AMXX_MAGIC_1 = 0x414D5842;
        constexpr uint32_t AMXX_MAGIC_2 = 0x414D5858;
        constexpr uint16_t AMX_MAGIC_RAW = 0xE0F1;

        if (magic == SP_MAGIC)
            throw std::runtime_error("SourcePawnFile not implemented yet");
        if (magic == AMXX_MAGIC_1 || magic == AMXX_MAGIC_2 || amx_magic == AMX_MAGIC_RAW)
            return std::make_unique<AMXModXFile>(std::move(bytes));
        throw std::runtime_error("not a .amxx or .smx file!");
    }

    Tag* PawnFile::findTag(int64_t tag_id) {
        for (auto& t : tags_) if (t->tag_id() == tag_id) return t.get();
        return nullptr;
    }

    Tag* PawnFile::findTag(const std::string& name) {
        for (auto& t : tags_) if (t->name() == name) return t.get();
        return nullptr;
    }

    Tag* PawnFile::findOrCreateTag(const std::string& name) {
        int64_t new_id = tags_.empty() ? 0 : (tags_.back()->tag_id() + 1);
        return addTag(name, new_id);
    }

    Tag* PawnFile::addTag(const std::string& name, int64_t tag_id) {
        if (Tag* t = findTag(name)) { assert(t->tag_id() == tag_id); return t; }
        if (Tag* t = findTag(tag_id)) { assert(t->name() == name); return t; }
        tags_.push_back(std::make_unique<Tag>(name, tag_id));
        return tags_.back().get();
    }

    Function* PawnFile::lookupFunction(int64_t pc) {
        for (auto& f : functions_)
            if (pc >= f->codeStart() && pc < f->codeEnd()) return f.get();
        return nullptr;
    }

    Public* PawnFile::lookupPublic(const std::string& name) {
        for (auto& p : publics_) if (p->name() == name) return p.get();
        return nullptr;
    }

    Public* PawnFile::lookupPublic(int64_t addr) {
        for (auto& p : publics_) if (p->address() == addr) return p.get();
        return nullptr;
    }

    const std::string* PawnFile::lookupFile(int64_t address) {
        if (debugFiles_.empty()) return nullptr;
        int high = (int)debugFiles_.size();
        int low = -1;
        while (high - low > 1) {
            int mid = (low + high) >> 1;
            if (debugFiles_[mid]->address() <= address) low = mid;
            else high = mid;
        }
        return (low == -1) ? nullptr : &debugFiles_[low]->name();
    }

    int32_t PawnFile::lookupLine(int64_t address) {
        if (debugLines_.empty()) return -1;
        int high = (int)debugLines_.size();
        int low = -1;
        while (high - low > 1) {
            int mid = (low + high) >> 1;
            if (debugLines_[mid]->address() <= address) low = mid;
            else high = mid;
        }
        return (low == -1) ? -1 : debugLines_[low]->line();
    }

    Variable* PawnFile::lookupDeclarations(int64_t pc, size_t& i, Scope scope) {
        for (++i; i < variables_.size(); ++i) {
            Variable* var = variables_[i].get();
            if (pc != var->codeStart()) continue;
            if (var->scope() == scope) return var;
        }
        return nullptr;
    }

    Variable* PawnFile::lookupVariable(int64_t pc, int64_t offset, Scope scope) {
        for (auto& v : variables_) {
            if (pc >= v->codeStart() && pc < v->codeEnd()
                && offset == v->address() && v->scope() == scope) return v.get();
        }
        return nullptr;
    }

    Variable* PawnFile::lookupGlobal(int64_t address) {
        for (auto& v : globals_) if (v->address() == address) return v.get();
        return nullptr;
    }

    Function* PawnFile::addFunction(int64_t addr) {
        for (auto& f : functions_) if (f->address() == addr) return f.get();

        std::string name = "sub_" + ToHex((uint64_t)addr);
        int64_t codeEnd = (int64_t)code().bytes().size() + 1;
        functions_.push_back(std::make_unique<Function>(addr, addr, codeEnd, std::move(name), (int64_t)0));
        return functions_.back().get();
    }

    bool PawnFile::addArgumentDummyVar(Function* func, int32_t num) {
        int64_t varAddr = 12 + num * 4;
        if (lookupVariable(func->address(), varAddr)) return false;

        variables_.push_back(std::make_unique<Variable>(
            varAddr, 0, nullptr,
            func->codeStart(), func->codeEnd(),
            VariableType::Normal, Scope::Local,
            "_arg" + std::to_string(num)));
        return true;
    }

    Argument PawnFile::buildArgumentInfo(Function* func, int32_t argNum) {
        int64_t argOffset = 12 + 4 * argNum;
        Variable* var = lookupVariable(func->address(), argOffset);
        if (!var) throw std::runtime_error("buildArgumentInfo: variable not found");
        int32_t tagId = var->tag() ? (int32_t)var->tag()->tag_id() : -1;
        return Argument(var->type(), var->name(), tagId, var->tag(),
            std::vector<Dimension>(var->dims()));
    }

    void PawnFile::addGlobal(int64_t addr) {
        if (lookupGlobal(addr)) return;
        for (auto& v : variables_)
            if (addr == v->address() && v->scope() == Scope::Static) return;

        globals_.push_back(std::make_unique<Variable>(
            addr, 0, nullptr, (int64_t)0, (int64_t)code().bytes().size(),
            VariableType::Normal, Scope::Global,
            "g_var" + ToHex((uint64_t)addr)));
    }

}