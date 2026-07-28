#include "llvm/IR/Verifier.h"

#include "codegen.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <cstddef>
#include <format>
#include <string>
#include "ast.hpp"

Codegen::Codegen(std::string_view name, ErrorCollector& errors)
    : error(errors), name(name) {
    topLevelStructs.clear();
    scopes.clear();
    globals.clear();

    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>(name, *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);

    i8ty = llvm::Type::getInt8Ty(*context);
    i16ty = llvm::Type::getInt16Ty(*context);
    i32ty = llvm::Type::getInt32Ty(*context);
    i64ty = llvm::Type::getInt64Ty(*context);

    f32ty = llvm::Type::getFloatTy(*context);
    f64ty = llvm::Type::getDoubleTy(*context);
    boolty = llvm::Type::getInt1Ty(*context);
    voidty = llvm::Type::getVoidTy(*context);

    ptrTy = llvm::PointerType::get(*context, 0);
}

llvm::Type* Codegen::typeConvertion(Type* type, Span* span) {
    if (!type)
        return voidty;

    switch (type->kind) {
        case TypeKind::I8:
        case TypeKind::U8:
            return i8ty;
        case TypeKind::I16:
        case TypeKind::U16:
            return i16ty;
        case TypeKind::I32:
        case TypeKind::U32:
        case TypeKind::INT_CONSTANT:
            return i32ty;
        case TypeKind::I64:
        case TypeKind::U64:
            return i64ty;

        case TypeKind::F32:
        case TypeKind::FLOAT_CONSTANT:
            return f32ty;
        case TypeKind::F64:
            return f64ty;

        case TypeKind::BOOL:
            return boolty;
        case TypeKind::VOID:
            return voidty;

        case TypeKind::POINTER:
            return ptrTy;
        default:
            error.error("unhandled type", *span);
            return voidty;
    }
}

llvm::Function* Codegen::declareFunction(Stmt* stmt) {
    std::vector<llvm::Type*> paramTypes;

    for (size_t i = 0; i < stmt->func_decl.param_count; i++) {
        Field param = stmt->func_decl.params[i];
        paramTypes.push_back(typeConvertion(param.type, &param.span));
    }

    llvm::FunctionType* functionType = llvm::FunctionType::get(
        typeConvertion(stmt->func_decl.return_type, &stmt->span), paramTypes,
        stmt->func_decl.is_variadic);

    llvm::Function* function =
        llvm::Function::Create(functionType, llvm::Function::ExternalLinkage,
                               std::string(stmt->func_decl.name), *module);

    unsigned idx = 0;
    for (llvm::Argument& Arg : function->args()) {
        auto name = stmt->func_decl.params[idx++].name;
        Arg.setName(name);
    }

    return function;
}

llvm::Value* Codegen::genTopLevel(Stmt* stmt) {
    switch (stmt->kind) {
        case StmtKind::UNION_DECL:
        case StmtKind::STRUCT_DECL: {
            llvm::StructType* structTy = llvm::StructType::create(
                *context, stmt->struct_union_decl.name);

            topLevelStructs[stmt->struct_union_decl.name] = structTy;

            return nullptr;
        }
        case StmtKind::FUNC_DECL: {
            return declareFunction(stmt);
        }
        default:
            return nullptr;
    }
}

