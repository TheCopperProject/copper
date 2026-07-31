#include "compiling/emit.hpp"
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>

bool emitObjectFile(llvm::Module *module, const std::string &outputPath,
                     const std::string &tripleStr, std::string &errMsg)
{
    llvm::Triple targetTriple(tripleStr);
    module->setTargetTriple(targetTriple);

    std::string lookupErr;
    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(targetTriple, lookupErr);
    if (!target) { errMsg = lookupErr; return false; }

    llvm::TargetOptions opts;
    auto rm = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
    std::unique_ptr<llvm::TargetMachine> tm(
        target->createTargetMachine(targetTriple, "generic", "", opts, rm));

    module->setDataLayout(tm->createDataLayout());

    std::error_code ec;
    llvm::raw_fd_ostream dest(outputPath, ec, llvm::sys::fs::OF_None);
    if (ec) { errMsg = ec.message(); return false; }

    llvm::legacy::PassManager pass;
    if (tm->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile))
    {
        errMsg = "Couldn't emit this type of file";
        return false;
    }

    pass.run(*module);
    dest.flush();
    return true;
}
