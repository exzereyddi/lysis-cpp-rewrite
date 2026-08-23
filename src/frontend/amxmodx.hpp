#pragma once

#include "../core/pawn_file.hpp"
#include "../core/util.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lysis {

    class AMXModXFile : public PawnFile {
    public:
        static constexpr uint32_t MAGIC = 0x414D5842;
        static constexpr uint32_t MAGIC2 = 0x414D5858;
        static constexpr uint32_t MAGIC2_VERSION = 0x0300;
        static constexpr uint16_t AMX_MAGIC = 0xE0F1;
        static constexpr uint16_t AMX_DBG_MAGIC = 0xEFF1;
        static constexpr int32_t  MIN_FILE_VERSION = 6;
        static constexpr int32_t  MIN_DEBUG_FILE_VERSION = 8;
        static constexpr int32_t  CUR_FILE_VERSION = 8;
        static constexpr int32_t  DEFSIZE = 8;

        static constexpr int32_t AMX_FLAG_DEBUG = 0x02;
        static constexpr int32_t AMX_FLAG_COMPACT = 0x04;
        static constexpr int32_t AMX_FLAG_BYTEOPC = 0x08;
        static constexpr int32_t AMX_FLAG_NOCHECKS = 0x10;

        static constexpr uint8_t IDENT_VARIABLE = 1;
        static constexpr uint8_t IDENT_REFERENCE = 2;
        static constexpr uint8_t IDENT_ARRAY = 3;
        static constexpr uint8_t IDENT_REFARRAY = 4;
        static constexpr uint8_t IDENT_FUNCTION = 9;
        static constexpr uint8_t IDENT_VARARGS = 11;

        static constexpr int64_t Q_USER_TAG_STRING = 100500;

        explicit AMXModXFile(std::vector<uint8_t> binary);

        std::string stringFromData(int64_t address) override;
        std::string stringFromData(int64_t address, int32_t maxread) override;
        float       floatFromData(int64_t address) override;
        int32_t     int32FromData(int64_t address) override;
        bool        PassArgCountAsSize() override { return true; }
        Automation* lookupAutomation(int64_t state_addr) override;
        const std::string* lookupState(int16_t state_id, int16_t automation_id) override;
        bool        IsMaybeString(int64_t address) override;

        Argument buildArgumentInfo(Function* func, int32_t argNum) override;

    private:
        struct PluginHeader {
            uint8_t cellsize;
            int32_t disksize;
            int32_t imagesize;
            int32_t memsize;
            int32_t offset;
        };
        struct TableEntry {
            uint8_t cellSize;
            int32_t origSize;
            int32_t offset;
        };
        struct AMX_HEADER {
            int32_t size;
            int32_t magic;
            uint8_t file_version;
            uint8_t amx_version;
            int16_t flags;
            int16_t defsize;
            int32_t cod;
            int32_t dat;
            int32_t hea;
            int32_t stp;
            int32_t cip;
            int32_t publics;
            int32_t natives;
            int32_t libraries;
            int32_t pubvars;
            int32_t tags;
            int32_t nametable;
        };
        struct AMX_DEBUG_HDR {
            int32_t size;
            int32_t magic;
            uint8_t file_version;
            uint8_t amx_version;
            int16_t flags;
            int16_t files;
            int16_t lines;
            int16_t symbols;
            int16_t tags;
            int16_t automatons;
            int16_t states;
            static constexpr int32_t SIZE = 4 + 2 + (1 * 2) + (2 * 7);
        };

        std::vector<Variable*> allvars_;
        Tag* stringTag_ = nullptr;
        std::vector<std::unique_ptr<Automation>> automations_;
        std::vector<std::unique_ptr<State>>      states_;

        static std::string ReadName(const std::vector<uint8_t>& bytes, size_t offset);
        static std::string ReadNameFromReader(BinaryReader& r);
        static std::string ReadStringCells(const std::vector<uint8_t>& bytes, size_t offset);
        static std::string ReadStringCellsEx(const std::vector<uint8_t>& bytes, size_t offset, size_t maxread);
        static VariableType FromIdent(uint8_t ident);
        Tag* findTagWithVar(int64_t tag_id, Variable* var);
        Tag* findTagString(Variable* var);
        std::vector<uint8_t> decompressCompactCode(const AMX_HEADER& amx, const std::vector<uint8_t>& binary);
    };

}