llvm::Value* Codegen::genStmt(Stmt* stmt) {
    if (!stmt)
        return nullptr;

    switch (stmt->kind) {
        case StmtKind::UNION_DECL:
        case StmtKind::STRUCT_DECL:
            return nullptr;
        case StmtKind::FUNC_DECL: {
            llvm::Function* function =
                module->getFunction(std::string(stmt->func_decl.name));

            if (!function) {
                function = declareFunction(stmt);
            }

            if (!function->isDeclaration()) {
                error.error("function was already declarated", stmt->span);
                return nullptr;
            }

            if (!stmt->func_decl.is_extern) {
                llvm::BasicBlock* entryBlock =
                    llvm::BasicBlock::Create(*context, "entry", function);
                builder->SetInsertPoint(entryBlock);

                for (size_t i = 0; i < stmt->func_decl.param_count; ++i) {
                    Field& fParam = stmt->func_decl.params[i];
                    llvm::Type* paramTy =
                        typeConvertion(fParam.type, &fParam.span);
                    if (!paramTy) {
                        error.error("invalid parameter type", fParam.span);
                        return nullptr;
                    }
                    llvm::AllocaInst* alloca =
                        createEntryAlloca(paramTy, nullptr, fParam.name);
                    if (!alloca)
                        return nullptr;
                    llvm::Value* argVal = function->getArg(i);
                    builder->CreateStore(argVal, alloca);
                    declare(fParam.name, alloca);
                }

                pushScope();
                genStmt(stmt->func_decl.body);

                bool isVoidReturn =
                    !stmt->func_decl.return_type ||
                    stmt->func_decl.return_type->kind == TypeKind::VOID;

                if (isVoidReturn &&
                    !builder->GetInsertBlock()->getTerminator()) {
                    builder->CreateRetVoid();
                }
                popScope();
            }

            return function;
        }
        case StmtKind::CONST_DECL:
        case StmtKind::VAR_DECL: {
            auto name = stmt->var_decl.name;
            auto init = genExpr(stmt->var_decl.init);
            auto is_global = stmt->var_decl.is_global;
            auto is_mutable = stmt->var_decl.is_mutable;
            auto type =
                typeConvertion(stmt->var_decl.type, &stmt->var_decl.type->span);

            if (is_global) {
                auto function = module->getFunction(std::string(name));

                if (function) {
                    return function;
                }

                if (!init) {
                    error.error(
                        std::format("global {} needs a initial value", name),
                        stmt->span);
                } else {
                    if (auto* C = llvm::dyn_cast<llvm::Constant>(init)) {
                        llvm::GlobalVariable* globalVar =
                            new llvm::GlobalVariable(
                                *module, type, !is_mutable,
                                llvm::GlobalValue::ExternalLinkage, C,
                                std::string(name));

                        globals[name] = globalVar;
                    } else {
                        error.error(
                            std::format(
                                "global {} needs a constant as initial value",
                                name),
                            stmt->span);
                    }
                }
            } else {
                llvm::AllocaInst* alloca =
                    createEntryAlloca(type, nullptr, name);
                if (!alloca) {
                    return nullptr;
                }

                if (init) {
                    builder->CreateStore(init, alloca);
                }

                if (!declare(name, alloca)) {
                    error.error(
                        std::format("redefinition of '{}' in this scope", name),
                        stmt->span);
                }
            }

            return nullptr;
        }
        case StmtKind::BLOCK: {
            llvm::Value* last = nullptr;

            for (size_t i = 0; i < stmt->block.count; i++) {
                last = genStmt(stmt->block.stmts[i]);
            }

            return last;
        }
        case StmtKind::RETURN_STMT: {
            if (!stmt->return_stmt.value) {
                return builder->CreateRetVoid();
            }
            return builder->CreateRet(genExpr(stmt->return_stmt.value));
        }
        default:
            return nullptr;
    }
}
llvm::Value* Codegen::genExpr(Expr* expr) {
    switch (expr->kind) {
        case ExprKind::LITERAL_INT: {
            llvm::Type* ty =
                expr->resolved_type
                    ? typeConvertion(expr->resolved_type, &expr->span)
                    : i32ty;

            bool isFloatTarget = expr->resolved_type &&
                                 (expr->resolved_type->kind == TypeKind::F32 ||
                                  expr->resolved_type->kind == TypeKind::F64);

            if (isFloatTarget)
                return llvm::ConstantFP::get(
                    ty, static_cast<double>(expr->literal_int.value));

            bool isUnsigned = expr->resolved_type &&
                              (expr->resolved_type->kind == TypeKind::U8 ||
                               expr->resolved_type->kind == TypeKind::U16 ||
                               expr->resolved_type->kind == TypeKind::U32 ||
                               expr->resolved_type->kind == TypeKind::U64);

            return llvm::ConstantInt::get(ty, expr->literal_int.value,
                                          !isUnsigned);
        }
        case ExprKind::LITERAL_FLOAT: {
            llvm::Type* ty =
                expr->resolved_type
                    ? typeConvertion(expr->resolved_type, &expr->span)
                    : f32ty;
            return llvm::ConstantFP::get(ty, expr->literal_float.value);
        }
        case ExprKind::IDENTIFIER: {
            if (expr->identifier.is_global) {
                auto globalVar = globals[expr->identifier.name];
                llvm::Value* runtimeValue =
                    builder->CreateLoad(globalVar->getValueType(), globalVar,
                                        expr->identifier.name);

                return runtimeValue;
            } else {
                auto localVar = lookup(expr->identifier.name);
                if (!localVar) {
                    error.error(std::format("undeclared identifier '{}'",
                                            expr->identifier.name),
                                expr->span);
                    return nullptr;
                }
                llvm::Value* localValue = builder->CreateLoad(
                    typeConvertion(expr->resolved_type, &expr->span), localVar,
                    std::format("loaded_{}", expr->identifier.name));

                return localValue;
            }
        }
        case ExprKind::BINARY:
            return genBinop(expr, expr->binary.left, expr->binary.right,
                            expr->binary.op);
        default:
            return nullptr;
    }
}

