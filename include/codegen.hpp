#pragma once

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

#include <llvm/IR/Type.h>
#include <memory>
#include <stack>
#include <string_view>
#include <unordered_map>

#include "ast.hpp"
#include "errors.hpp"

class Codegen {
  private:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;
    ErrorCollector& error;
    std::string_view name;

    std::unordered_map<std::string_view, llvm::StructType*> topLevelStructs;
    std::stack<std::unordered_map<std::string_view, llvm::Value*>> scopes;

    llvm::Type* typeConvertion(Type* type, Span* span);

    llvm::Value* genTopLevel(Stmt* stmt);
    llvm::Value* genStmt(Stmt* stmt);
    llvm::Value* genExpr(Expr* expr);

    llvm::Function* declareFunction(Stmt* stmt);

  public:
    Codegen(std::string_view name, ErrorCollector& errors);
    ~Codegen();

    void generate(SourceFile* source);
};