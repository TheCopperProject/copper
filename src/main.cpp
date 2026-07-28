#include "lexer.hpp"
#include "parser.hpp"
#include "ast_printer.hpp"
#include "errors.hpp"
#include "arena.hpp"
#include "optimizer.hpp"
#include "sema.hpp"
#include "emit.hpp"
#include "codegen.hpp"
#include "link.hpp"

#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace fs = std::filesystem;

static bool readFile(const std::string &path, std::string &out)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open())
        return false;

    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

struct CompilationUnit
{
    std::string path;
    std::unique_ptr<ArenaAllocator> arena;
    std::unique_ptr<ErrorCollector> errors;
    //std::unique_ptr<Codegen> codegen;
    std::string objPath;
};

struct Options
{
    std::vector<std::string> inputs;
    std::string outExe = "out.exe";
    std::string triple;
    bool emitLLVMIR = false; 
    bool printAst = false;  
};

static void printUsage(const char *argv0)
{
    std::cerr << "uso: " << argv0 << " <archivo1.jc> [archivo2.jc ...] -o <salida.exe> "
              << "[--target=<triple>] [--emit-llvm-ir] [--print-ast]\n";
}

static bool parseArgs(int argc, char **argv, Options &opts)
{
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];

        if (arg == "-o")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "error: '-o' requiere un argumento\n";
                return false;
            }
            opts.outExe = argv[++i];
        }
        else if (arg.rfind("--target=", 0) == 0)
        {
            opts.triple = arg.substr(std::string("--target=").size());
        }
        else if (arg == "--emit-llvm-ir")
        {
            opts.emitLLVMIR = true;
        }
        else if (arg == "--print-ast")
        {
            opts.printAst = true;
        }
        else if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return false;
        }
        else
        {
            opts.inputs.push_back(arg);
        }
    }

    if (opts.inputs.empty() && argc == 1)
    {
        opts.inputs.push_back("test.jc");
    }

    return true;
}

int main(int argc, char **argv)
{
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();

    Options opts;
    if (!parseArgs(argc, argv, opts))
        return 1;

    if (opts.inputs.empty())
    {
        printUsage(argv[0]);
        return 1;
    }

    std::string triple = !opts.triple.empty() ? opts.triple : llvm::sys::getDefaultTargetTriple();
    TargetOS os = targetOSFromTriple(triple);

    std::vector<std::unique_ptr<CompilationUnit>> units;
    units.reserve(opts.inputs.size());

    bool anyErrors = false;

    for (const auto &path : opts.inputs)
    {
        auto unit = std::make_unique<CompilationUnit>();
        unit->path = path;

        std::string source;
        if (!readFile(path, source))
        {
            std::cerr << "No se pudo abrir: " << path << "\n";
            anyErrors = true;
            continue;
        }

        unit->arena = std::make_unique<ArenaAllocator>();
        unit->errors = std::make_unique<ErrorCollector>(unit->path);

        std::vector<Token> tokens = tokenify(unit->path.c_str(), source.c_str(), *unit->arena);

        Parser parser(tokens, *unit->arena, *unit->errors, 0);
        SourceFile *file = optimizeSourceFile(parser.parseSourceFile(), *unit->arena, *unit->errors);

        if (!unit->errors->hasErrors())
        {
            Sema sema(*unit->arena, *unit->errors);
            sema.analyze(file);
        }

        if (unit->errors->hasErrors())
        {
            anyErrors = true;
            std::cout << "--- Diagnostics (" << unit->path << ") ---\n";
            for (const auto &d : unit->errors->all())
            {
                std::cout << "[" << (d.severity == Severity::ERROR ? "ERROR" : "WARN") << "] "
                          << unit->errors->src_name << " "
                          << d.span.start_line << ":" << d.span.start_column
                          << " - " << d.message << "\n";
            }
            units.push_back(std::move(unit));
            continue;
        }

        if (opts.printAst)
            std::cout << AstPrinter::print(file) << "\n";


        units.push_back(std::move(unit));
    }

    if (anyErrors)
        return 1;

    std::vector<std::string> objPaths;
    objPaths.reserve(units.size());

    fs::path buildDir = fs::path(opts.outExe).parent_path();
    if (buildDir.empty())
        buildDir = ".";

    for (size_t i = 0; i < units.size(); i++)
    {
        CompilationUnit &unit = *units[i];
        std::string stem = fs::path(unit.path).stem().string();

        if (opts.emitLLVMIR)
        {
            std::string llPath = (buildDir / (stem + "." + std::to_string(i) + ".ll")).string();
            std::error_code ec;
            llvm::raw_fd_ostream llOut(llPath, ec, llvm::sys::fs::OF_None);
            if (ec)
            {
                std::cerr << "No se pudo escribir " << llPath << ": " << ec.message() << "\n";
                return 1;
            }
            //unit.codegen->module->print(llOut, nullptr);
        }

        unit.objPath = (buildDir / (stem + "." + std::to_string(i) + ".o")).string();

        /*
        std::string errMsg;
        if (!emitObjectFile(unit.codegen->module.get(), unit.objPath, triple, errMsg))
        {
            std::cerr << "Error emitting object (" << unit.path << "): " << errMsg << "\n";
            return 1;
        }
            */

        objPaths.push_back(unit.objPath);
    }

    /*
    std::string errMsg;
    if (!linkExecutable(objPaths, opts.outExe, argv[0], os, errMsg))
    {
        std::cerr << "Linking error: " << errMsg << "\n";
        return 1;
    }*/

    return 0;
}