llvm::Value* Codegen::genBinop(Expr* e, Expr* l, Expr* r, TokenKind op) {
    llvm::Value* lv = genExpr(l);
    llvm::Value* rv = genExpr(r);
    if (!lv || !rv)
        return nullptr;

    bool isFloat = l->resolved_type &&
                   (l->resolved_type->kind == TypeKind::F32 ||
                    l->resolved_type->kind == TypeKind::F64 ||
                    l->resolved_type->kind == TypeKind::FLOAT_CONSTANT);

    bool isUnsigned =
        l->resolved_type && (l->resolved_type->kind == TypeKind::U8 ||
                             l->resolved_type->kind == TypeKind::U16 ||
                             l->resolved_type->kind == TypeKind::U32 ||
                             l->resolved_type->kind == TypeKind::U64);

    switch (op) {
        case TokenKind::PLUS_TOKEN:
            return isFloat ? builder->CreateFAdd(lv, rv, "faddtmp")
                           : builder->CreateAdd(lv, rv, "addtmp");
        case TokenKind::MINUS_TOKEN:
            return isFloat ? builder->CreateFSub(lv, rv, "fsubtmp")
                           : builder->CreateSub(lv, rv, "subtmp");
        case TokenKind::MULTIPLY_TOKEN:
            return isFloat ? builder->CreateFMul(lv, rv, "fmultmp")
                           : builder->CreateMul(lv, rv, "multmp");
        case TokenKind::DIVIDE_TOKEN:
            if (isFloat)
                return builder->CreateFDiv(lv, rv, "fdivtmp");
            return isUnsigned ? builder->CreateUDiv(lv, rv, "udivtmp")
                              : builder->CreateSDiv(lv, rv, "sdivtmp");
        case TokenKind::MODULO_TOKEN:
            return isUnsigned ? builder->CreateURem(lv, rv, "uremtmp")
                              : builder->CreateSRem(lv, rv, "sremtmp");

        case TokenKind::EQUAL_EQUAL_TOKEN:
            return isFloat ? builder->CreateFCmpOEQ(lv, rv, "eqtmp")
                           : builder->CreateICmpEQ(lv, rv, "eqtmp");
        case TokenKind::NOT_EQUAL_TOKEN:
            return isFloat ? builder->CreateFCmpONE(lv, rv, "netmp")
                           : builder->CreateICmpNE(lv, rv, "netmp");
        case TokenKind::LESS_TOKEN:
            if (isFloat)
                return builder->CreateFCmpOLT(lv, rv, "lttmp");
            return isUnsigned ? builder->CreateICmpULT(lv, rv, "lttmp")
                              : builder->CreateICmpSLT(lv, rv, "lttmp");
        case TokenKind::GREATER_TOKEN:
            if (isFloat)
                return builder->CreateFCmpOGT(lv, rv, "gttmp");
            return isUnsigned ? builder->CreateICmpUGT(lv, rv, "gttmp")
                              : builder->CreateICmpSGT(lv, rv, "gttmp");
        case TokenKind::LESS_EQUAL_TOKEN:
            if (isFloat)
                return builder->CreateFCmpOLE(lv, rv, "letmp");
            return isUnsigned ? builder->CreateICmpULE(lv, rv, "letmp")
                              : builder->CreateICmpSLE(lv, rv, "letmp");
        case TokenKind::GREATER_EQUAL_TOKEN:
            if (isFloat)
                return builder->CreateFCmpOGE(lv, rv, "getmp");
            return isUnsigned ? builder->CreateICmpUGE(lv, rv, "getmp")
                              : builder->CreateICmpSGE(lv, rv, "getmp");

        case TokenKind::LOGICAL_AND_TOKEN:
            return builder->CreateAnd(lv, rv, "andtmp");
        case TokenKind::LOGICAL_OR_TOKEN:
            return builder->CreateOr(lv, rv, "ortmp");

        case TokenKind::AMPERSAND_TOKEN:
            return builder->CreateAnd(lv, rv, "bandtmp");
        case TokenKind::PIPE_TOKEN:
            return builder->CreateOr(lv, rv, "bortmp");
        case TokenKind::CARET_TOKEN:
            return builder->CreateXor(lv, rv, "xortmp");
        case TokenKind::LEFT_SHIFT_TOKEN:
            return builder->CreateShl(lv, rv, "shltmp");
        case TokenKind::RIGHT_SHIFT_TOKEN:
            return isUnsigned ? builder->CreateLShr(lv, rv, "shrtmp")
                              : builder->CreateAShr(lv, rv, "shrtmp");

        default:
            error.error("error making a binop", e->span);
            return nullptr;
    }
}

