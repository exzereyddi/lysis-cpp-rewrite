#include "core/pawn_file.hpp"
#include "core/util.hpp"
#include "ir/method_parser.hpp"
#include "ir/instructions.hpp"
#include "ir/node_builder.hpp"
#include "ir/node_analysis.hpp"
#include "ir/node_rewriter.hpp"
#include "ir/node_renamer.hpp"
#include "ir/type_propagation.hpp"
#include "ir/source_structure_builder.hpp"
#include "ir/source_builder.hpp"

#include <chrono>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#  include <eh.h>

static std::string exeDirectory() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf, n);
    size_t slash = s.find_last_of("\\/");
    return (slash == std::string::npos) ? std::string(".") : s.substr(0, slash);
}

#  if defined(_MSC_VER)
static void se_translator(unsigned int code, EXCEPTION_POINTERS*) {
    throw std::runtime_error(std::string("SEH exception 0x") + std::to_string(code));
}
struct SehInstaller {
    SehInstaller() { _set_se_translator(se_translator); }
};
static SehInstaller g_seh_installer;
#  endif

#else
static std::string exeDirectory() { return "."; }
#endif

static void trim(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
}

static std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}

static std::string nowString() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

struct PluginInfo {
    std::string name;
    std::string version;
    std::string author;
};

static PluginInfo extractPluginInfo(lysis::PawnFile* file) {
    PluginInfo info;
    auto readStr = [&](int64_t addr) -> std::string {
        return file->isValidDataAddress(addr) ? file->stringFromData(addr) : std::string();
        };

    for (auto& g : file->globals()) {
        if (g->name() == "myinfo") {
            info.name = readStr(file->int32FromData(g->address() + 0));
            info.version = readStr(file->int32FromData(g->address() + 12));
            info.author = readStr(file->int32FromData(g->address() + 8));
            if (!info.name.empty()) return info;
        }
    }

    for (auto& g : file->globals()) {
        const std::string& n = g->name();
        if (n == "PLUGIN")  info.name = readStr(g->address());
        if (n == "VERSION") info.version = readStr(g->address());
        if (n == "AUTHOR")  info.author = readStr(g->address());
    }
    if (!info.name.empty()) return info;

    lysis::Native* regNative = nullptr;
    for (auto& n : file->natives()) {
        if (n->name() == "register_plugin") { regNative = n.get(); break; }
    }
    if (!regNative) return info;

    const auto& code = file->code().bytes();
    size_t codeSize = code.size();
    if (codeSize < 12) return info;

    auto rd32 = [&](size_t off) -> int32_t {
        return lysis::BitConverter::ToInt32(code.data(), off);
        };
    auto rdU32 = [&](size_t off) -> uint32_t {
        return lysis::BitConverter::ToUInt32(code.data(), off);
        };

    for (size_t pc = 0; pc + 4 < codeSize; pc += 4) {
        uint32_t op = rdU32(pc);
        auto sp = (lysis::SPOpcode)op;
        if (sp != lysis::SPOpcode::sysreq_c && sp != lysis::SPOpcode::sysreq_n) continue;
        if (pc + 8 > codeSize) continue;
        uint32_t nativeIdx = rdU32(pc + 4);
        if ((int32_t)nativeIdx != regNative->index()) continue;

        std::vector<int64_t> strAddrs;
        int64_t argCount = -1;
        size_t scan = pc;

        for (int step = 0; step < 20 && scan >= 8; step++) {
            scan -= 8;
            uint32_t backOp = rdU32(scan);
            int32_t backVal = rd32(scan + 4);
            auto bop = (lysis::SPOpcode)backOp;
            if (bop == lysis::SPOpcode::push_c) {
                if (argCount == -1 && (backVal == 12 || backVal == 3)) {
                    argCount = backVal;
                }
                else {
                    strAddrs.push_back(backVal);
                    if (strAddrs.size() >= 3) break;
                }
            }
        }

        if (strAddrs.size() >= 3) {
            info.name = readStr(strAddrs[0]);
            info.version = readStr(strAddrs[1]);
            info.author = readStr(strAddrs[2]);
            if (!info.name.empty()) return info;
        }
    }

    return info;
}

static std::string formatPluginBanner(const PluginInfo& p) {
    std::string s;
    if (!p.name.empty()) s += p.name;
    else s += "<unknown plugin>";
    if (!p.version.empty()) s += " v" + p.version;
    if (!p.author.empty())  s += " by " + p.author;
    return s;
}

static void preprocessMethod(lysis::PawnFile* file, lysis::Function* func) {
    lysis::MethodParser mp(file, func);
    mp.preprocess();

    int32_t nargs = mp.getNumArgs();
    if ((int64_t)func->codeEnd() == (int64_t)file->code().bytes().size() + 1)
        func->setCodeEnd(mp.getExitPC() - 4);

    if (func->args().empty() || (int)func->args().size() < nargs) {
        std::vector<lysis::Argument> args;
        int32_t start = 0;
        if (!func->args().empty()) {
            start = (int32_t)func->args().size();
            for (const auto& a : func->args()) args.push_back(a);
        }
        for (int32_t i = start; i < nargs; i++) {
            file->addArgumentDummyVar(func, i);
            try {
                args.push_back(file->buildArgumentInfo(func, i));
            }
            catch (...) {
                lysis::Argument arg(lysis::VariableType::Normal,
                    "_arg" + std::to_string(i), 0, nullptr, {});
                arg.markGenerated();
                args.push_back(std::move(arg));
            }
        }
        for (int32_t i = nargs;; i++) {
            try { args.push_back(file->buildArgumentInfo(func, i)); }
            catch (...) { break; }
        }
        func->setArguments(std::move(args));
    }
}

