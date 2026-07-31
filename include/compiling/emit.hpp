#pragma once
#include <llvm/IR/Module.h>
#include <string>

bool emitObjectFile(llvm::Module *module, const std::string &outputPath,
                     const std::string &triple, std::string &errMsg);