bool Codegen::generate(SourceFile* source) {
    if (!source)
        return false;

    // First creates forward-declarations
    for (size_t i = 0; i < source->decl_count; i++) {
        Stmt* decl = source->top_level_decls[i];
        genTopLevel(decl);
    }

    // Then it generates the bodies
    for (size_t i = 0; i < source->decl_count; i++) {
        Stmt* decl = source->top_level_decls[i];
        genStmt(decl);
    }

    bool broken = llvm::verifyModule(*module, &llvm::errs());
    if (broken) {
        error.error("module verification failed", Span{});
        return false;
    }

    module->print(llvm::outs(), nullptr);
    return true;
}

Codegen::~Codegen() {}

void Codegen::pushScope() {
    scopes.push_back({});
}

void Codegen::popScope() {
    if (!scopes.empty()) {
        scopes.pop_back();
    }
}

bool Codegen::declare(std::string_view& name, llvm::Value* val) {
    if (scopes.empty())
        return false;

    auto& currentScope = scopes.back();
    if (currentScope.find(name) != currentScope.end()) {
        return false;
    }

    currentScope[name] = val;
    return true;
}

llvm::Value* Codegen::lookup(std::string_view& name) {
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    return nullptr;
}

llvm::AllocaInst* Codegen::createEntryAlloca(llvm::Type* ty,
                                             llvm::Value* arraySize,
                                             std::string_view& name) {
    llvm::BasicBlock* curBlock = builder->GetInsertBlock();
    if (!curBlock) {
        error.error("createEntryAlloca called outside of function", Span{});
        return nullptr;
    }
    llvm::Function* func = curBlock->getParent();
    if (!func) {
        error.error("createEntryAlloca: no parent function", Span{});
        return nullptr;
    }

    llvm::IRBuilderBase::InsertPoint savedIP = builder->saveIP();
    builder->SetInsertPoint(&func->getEntryBlock(),
                            func->getEntryBlock().begin());

    llvm::AllocaInst* alloca = builder->CreateAlloca(ty, arraySize, name);

    builder->restoreIP(savedIP);
    return alloca;
}