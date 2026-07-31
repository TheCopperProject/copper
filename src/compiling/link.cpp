#include "compiling/link.hpp"
#include <lld/Common/Driver.h>
#include <lld/Common/CommonLinkerContext.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/FileSystem.h>
#include <filesystem>
#include <vector>
#include <algorithm>

LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(elf)

namespace fs = std::filesystem;

TargetOS targetOSFromTriple(const std::string &triple)
{
    std::string t = triple;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);

    if (t.find("windows") != std::string::npos)
        return TargetOS::Windows;
    if (t.find("linux") != std::string::npos)
        return TargetOS::Linux;

    return TargetOS::Windows;
}

std::string runtimeSubdir(TargetOS os)
{
    switch (os)
    {
    case TargetOS::Windows: return "windows";
    case TargetOS::Linux:   return "linux";
    }
    return "windows";
}

static fs::path runtimeDir(const char *argv0, TargetOS os)
{
    fs::path exePath = fs::absolute(argv0);
    return exePath.parent_path() / "runtime" / runtimeSubdir(os);
}

static bool linkWindows(const std::vector<std::string> &objPaths, const std::string &exePath,
                         const fs::path &rt, std::string &errMsg)
{
    std::vector<std::string> argStrs = {
        "lld-link",
    };
    for (const auto &obj : objPaths)
        argStrs.push_back(obj);

    argStrs.insert(argStrs.end(), {
        "/out:" + exePath,
        "/entry:mainCRTStartup",
        "/subsystem:console",
        "/machine:x64",
        "/libpath:" + rt.string(),
        "crt2.o",
        "crtbegin.o",
        "libmingw32.a",
        "libgcc.a",
        "libgcc_eh.a",
        "libmoldname.a",
        "libmingwex.a",
        "libmsvcrt.a",
        "libadvapi32.a",
        "libshell32.a",
        "libuser32.a",
        "libkernel32.a",
        "crtend.o",
        "/threads:1",
    });

    std::vector<const char *> args;
    args.reserve(argStrs.size());
    for (auto &s : argStrs) args.push_back(s.c_str());

    std::string outStr, errStr;
    llvm::raw_string_ostream outOS(outStr), errOS(errStr);

    bool ok = lld::coff::link(args, outOS, errOS, false, false);
    outOS.flush();
    errOS.flush();

    if (!ok)
    {
        errMsg = errStr.empty() ? "lld-link failed without a message" : errStr;
        return false;
    }
    if (!errStr.empty())
        llvm::errs() << errStr;

    return true;
}

static bool linkLinux(const std::vector<std::string> &objPaths, const std::string &exePath,
                       const fs::path &rt, std::string &errMsg)
{
    std::string dynLinker = "/lib64/ld-linux-x86-64.so.2";

    std::vector<std::string> argStrs = {
        "ld.lld",
        "-o", exePath,
        "--dynamic-linker", dynLinker,
        (rt / "crt1.o").string(),
        (rt / "crti.o").string(),
        (rt / "crtbeginS.o").string(),
    };
    for (const auto &obj : objPaths)
        argStrs.push_back(obj);

    argStrs.insert(argStrs.end(), {
        "-L" + rt.string(),
        "-lc",
        "-lgcc",
        (rt / "crtendS.o").string(),
        (rt / "crtn.o").string(),
    });

    std::vector<const char *> args;
    args.reserve(argStrs.size());
    for (auto &s : argStrs) args.push_back(s.c_str());

    std::string outStr, errStr;
    llvm::raw_string_ostream outOS(outStr), errOS(errStr);

    bool ok = lld::elf::link(args, outOS, errOS, false, false);
    outOS.flush();
    errOS.flush();

    if (!ok)
    {
        errMsg = errStr.empty() ? "ld.lld failed without message" : errStr;
        return false;
    }
    if (!errStr.empty())
        llvm::errs() << errStr;

    return true;
}

bool linkExecutable(const std::vector<std::string> &objPaths, const std::string &exePath,
                     const char *argv0, TargetOS os, std::string &errMsg)
{
    if (objPaths.empty())
    {
        errMsg = "No object files were provided to link";
        return false;
    }

    fs::path rt = runtimeDir(argv0, os);
    if (!fs::exists(rt))
    {
        errMsg = "Couldn't find the directory runtime/" + runtimeSubdir(os) +
                  "/ with the compilator (" + rt.string() + ")";
        return false;
    }

    bool ok;
    switch (os)
    {
    case TargetOS::Windows:
        ok = linkWindows(objPaths, exePath, rt, errMsg);
        break;
    case TargetOS::Linux:
        ok = linkLinux(objPaths, exePath, rt, errMsg);
        break;
    default:
        errMsg = "Unhandled OS";
        ok = false;
    }

    lld::CommonLinkerContext::destroy();

    return ok;
}
