#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace lysis {

    namespace BitConverter {
        int16_t  ToInt16(const uint8_t* bytes, size_t offset);
        uint16_t ToUInt16(const uint8_t* bytes, size_t offset);
        int32_t  ToInt32(const uint8_t* bytes, size_t offset);
        uint32_t ToUInt32(const uint8_t* bytes, size_t offset);
        uint64_t ToUInt64(const uint8_t* bytes, size_t offset);
        float    ToSingle(const uint8_t* bytes, size_t offset);

        std::vector<uint8_t> GetBytesBE(int32_t value);
        std::vector<uint8_t> GetBytesBE(int64_t value);
    }

    class BinaryReader {
    public:
        BinaryReader(const uint8_t* data, size_t size, size_t offset = 0);
        BinaryReader(const std::vector<uint8_t>& data, size_t offset = 0);

        int8_t   ReadInt8();
        uint8_t  ReadUInt8();
        int16_t  ReadInt16();
        uint16_t ReadUInt16();
        int32_t  ReadInt32();
        uint32_t ReadUInt32();

        void ReadBytes(uint8_t* out, size_t count);
        std::vector<uint8_t> ReadBytes(size_t count);

        void Skip(size_t count);
        void Seek(size_t pos);
        size_t Position() const;
        size_t Size() const;
        bool Eof() const;

        const uint8_t* Data() const { return data_; }

    private:
        const uint8_t* data_;
        size_t size_;
        size_t pos_;
        void ensure(size_t n) const;
    };

    std::string ReadCString(const uint8_t* bytes, size_t size, size_t offset);
    std::string ReadCString(const uint8_t* bytes, size_t size, size_t offset, size_t maxread);

    namespace Similarity {
        float GetSimilarity(const std::string& first, const std::string& second);
    }

    std::vector<uint8_t> Slice(const std::vector<uint8_t>& bytes, size_t offset, size_t length);
    std::string ToHex(uint64_t value);
    std::string Format(const char* fmt, ...);
    std::vector<uint8_t> ReadWholeFile(const std::string& path);

}