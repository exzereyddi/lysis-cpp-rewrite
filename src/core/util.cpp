#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace lysis {

    namespace BitConverter {

        int16_t ToInt16(const uint8_t* b, size_t o) {
            return (int16_t)((uint16_t)b[o] | ((uint16_t)b[o + 1] << 8));
        }

        uint16_t ToUInt16(const uint8_t* b, size_t o) {
            return (uint16_t)b[o + 1] | ((uint16_t)b[o] << 8);
        }

        int32_t ToInt32(const uint8_t* b, size_t o) {
            return (int32_t)((uint32_t)b[o]
                | ((uint32_t)b[o + 1] << 8)
                | ((uint32_t)b[o + 2] << 16)
                | ((uint32_t)b[o + 3] << 24));
        }

        uint32_t ToUInt32(const uint8_t* b, size_t o) {
            return (uint32_t)b[o]
                | ((uint32_t)b[o + 1] << 8)
                | ((uint32_t)b[o + 2] << 16)
                | ((uint32_t)b[o + 3] << 24);
        }

        uint64_t ToUInt64(const uint8_t* b, size_t o) {
            uint64_t v = 0;
            for (int i = 0; i <= 56; i += 8) v |= (uint64_t)b[o++] << i;
            return v;
        }

        float ToSingle(const uint8_t* b, size_t o) {
            uint32_t bits = ToUInt32(b, o);
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            return f;
        }

        std::vector<uint8_t> GetBytesBE(int32_t v) {
            return { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
        }

        std::vector<uint8_t> GetBytesBE(int64_t v) {
            return { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
        }

    }

    BinaryReader::BinaryReader(const uint8_t* data, size_t size, size_t offset)
        : data_(data), size_(size), pos_(offset) {
    }

    BinaryReader::BinaryReader(const std::vector<uint8_t>& data, size_t offset)
        : data_(data.data()), size_(data.size()), pos_(offset) {
    }

    void BinaryReader::ensure(size_t n) const {
        if (pos_ + n > size_) throw std::runtime_error("BinaryReader: unexpected end of stream");
    }

    int8_t   BinaryReader::ReadInt8() { ensure(1); return (int8_t)data_[pos_++]; }
    uint8_t  BinaryReader::ReadUInt8() { ensure(1); return data_[pos_++]; }

    int16_t  BinaryReader::ReadInt16() { ensure(2); auto v = BitConverter::ToInt16(data_, pos_);  pos_ += 2; return v; }
    uint16_t BinaryReader::ReadUInt16() { ensure(2); auto v = BitConverter::ToUInt16(data_, pos_); pos_ += 2; return v; }
    int32_t  BinaryReader::ReadInt32() { ensure(4); auto v = BitConverter::ToInt32(data_, pos_);  pos_ += 4; return v; }
    uint32_t BinaryReader::ReadUInt32() { ensure(4); auto v = BitConverter::ToUInt32(data_, pos_); pos_ += 4; return v; }

    void BinaryReader::ReadBytes(uint8_t* out, size_t count) {
        ensure(count);
        std::memcpy(out, data_ + pos_, count);
        pos_ += count;
    }

    std::vector<uint8_t> BinaryReader::ReadBytes(size_t count) {
        std::vector<uint8_t> r(count);
        ReadBytes(r.data(), count);
        return r;
    }

    void BinaryReader::Skip(size_t count) { ensure(count); pos_ += count; }
    void BinaryReader::Seek(size_t pos) {
        if (pos > size_) throw std::runtime_error("BinaryReader: seek out of range");
        pos_ = pos;
    }

    size_t BinaryReader::Position() const { return pos_; }
    size_t BinaryReader::Size() const { return size_; }
    bool   BinaryReader::Eof() const { return pos_ >= size_; }

    std::string ReadCString(const uint8_t* bytes, size_t size, size_t offset) {
        size_t count = offset;
        while (count < size && bytes[count] != 0) count++;
        return std::string(reinterpret_cast<const char*>(bytes + offset), count - offset);
    }

    std::string ReadCString(const uint8_t* bytes, size_t size, size_t offset, size_t maxread) {
        size_t count = offset;
        size_t last = count + maxread;
        while (count < size && count <= last && bytes[count] != 0) count++;
        return std::string(reinterpret_cast<const char*>(bytes + offset), count - offset);
    }

    namespace {

        std::vector<std::string> GetTriGrams(const std::string& term, bool useFiller) {
            std::vector<std::string> list;
            if (term.size() < 2) { list.push_back(term); return list; }
            if (useFiller) {
                list.push_back(std::string("__") + term[0]);
                list.push_back(std::string("_") + term[0] + term[1]);
            }
            for (size_t i = 0; i + 2 < term.size(); i++)
                list.push_back(term.substr(i, 3));
            if (useFiller) {
                std::string a; a += term[term.size() - 2]; a += term[term.size() - 1]; a += '_';
                list.push_back(a);
                std::string b; b += term[term.size() - 1]; b += "__";
                list.push_back(b);
            }
            return list;
        }

        bool iequals(const std::string& a, const std::string& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); i++)
                if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
            return true;
        }

    }

    namespace Similarity {

        float GetSimilarity(const std::string& first, const std::string& second) {
            bool useFiller = (first.size() < 5) ? (second.size() < 5) : false;
            auto firstList = GetTriGrams(first, useFiller);
            auto secondList = GetTriGrams(second, useFiller);
            size_t length = std::min(firstList.size(), secondList.size());
            int equals = 0;
            for (size_t i = 0; i < length; i++)
                if (iequals(firstList[i], secondList[i])) equals++;
            size_t total = firstList.size() + secondList.size();
            if (total == 0) return 0.0f;
            return 2.0f * (float)equals / (float)total;
        }

    }

    std::vector<uint8_t> Slice(const std::vector<uint8_t>& bytes, size_t offset, size_t length) {
        std::vector<uint8_t> r(length);
        for (size_t i = 0; i < length; i++) r[i] = bytes[offset + i];
        return r;
    }

    std::string ToHex(uint64_t value) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llx", (unsigned long long)value);
        return std::string(buf);
    }

    std::string Format(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        va_list args2;
        va_copy(args2, args);
        int need = std::vsnprintf(nullptr, 0, fmt, args);
        va_end(args);
        if (need < 0) { va_end(args2); return {}; }
        std::string result(need, '\0');
        std::vsnprintf(&result[0], (size_t)need + 1, fmt, args2);
        va_end(args2);
        return result;
    }

    std::vector<uint8_t> ReadWholeFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) throw std::runtime_error("Failed to open file: " + path);
        std::streamsize sz = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<uint8_t> data((size_t)sz);
        if (sz > 0 && !f.read(reinterpret_cast<char*>(data.data()), sz))
            throw std::runtime_error("Failed to read file: " + path);
        return data;
    }

}