static void dumpMethod(lysis::PawnFile* file, lysis::SourceBuilder& source, lysis::Function* func) {
    lysis::MethodParser mp(file, func);
    auto graph = mp.parse();
    if (!graph) return;

    lysis::NodeBuilder nb(file, graph.get());
    auto ngraph = nb.buildNodes();

    lysis::NodeAnalysis::RemoveDeadCode(*ngraph);
    lysis::NodeRewriter rewriter(*ngraph);
    rewriter.rewrite();
    lysis::NodeAnalysis::CollapseArrayReferences(*ngraph);

    lysis::ForwardTypePropagation ftypes(*ngraph);
    lysis::BackwardTypePropagation btypes(*ngraph);
    ftypes.propagate(); btypes.propagate();
    ftypes.propagate(); btypes.propagate();

    lysis::NodeAnalysis::CollapseArrayReferences(*ngraph);
    ftypes.propagate(); btypes.propagate();

    lysis::NodeAnalysis::CoalesceLoadStores(*ngraph);
    lysis::NodeAnalysis::HandleMemCopys(*ngraph);
    lysis::NodeAnalysis::AnalyzeHeapUsage(*ngraph);
    lysis::NodeAnalysis::RemoveGuards(*ngraph);
    lysis::NodeAnalysis::RemoveDeadCode(*ngraph);

    lysis::NodeRenamer renamer(*ngraph);
    renamer.rename();

    lysis::NodeAnalysis::CoalesceLoadsAndDeclarations(*ngraph);

    lysis::SourceStructureBuilder sb(*ngraph);
    source.write(sb.build());
}

static int64_t fileSize(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    return f ? (int64_t)f.tellg() : -1;
}

static void processFile(const std::string& path) {
    std::cout << "\n=== " << path << " ===\n";
    auto tStart = std::chrono::steady_clock::now();

    std::string outPath = exeDirectory() + "\\" + baseName(path) + "_decompile.sma";
    std::ofstream out(outPath);
    if (!out) {
        std::cerr << "ERROR: can't open output " << outPath << "\n";
        return;
    }

    std::string startedAt = nowString();

    try {
        auto file = lysis::PawnFile::FromFile(path);
        PluginInfo info = extractPluginInfo(file.get());

        std::cout << "Plugin: " << formatPluginBanner(info) << "\n";

        for (auto& fun : file->functions()) {
            try { preprocessMethod(file.get(), fun.get()); }
            catch (...) {}
        }

        out << "// Decompile by fck\n";
        out << "// Decompiled: " << startedAt << " (took ...s)\n";
        out << "// Plugin: " << formatPluginBanner(info) << "\n\n";

        lysis::SourceBuilder source(file.get(), out);
        source.writeGlobals();
        out << "\n";

        int okCount = 0, errCount = 0;
        for (size_t i = 0; i < file->functions().size(); i++) {
            lysis::Function* fun = file->functions()[i].get();
            if (i % 20 == 0 || i == file->functions().size() - 1) {
                std::cout << "// [" << (i + 1) << "/" << file->functions().size()
                    << "] " << fun->name() << "\n" << std::flush;
            }
            try {
                dumpMethod(file.get(), source, fun);
                out << "\n";
                out.flush();
                okCount++;
            }
            catch (const std::exception& e) {
                out << "\n/* ERROR decompiling \"" << fun->name() << "\": " << e.what() << " */\n";
                out.flush();
                errCount++;
                source = lysis::SourceBuilder(file.get(), out);
            }
            catch (...) {
                out << "\n/* ERROR decompiling \"" << fun->name() << "\" (unknown) */\n";
                out.flush();
                errCount++;
                source = lysis::SourceBuilder(file.get(), out);
            }
        }
        out.close();

        auto tEnd = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(tEnd - tStart).count();

        {
            std::ifstream in(outPath, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();

            char timeBuf[32];
            std::snprintf(timeBuf, sizeof(timeBuf), "%.2fs", secs);
            std::string marker = "(took ...s)";
            std::string replacement = std::string("(took ") + timeBuf + ")";
            size_t p = content.find(marker);
            if (p != std::string::npos) content.replace(p, marker.size(), replacement);

            std::ofstream ow(outPath, std::ios::binary);
            ow.write(content.data(), (std::streamsize)content.size());
        }

        int64_t sz = fileSize(outPath);
        std::string sizeStr;
        if (sz < 0) sizeStr = "?";
        else if (sz < 1024) sizeStr = std::to_string(sz) + " B";
        else                sizeStr = std::to_string(sz / 1024) + " KB";

        std::cout << "\nDecompiled: " << okCount << " ok, " << errCount << " errors\n";
        std::cout << "Written: " << baseName(path) << "_decompile.sma"
            << " (" << sizeStr << ") in "
            << std::fixed << std::setprecision(2) << secs << "s\n";

    }
    catch (const std::exception& e) {
        out << "\n// PARSING FAILED: " << e.what() << "\n";
        std::cerr << "ERROR: " << e.what() << "\n";
    }
    catch (...) {
        out << "\n// PARSING FAILED: unknown exception\n";
        std::cerr << "ERROR: unknown exception\n";
    }
}

int main(int argc, char** argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            std::string p = argv[i];
            trim(p);
            processFile(p);
        }
        std::cout << "\n";
    }

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        trim(line);
        if (line.empty()) break;
        processFile(line);
    }
    return 0;
}