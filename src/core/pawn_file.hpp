#pragma once

#include "lstructure.hpp"
#include "util.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lysis {

    class Public {
    public:
        Public(std::string name, int64_t address) : name_(std::move(name)), address_(address) {}
        const std::string& name() const { return name_; }
        int64_t address()         const { return address_; }
    private:
        std::string name_;
        int64_t     address_;
    };

    class PawnFile {
    public:
        struct DebugHeader {
            int32_t numFiles = 0;
            int32_t numLines = 0;
            int32_t numSyms = 0;
        };

        class Code {
        public:
            Code() = default;
            Code(std::vector<uint8_t> code, uint8_t cellsize, int32_t flags, int64_t main, int32_t version)
                : code_(std::move(code)), cellsize_(cellsize), flags_(flags), main_(main), version_(version) {
            }
            const std::vector<uint8_t>& bytes() const { return code_; }
            uint8_t cellsize() const { return cellsize_; }
            int32_t flags()    const { return flags_; }
            int64_t main()     const { return main_; }
            int32_t version()  const { return version_; }
        private:
            std::vector<uint8_t> code_;
            uint8_t cellsize_ = 0;
            int32_t flags_ = 0;
            int64_t main_ = 0;
            int32_t version_ = 0;
        };

        class Data {
        public:
            Data() = default;
            Data(std::vector<uint8_t> data, int32_t memory) : data_(std::move(data)), memory_(memory) {}
            const std::vector<uint8_t>& bytes() const { return data_; }
            int32_t memory() const { return memory_; }
        private:
            std::vector<uint8_t> data_;
            int32_t memory_ = 0;
        };

        class PubVar {
        public:
            PubVar(std::string name, int64_t address) : name_(std::move(name)), address_(address) {}
            const std::string& name() const { return name_; }
            int64_t address()         const { return address_; }
        private:
            std::string name_;
            int64_t     address_;
        };

        class DebugFile {
        public:
            DebugFile(std::string name, int64_t address) : name_(std::move(name)), address_(address) {}
            const std::string& name() const { return name_; }
            int64_t address()         const { return address_; }
            std::string toString() const {
                return Format("File %s @ %llx", name_.c_str(), (unsigned long long)address_);
            }
        private:
            std::string name_;
            int64_t     address_;
        };

        class DebugLine {
        public:
            DebugLine(int32_t line, int64_t address) : line_(line), address_(address) {}
            int32_t line()    const { return line_; }
            int64_t address() const { return address_; }
            std::string toString() const {
                return Format("Line %d @ %llx", line_, (unsigned long long)address_);
            }
        private:
            int64_t address_;
            int32_t line_;
        };

        class Automation {
        public:
            Automation(int16_t id, int64_t addr, std::string name)
                : automation_id_(id), address_(addr), name_(std::move(name)) {
            }
            int16_t automation_id() const { return automation_id_; }
            int64_t address()       const { return address_; }
            const std::string& name() const { return name_; }
            std::string toString() const {
                return Format("Automation %d @ %llx : %s",
                    (int)automation_id_, (unsigned long long)address_, name_.c_str());
            }
        private:
            int16_t automation_id_;
            int64_t address_;
            std::string name_;
        };

        class State {
        public:
            State(int16_t s, int16_t a, std::string name)
                : state_id_(s), automation_id_(a), name_(std::move(name)) {
            }
            int16_t state_id()      const { return state_id_; }
            int16_t automation_id() const { return automation_id_; }
            const std::string& name() const { return name_; }
            std::string toString() const {
                return Format("State %d of automation %d : %s",
                    (int)state_id_, (int)automation_id_, name_.c_str());
            }
        private:
            int16_t state_id_;
            int16_t automation_id_;
            std::string name_;
        };

        virtual ~PawnFile() = default;

        static std::unique_ptr<PawnFile> FromFile(const std::string& path);
        static std::unique_ptr<PawnFile> FromBytes(std::vector<uint8_t> bytes);

        virtual std::string stringFromData(int64_t address) = 0;
        virtual std::string stringFromData(int64_t address, int32_t maxread) = 0;
        virtual float       floatFromData(int64_t address) = 0;
        virtual int32_t     int32FromData(int64_t address) = 0;
        virtual bool        PassArgCountAsSize() = 0;
        virtual Automation* lookupAutomation(int64_t state_addr) = 0;
        virtual const std::string* lookupState(int16_t state_id, int16_t automation_id) = 0;
        virtual bool        IsMaybeString(int64_t address) = 0;

        bool isValidDataAddress(int64_t address) const {
            return address >= 0 && (size_t)address < DAT().size();
        }

        Function* lookupFunction(int64_t pc);
        Public* lookupPublic(const std::string& name);
        Public* lookupPublic(int64_t addr);
        const std::string* lookupFile(int64_t address);
        int32_t   lookupLine(int64_t address);

        Variable* lookupDeclarations(int64_t pc, size_t& i, Scope scope);
        Variable* lookupDeclarations(int64_t pc, size_t& i) { return lookupDeclarations(pc, i, Scope::Local); }

        Variable* lookupVariable(int64_t pc, int64_t offset, Scope scope);
        Variable* lookupVariable(int64_t pc, int64_t offset) { return lookupVariable(pc, offset, Scope::Local); }

        Variable* lookupGlobal(int64_t address);

        Function* addFunction(int64_t addr);
        bool      addArgumentDummyVar(Function* func, int32_t num);
        virtual Argument buildArgumentInfo(Function* func, int32_t argNum);
        void      addGlobal(int64_t addr);

        std::vector<std::unique_ptr<Function>>& functions() { return functions_; }
        std::vector<std::unique_ptr<Public>>& publics() { return publics_; }
        std::vector<std::unique_ptr<Variable>>& globals() { return globals_; }
        std::vector<std::unique_ptr<Native>>& natives() { return natives_; }
        std::vector<std::unique_ptr<PubVar>>& pubvars() { return pubvars_; }
        std::vector<std::unique_ptr<Tag>>& tags() { return tags_; }
        std::vector<std::unique_ptr<Variable>>& variables() { return variables_; }
        std::vector<std::unique_ptr<DebugFile>>& debugFiles() { return debugFiles_; }
        std::vector<std::unique_ptr<DebugLine>>& debugLines() { return debugLines_; }

        Code& code() { return code_; }
        Data& data() { return data_; }
        const Code& code() const { return code_; }
        const Data& data() const { return data_; }

        const std::vector<uint8_t>& DAT() const { return data_.bytes(); }
        DebugHeader& debugHeader() { return debugHeader_; }

    protected:
        static std::vector<uint8_t> Slice(const std::vector<uint8_t>& bytes, size_t off, size_t len) {
            return lysis::Slice(bytes, off, len);
        }

        Tag* findTag(int64_t tag_id);
        Tag* findTag(const std::string& name);
        Tag* findOrCreateTag(const std::string& name);
        Tag* addTag(const std::string& name, int64_t tag_id);

        std::vector<std::unique_ptr<Function>>  functions_;
        std::vector<std::unique_ptr<Public>>    publics_;
        std::vector<std::unique_ptr<Variable>>  globals_;
        std::vector<std::unique_ptr<Variable>>  variables_;
        std::vector<std::unique_ptr<Tag>>       tags_;
        std::vector<std::unique_ptr<PubVar>>    pubvars_;
        std::vector<std::unique_ptr<Native>>    natives_;
        std::vector<std::unique_ptr<DebugFile>> debugFiles_;
        std::vector<std::unique_ptr<DebugLine>> debugLines_;
        DebugHeader debugHeader_;

        Code code_;
        Data data_;
    };

}