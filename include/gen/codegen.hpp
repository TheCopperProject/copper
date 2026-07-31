#pragma once

#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Type.h>

#include <memory>
#include <vector>
#include <string_view>
#include <unordered_map>

#include "ast/ast.hpp"
#include "errors.hpp"
#include "ast/token.hpp"

struct StructInfo {
    llvm::StructType* llvmType;
    std::unordered_map<std::string_view, unsigned> fieldIndices;
};

class Codegen {
private:
    llvm::Function* curFunc;

    llvm::Type* i8ty;
    llvm::Type* i16ty;
    llvm::Type* i32ty;
    llvm::Type* i64ty;

    llvm::Type* f32ty;
    llvm::Type* f64ty;

    llvm::Type* boolty;
    llvm::Type* voidty;

    llvm::Type* ptrTy;

    ErrorCollector& error;
    std::string_view name;

    std::unordered_map<std::string_view, StructInfo> topLevelStructs;
    std::vector<std::unordered_map<std::string_view, llvm::Value*>> scopes;

    llvm::Type* typeConvertion(Type* type, Span* span);

    llvm::Value* genTopLevel(Stmt* stmt);
    llvm::Value* genStmt(Stmt* stmt);
    llvm::Value* genExpr(Expr* expr);
    llvm::Value* genAddr(Expr* expr);

    llvm::Value* genBinop(Expr* e, Expr* l, Expr* r, TokenKind op);
    llvm::Value* genCast(Expr* expr);
    llvm::Value* genSizeof(Expr* expr);
    llvm::Value* genIncDec(Expr* operand, TokenKind op, bool isPrefix);

    llvm::Function* declareFunction(Stmt* stmt);

    bool declare(std::string_view& name, llvm::Value* val);
    llvm::Value* lookup(std::string_view& name);

    void pushScope();
    void popScope();

    llvm::AllocaInst* createEntryAlloca(llvm::Type* ty, llvm::Value* arraySize,
                                        std::string_view& name);

public:
    std::unique_ptr<llvm::LLVMContext> context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<>> builder;

    Codegen(std::string_view name, ErrorCollector& errors);
    ~Codegen();

    bool generate(SourceFile* source);
};
