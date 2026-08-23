#include "amxmodx.hpp"
#include "../core/util.hpp"

#include "../../third_party/miniz.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace lysis {

    std::string AMXModXFile::ReadName(const std::vector<uint8_t>& bytes, size_t offset) {
        size_t count = offset;
        while (count < bytes.size() && bytes[count] != 0) count++;
        return std::string(reinterpret_cast<const char*>(bytes.data() + offset), count - offset);
    }

    std::string AMXModXFile::ReadNameFromReader(BinaryReader& r) {
        std::vector<uint8_t> buf;
        while (true) {
            uint8_t b = r.ReadUInt8();
            if (b == 0) break;
            buf.push_back(b);
        }
        return std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
    }

    std::string AMXModXFile::ReadStringCells(const std::vector<uint8_t>& bytes, size_t offset) {
        std::string result;
        for (size_t c = offset; c + 3 < bytes.size(); c += 4) {
            if (bytes[c] == 0) break;
            result.push_back((char)(uint8_t)BitConverter::ToInt32(bytes.data(), c));
        }
        return result;
    }

    std::string AMXModXFile::ReadStringCellsEx(const std::vector<uint8_t>& bytes, size_t offset, size_t maxread) {
        std::string result;
        size_t last = offset + maxread;
        for (size_t c = offset; c + 3 < bytes.size() && c <= last; c += 4) {
            if (bytes[c] == 0) break;
            result.push_back((char)(uint8_t)BitConverter::ToInt32(bytes.data(), c));
        }
        return result;
    }

    VariableType AMXModXFile::FromIdent(uint8_t ident) {
        switch (ident) {
        case IDENT_VARIABLE:  return VariableType::Normal;
        case IDENT_REFERENCE: return VariableType::Reference;
        case IDENT_ARRAY:     return VariableType::Array;
        case IDENT_REFARRAY:  return VariableType::ArrayReference;
        case IDENT_VARARGS:   return VariableType::Variadic;
        default:              return VariableType::Normal;
        }
    }

    static std::vector<uint8_t> InflateRaw(const uint8_t* src, size_t srcLen, size_t expectedOut) {
        std::vector<uint8_t> out(expectedOut);
        mz_stream stream{};
        stream.next_in = src;
        stream.avail_in = (unsigned int)srcLen;
        stream.next_out = out.data();
        stream.avail_out = (unsigned int)expectedOut;

        if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
            throw std::runtime_error("inflateInit failed");

        int status = mz_inflate(&stream, MZ_FINISH);
        mz_inflateEnd(&stream);
        if (status != MZ_STREAM_END && status != MZ_OK)
            throw std::runtime_error("uncompress error");

        out.resize(stream.total_out);
        return out;
    }

    AMXModXFile::AMXModXFile(std::vector<uint8_t> binary) {
        BinaryReader reader(binary);
        uint32_t magic = reader.ReadUInt32();

        if (magic == MAGIC) {
            TableEntry chosen{};
            bool found = false;
            uint8_t numPlugins = reader.ReadUInt8();
            uint8_t tableIndex = 0;
            for (; tableIndex < numPlugins; tableIndex++) {
                TableEntry t{};
                t.cellSize = reader.ReadUInt8();
                t.origSize = reader.ReadInt32();
                t.offset = reader.ReadInt32();
                if (t.cellSize == 4) { chosen = t; found = true; break; }
            }
            if (!found) throw std::runtime_error("could not find applicable cell size");

            int32_t sectionLength;
            if ((tableIndex + 1) < numPlugins) {
                TableEntry nextT{};
                nextT.cellSize = reader.ReadUInt8();
                nextT.origSize = reader.ReadInt32();
                nextT.offset = reader.ReadInt32();
                sectionLength = nextT.offset - chosen.offset;
            }
            else {
                sectionLength = (int32_t)binary.size() - chosen.offset;
            }

            binary = InflateRaw(binary.data() + chosen.offset + 2,
                (size_t)sectionLength - 2, (size_t)chosen.origSize);
        }
        else if (magic == MAGIC2) {
            uint16_t version = reader.ReadUInt16();
            if (version > MAGIC2_VERSION) throw std::runtime_error("unexpected version");

            PluginHeader chosen{};
            bool found = false;
            uint8_t numPlugins = reader.ReadUInt8();
            for (uint8_t i = 0; i < numPlugins; i++) {
                PluginHeader p{};
                p.cellsize = reader.ReadUInt8();
                p.disksize = reader.ReadInt32();
                p.imagesize = reader.ReadInt32();
                p.memsize = reader.ReadInt32();
                p.offset = reader.ReadInt32();
                if (p.cellsize == 4) { chosen = p; found = true; break; }
            }
            if (!found) throw std::runtime_error("could not find applicable cell size");

            int32_t bufferSize = std::max(chosen.imagesize, chosen.memsize) + 1;
            if (bufferSize <= 0 || bufferSize > 64 * 1024 * 1024)
                throw std::runtime_error("insane buffer size in AMXX header");
            int32_t disksize = chosen.disksize;
            if (disksize <= 2 || (size_t)(chosen.offset + disksize) > binary.size())
                disksize = (int32_t)(binary.size() - chosen.offset);

            auto inflated = InflateRaw(binary.data() + chosen.offset + 2,
                (size_t)disksize - 2, (size_t)bufferSize);
            if ((int32_t)inflated.size() != chosen.imagesize) {
                std::cerr << "uncompressed size mismatch, bad file? expected "
                    << chosen.imagesize << ", got " << inflated.size() << "\n";
            }
            if ((int32_t)inflated.size() < bufferSize)
                inflated.resize((size_t)bufferSize, 0);
            binary = std::move(inflated);
        }
        else {
            uint16_t amx_magic = reader.ReadUInt16();
            if (amx_magic != AMX_MAGIC) throw std::runtime_error("unrecognized file");
        }

        BinaryReader r2(binary);
        AMX_HEADER amx{};
        amx.size = r2.ReadInt32();
        amx.magic = r2.ReadUInt16();
        amx.file_version = r2.ReadUInt8();
        amx.amx_version = r2.ReadUInt8();
        amx.flags = r2.ReadInt16();
        amx.defsize = r2.ReadInt16();
        amx.cod = r2.ReadInt32();
        amx.dat = r2.ReadInt32();
        amx.hea = r2.ReadInt32();
        amx.stp = r2.ReadInt32();
        amx.cip = r2.ReadInt32();
        amx.publics = r2.ReadInt32();
        amx.natives = r2.ReadInt32();
        amx.libraries = r2.ReadInt32();
        amx.pubvars = r2.ReadInt32();
        amx.tags = r2.ReadInt32();
        amx.nametable = r2.ReadInt32();

        if (amx.magic != AMX_MAGIC) throw std::runtime_error("unrecognized amx header");
        if (amx.file_version < MIN_FILE_VERSION || amx.file_version > CUR_FILE_VERSION)
            throw std::runtime_error("unrecognized amx version");
        if (amx.defsize != DEFSIZE) throw std::runtime_error("unrecognized header defsize");

        if (amx.publics > 0) {
            int32_t count = (amx.natives - amx.publics) / DEFSIZE;
            BinaryReader pr(binary.data() + amx.publics, (size_t)count * DEFSIZE);
            for (int32_t i = 0; i < count; i++) {
                int64_t address = (int64_t)pr.ReadUInt32();
                int32_t nameoffset = pr.ReadInt32();
                std::string name = ReadName(binary, (size_t)nameoffset);
                publics_.push_back(std::make_unique<Public>(name, address));
                functions_.push_back(std::make_unique<Function>(address, address, (int64_t)0, name));
            }
        }

        assert((amx.flags & AMX_FLAG_COMPACT) == AMX_FLAG_COMPACT || amx.size == amx.hea);
        if ((amx.flags & AMX_FLAG_COMPACT) == AMX_FLAG_COMPACT)
            binary = decompressCompactCode(amx, binary);

        {
            auto codeBytes = Slice(binary, (size_t)amx.cod, (size_t)(amx.dat - amx.cod));
            code_ = Code(std::move(codeBytes), 0, 0, 0, 0);
            int64_t codeEnd = (int64_t)code().bytes().size() + 1;
            for (auto& f : functions_) f->setCodeEnd(codeEnd);
        }
        {
            auto dataBytes = Slice(binary, (size_t)amx.dat, (size_t)(amx.hea - amx.dat));
            data_ = Data(std::move(dataBytes), amx.hea - amx.dat);
        }

        if (amx.natives > 0) {
            int32_t count = (amx.libraries - amx.natives) / DEFSIZE;
            BinaryReader nr(binary.data() + amx.natives, (size_t)count * DEFSIZE);
            for (int32_t i = 0; i < count; i++) {
                nr.ReadUInt32();
                int32_t nameoffset = nr.ReadInt32();
                std::string name = ReadName(binary, (size_t)nameoffset);
                natives_.push_back(std::make_unique<Native>(name, i));
            }
        }

        if (amx.pubvars > 0) {
            int32_t count = (amx.tags - amx.pubvars) / DEFSIZE;
            BinaryReader pr(binary.data() + amx.pubvars, (size_t)count * DEFSIZE);
            for (int32_t i = 0; i < count; i++) {
                int64_t address = (int64_t)pr.ReadUInt32();
                int32_t nameoffset = pr.ReadInt32();
                std::string name = ReadName(binary, (size_t)nameoffset);
                pubvars_.push_back(std::make_unique<PubVar>(name, address));
                globals_.push_back(std::make_unique<Variable>(
                    address, 0, nullptr, (int64_t)0, (int64_t)code().bytes().size(),
                    VariableType::Normal, Scope::Global, name));
            }
            allvars_.reserve(globals_.size());
            for (auto& g : globals_) allvars_.push_back(g.get());
        }

        if (amx.file_version >= MIN_DEBUG_FILE_VERSION
            && (amx.flags & AMX_FLAG_DEBUG) == AMX_FLAG_DEBUG) {

            BinaryReader dr(binary.data() + amx.hea, AMX_DEBUG_HDR::SIZE);
            AMX_DEBUG_HDR dbg{};
            dbg.size = dr.ReadInt32();
            dbg.magic = dr.ReadUInt16();
            dbg.file_version = dr.ReadUInt8();
            dbg.amx_version = dr.ReadUInt8();
            dbg.flags = dr.ReadInt16();
            dbg.files = dr.ReadInt16();
            dbg.lines = dr.ReadInt16();
            dbg.symbols = dr.ReadInt16();
            dbg.tags = dr.ReadInt16();
            dbg.automatons = dr.ReadInt16();
            dbg.states = dr.ReadInt16();

            if (dbg.magic != AMX_DBG_MAGIC) throw std::runtime_error("unrecognized debug magic");

            BinaryReader r(binary.data() + amx.hea + AMX_DEBUG_HDR::SIZE,
                (size_t)(dbg.size - AMX_DEBUG_HDR::SIZE));

            debugHeader_.numFiles = dbg.files;
            debugHeader_.numLines = dbg.lines;
            debugHeader_.numSyms = dbg.symbols;

            for (int16_t i = 0; i < dbg.files; i++) {
                int64_t offset = (int64_t)r.ReadUInt32();
                std::string name = ReadNameFromReader(r);
                debugFiles_.push_back(std::make_unique<DebugFile>(name, offset));
            }
            for (int16_t i = 0; i < dbg.lines; i++) {
                int64_t offset = (int64_t)r.ReadUInt32();
                int32_t lineno = r.ReadInt32();
                debugLines_.push_back(std::make_unique<DebugLine>(lineno, offset));
            }

            auto newFunctions = std::move(functions_);
            functions_.clear();
            auto newGlobals = std::move(globals_);
            globals_.clear();
            std::vector<std::unique_ptr<Variable>> newLocals;

            for (int16_t i = 0; i < dbg.symbols; i++) {
                int32_t addr = r.ReadInt32();
                int32_t tagid = r.ReadUInt16();
                int64_t codestart = (int64_t)r.ReadUInt32();
                int64_t codeend = (int64_t)r.ReadUInt32();
                uint8_t ident = r.ReadUInt8();
                uint8_t vclassByte = r.ReadUInt8();
                Scope vclass = (vclassByte < 3) ? (Scope)vclassByte : Scope::Local;
                int16_t dimcount = r.ReadInt16();
                std::string name = ReadNameFromReader(r);

                if (ident == IDENT_FUNCTION) {
                    auto func = std::make_unique<Function>(addr, codestart, codeend, name, (int64_t)tagid);
                    Function* dup = nullptr;
                    for (auto& f : newFunctions)
                        if (name == f->name()) { dup = f.get(); break; }
                    if (dup) {
                        if (dup->address() != func->address() || dup->codeStart() != func->codeStart()) {
                            std::cerr << "// Duplicate info for \"" << name << "\", keeping existing at "
                                << ToHex((uint64_t)dup->address()) << "\n";
                        }
                        newFunctions.erase(std::remove_if(newFunctions.begin(), newFunctions.end(),
                            [dup](const std::unique_ptr<Function>& p) { return p.get() == dup; }),
                            newFunctions.end());
                    }
                    newFunctions.push_back(std::move(func));
                }
                else {
                    VariableType type = FromIdent(ident);
                    std::vector<Dimension> dims;
                    if (dimcount > 0) {
                        dims.reserve(dimcount);
                        for (int j = 0; j < dimcount; j++) {
                            int16_t dim_tag_id = r.ReadInt16();
                            int32_t size = r.ReadInt32();
                            dims.emplace_back(dim_tag_id, nullptr, size);
                        }
                    }
                    auto var = std::make_unique<Variable>(
                        (int64_t)addr, tagid, nullptr, codestart, codeend, type, vclass, name, std::move(dims));

                    if (vclass == Scope::Global) {
                        Variable* dup = nullptr;
                        for (auto& g : newGlobals)
                            if (name == g->name()) { dup = g.get(); break; }
                        if (dup) {
                            if (dup->address() != (int64_t)addr) {
                                std::cerr << "// Duplicate global \"" << name << "\"\n";
                                continue;
                            }
                            newGlobals.erase(std::remove_if(newGlobals.begin(), newGlobals.end(),
                                [dup](const std::unique_ptr<Variable>& p) { return p.get() == dup; }),
                                newGlobals.end());
                        }
                        newGlobals.push_back(std::move(var));
                    }
                    else {
                        newLocals.push_back(std::move(var));
                    }
                }
            }

            auto sortByAddr = [](const auto& a, const auto& b) { return a->address() < b->address(); };
            std::sort(newGlobals.begin(), newGlobals.end(), sortByAddr);
            std::sort(newFunctions.begin(), newFunctions.end(), sortByAddr);

            allvars_.clear();
            for (auto& v : newLocals)  allvars_.push_back(v.get());
            for (auto& v : newGlobals) allvars_.push_back(v.get());
            std::sort(allvars_.begin(), allvars_.end(),
                [](Variable* a, Variable* b) { return a->address() < b->address(); });

            variables_ = std::move(newLocals);
            globals_ = std::move(newGlobals);
            functions_ = std::move(newFunctions);

            tags_.clear();
            for (int16_t i = 0; i < dbg.tags; i++) {
                int32_t tag_id = r.ReadUInt16();
                std::string name = ReadNameFromReader(r);
                tags_.push_back(std::make_unique<Tag>(name, tag_id));
            }
            for (int16_t i = 0; i < dbg.automatons; i++) {
                int16_t automation_id = r.ReadInt16();
                int32_t addr = r.ReadInt32();
                std::string name = ReadNameFromReader(r);
                automations_.push_back(std::make_unique<Automation>(automation_id, addr, name));
            }
            for (int16_t i = 0; i < dbg.states; i++) {
                int16_t state_id = r.ReadInt16();
                int16_t automation_id = r.ReadInt16();
                std::string name = ReadNameFromReader(r);
                states_.push_back(std::make_unique<State>(state_id, automation_id, name));
            }

            tags_.push_back(std::make_unique<Tag>("String", Q_USER_TAG_STRING));
            stringTag_ = tags_.back().get();

            for (auto& f : functions_) f->setTag(findTag(f->tag_id()));
            for (auto& v : variables_) v->setTag(findTagWithVar(v->tag_id(), v.get()));
            for (auto& v : globals_)   v->setTag(findTagWithVar(v->tag_id(), v.get()));

            for (auto& fun : functions_) {
                int32_t argNum = 0;
                std::vector<Argument> args;
                while (true) {
                    int64_t argOffset = 12 + 4 * argNum;
                    if (!lookupVariable(fun->address(), argOffset)) break;
                    args.push_back(buildArgumentInfo(fun.get(), argNum));
                    argNum++;
                }
                fun->setArguments(std::move(args));
            }
        }
        else if (amx.file_version == 7) {
            int32_t count = (amx.nametable - amx.tags) / DEFSIZE;
            BinaryReader tr(binary.data() + amx.tags, (size_t)count * DEFSIZE);
            for (int16_t i = 0; i < count; i++) {
                int64_t tag_id = (int64_t)tr.ReadUInt32();
                int32_t nameoffset = tr.ReadInt32();
                std::string name = ReadName(binary, (size_t)nameoffset);
                tags_.push_back(std::make_unique<Tag>(name, tag_id));
            }
        }
    }

    Argument AMXModXFile::buildArgumentInfo(Function* func, int32_t argNum) {
        int64_t argOffset = 12 + 4 * argNum;
        Variable* var = lookupVariable(func->address(), argOffset);
        if (!var) throw std::runtime_error("buildArgumentInfo: variable not found");

        if (var->type() == VariableType::ArrayReference
            && !var->dims().empty() && var->dims().size() == 1
            && var->dims()[0].size() == 0
            && var->tag() && var->tag()->name() == "_") {
            var->setTag(stringTag_);
            var->setTagId(Q_USER_TAG_STRING);
        }

        int32_t tagId = var->tag() ? (int32_t)var->tag()->tag_id() : -1;
        return Argument(var->type(), var->name(), tagId, var->tag(),
            std::vector<Dimension>(var->dims()));
    }

    std::vector<uint8_t> AMXModXFile::decompressCompactCode(const AMX_HEADER& amx, const std::vector<uint8_t>& binary) {
        std::vector<uint8_t> out;
        out.reserve(binary.size());
        out.insert(out.end(), binary.begin(), binary.begin() + amx.cod);

        int32_t codesize = amx.size - amx.cod;
        auto code = Slice(binary, (size_t)amx.cod, (size_t)codesize);
        std::vector<uint32_t> stack;

        while (codesize > 0) {
            uint32_t c = 0;
            int16_t shift = 0;
            do {
                codesize--;
                assert(shift < 32);
                assert(shift > 0 || (code[codesize] & 0x80) == 0);
                c |= (uint32_t)(code[codesize] & 0x7f) << shift;
                shift += 7;
            } while (codesize > 0 && (code[codesize - 1] & 0x80) != 0);

            if ((code[codesize] & 0x40) != 0) {
                while (shift < 32) { c |= (uint32_t)0xff << shift; shift += 8; }
            }
            stack.push_back(c);
        }

        while (!stack.empty()) {
            uint32_t v = stack.back(); stack.pop_back();
            out.push_back((uint8_t)(v));
            out.push_back((uint8_t)(v >> 8));
            out.push_back((uint8_t)(v >> 16));
            out.push_back((uint8_t)(v >> 24));
        }
        out.insert(out.end(), binary.begin() + amx.size, binary.end());
        return out;
    }

    Tag* AMXModXFile::findTagWithVar(int64_t tag_id, Variable* var) {
        Tag* tag = findTag(tag_id);
        if (!tag) return nullptr;
        if (tag->name() != "_") return tag;
        if (Tag* maybe = findTagString(var)) return maybe;
        return tag;
    }

    Tag* AMXModXFile::findTagString(Variable* var) {
        if (var->scope() == Scope::Local) return nullptr;
        if (var->dims().empty()) return nullptr;

        if (var->dims().size() == 1
            && (var->type() == VariableType::ArrayReference
                || var->type() == VariableType::Array
                || var->type() == VariableType::Reference)) {

            bool isPastVar = false;
            int64_t size = 0;
            for (size_t i = 0; i < allvars_.size(); i++) {
                if (allvars_[i] == var) { isPastVar = true; continue; }
                if (isPastVar && allvars_[i]->scope() != Scope::Local) {
                    size = allvars_[i]->address() - var->address();
                    break;
                }
            }
            if (size <= 0) return nullptr;

            int32_t end = (int32_t)(var->address() + size - 4);
            if (BitConverter::ToInt32(DAT().data(), (size_t)end) != 0) return nullptr;

            int32_t addr = (int32_t)var->address();
            for (; addr < (int32_t)DAT().size() && addr < end; addr += 4) {
                int32_t cell = BitConverter::ToInt32(DAT().data(), (size_t)addr);
                if (cell == 0) return nullptr;
                if (cell < 0 || cell > 0x10FFFF || (cell >= 0xD800 && cell <= 0xDFFF)) return nullptr;
            }
            if (addr != end) return nullptr;
            return stringTag_;
        }
        return nullptr;
    }

    bool AMXModXFile::IsMaybeString(int64_t address) {
        if (!isValidDataAddress(address)) return false;

        if (address >= 4) {
            int32_t cell = BitConverter::ToInt32(DAT().data(), (size_t)(address - 4));
            if (cell > 0 && cell <= 0x10FFFF && !(cell >= 0xD800 && cell <= 0xDFFF)) return false;
        }

        int32_t len = 0;
        while ((size_t)address < DAT().size()) {
            int32_t cell = BitConverter::ToInt32(DAT().data(), (size_t)address);
            if (cell == 0) break;
            if (cell < 0 || cell > 0x10FFFF || (cell >= 0xD800 && cell <= 0xDFFF)) return false;
            address += 4;
            len++;
        }
        return len > 1;
    }

    std::string AMXModXFile::stringFromData(int64_t address) {
        return ReadStringCells(DAT(), (size_t)address);
    }

    std::string AMXModXFile::stringFromData(int64_t address, int32_t maxread) {
        return ReadStringCellsEx(DAT(), (size_t)address, (size_t)maxread);
    }

    float AMXModXFile::floatFromData(int64_t address) {
        return BitConverter::ToSingle(DAT().data(), (size_t)address);
    }

    int32_t AMXModXFile::int32FromData(int64_t address) {
        if (address < 0 || (size_t)address >= DAT().size()) return 0;
        return BitConverter::ToInt32(DAT().data(), (size_t)address);
    }

    PawnFile::Automation* AMXModXFile::lookupAutomation(int64_t state_addr) {
        for (auto& a : automations_)
            if (a->address() == state_addr) return a.get();
        return nullptr;
    }

    const std::string* AMXModXFile::lookupState(int16_t state_id, int16_t automation_id) {
        for (auto& s : states_)
            if (s->automation_id() == automation_id && s->state_id() == state_id) return &s->name();
        return nullptr;
    }

}