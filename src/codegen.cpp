#include "llvm/IR/Verifier.h"

#include "codegen.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <cstddef>
#include <format>
#include <string>
#include <vector>
#include "ast.hpp"
#include "token.hpp"

Codegen::Codegen(std::string_view name, ErrorCollector& errors)
    : error(errors), name(name) {
    topLevelStructs.clear();
    scopes.clear();

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
        case TypeKind::CHAR:
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
        case TypeKind::NAMED: {
            auto it = topLevelStructs.find(type->named.name);
            if (it == topLevelStructs.end() || !it->second.llvmType) {
                error.error(std::format("unknown type '{}'", type->named.name),
                            *span);
                return voidty;
            }
            return it->second.llvmType;
        }

        case TypeKind::POINTER:
            return ptrTy;
        case TypeKind::ARRAY:
            return llvm::ArrayType::get(
                typeConvertion(type->array.element, &type->span),
                type->array.size);
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

            topLevelStructs[stmt->struct_union_decl.name] = {structTy};

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
        case StmtKind::STRUCT_DECL: {
            auto& structDecl = topLevelStructs[stmt->struct_union_decl.name];
            auto& structTy = structDecl.llvmType;

            if (!structTy) {
                structTy = llvm::StructType::create(
                    *context, stmt->struct_union_decl.name);
            }

            std::vector<llvm::Type*> types;
            types.reserve(stmt->struct_union_decl.field_count);

            for (size_t i = 0; i < stmt->struct_union_decl.field_count; i++) {
                Field& field = stmt->struct_union_decl.fields[i];
                types.push_back(typeConvertion(field.type, &field.span));
                structDecl.fieldIndices[field.name] = static_cast<unsigned>(i);
            }

            structTy->setBody(types);

            return nullptr;
        }
        case StmtKind::FUNC_DECL: {
            llvm::Function* prev = curFunc;

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
                curFunc = function;
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

            curFunc = prev;

            return function;
        }
        case StmtKind::CONST_DECL:
        case StmtKind::VAR_DECL: {
            auto name = stmt->var_decl.name;
            auto is_global = stmt->var_decl.is_global;
            auto is_mutable = stmt->var_decl.is_mutable;

            auto type = typeConvertion(stmt->var_decl.type, &stmt->span);

            if (is_global) {
                llvm::Value* init = genExpr(stmt->var_decl.init);
                llvm::Constant* C = nullptr;

                if (!init) {
                    C = llvm::Constant::getNullValue(type);
                } else if (auto* constInit = llvm::dyn_cast<llvm::Constant>(init)) {
                    C = constInit;
                } else {
                    error.error(
                        std::format("global {} needs a constant as initial value", name),
                        stmt->span);
                }

                if (C) {
                    llvm::GlobalVariable* globalVar = new llvm::GlobalVariable(
                        *module, type, !is_mutable,
                        llvm::GlobalValue::ExternalLinkage, C,
                        std::string(name));
                }
            } else {
                llvm::AllocaInst* alloca = createEntryAlloca(type, nullptr, name);
                if (!alloca) {
                    return nullptr;
                }

                if (!declare(name, alloca)) {
                    error.error(
                        std::format("redefinition of '{}' in this scope", name),
                        stmt->span);
                    return nullptr;
                }

                if (stmt->var_decl.init) {
                    llvm::Value* init = genExpr(stmt->var_decl.init);
                    if (init) {
                        builder->CreateStore(init, alloca);
                    }
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
        case StmtKind::IF_STMT: {
            llvm::BasicBlock* thenBlock =
                llvm::BasicBlock::Create(*context, "if.then", curFunc);
            llvm::BasicBlock* elseBlock =
                llvm::BasicBlock::Create(*context, "if.else", curFunc);
            llvm::BasicBlock* mergeBlock =
                llvm::BasicBlock::Create(*context, "if.end", curFunc);

            builder->CreateCondBr(genExpr(stmt->if_stmt.cond), thenBlock,
                                  elseBlock);

            builder->SetInsertPoint(thenBlock);
            genStmt(stmt->if_stmt.then_branch);
            if (!builder->GetInsertBlock()->getTerminator())
                builder->CreateBr(mergeBlock);

            builder->SetInsertPoint(elseBlock);
            if (stmt->if_stmt.else_branch) {
                genStmt(stmt->if_stmt.else_branch);
            }
            if (!builder->GetInsertBlock()->getTerminator())
                builder->CreateBr(mergeBlock);

            builder->SetInsertPoint(mergeBlock);

            return nullptr;
        }
        case StmtKind::WHILE_STMT: {
            llvm::BasicBlock* loopCondB =
                llvm::BasicBlock::Create(*context, "loop.cond", curFunc);
            llvm::BasicBlock* loopBodyB =
                llvm::BasicBlock::Create(*context, "loop.body", curFunc);
            llvm::BasicBlock* loopEndB =
                llvm::BasicBlock::Create(*context, "loop.end", curFunc);

            builder->SetInsertPoint(loopCondB);
            builder->CreateCondBr(genExpr(stmt->while_stmt.cond), loopBodyB,
                                  loopEndB);

            builder->SetInsertPoint(loopBodyB);

            genStmt(stmt->while_stmt.body);

            builder->CreateBr(loopCondB);

            builder->SetInsertPoint(loopEndB);

            return nullptr;
        }
        case StmtKind::EXPR_STMT: {
            return genExpr(stmt->expr_stmt.expr);
        }
        default:
            return nullptr;
    }
}
llvm::Value* Codegen::genExpr(Expr* expr) {
    if (!expr)
        return nullptr;

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
        case ExprKind::LITERAL_STRING:
            return builder->CreateGlobalString(expr->literal_string.value);
        case ExprKind::LITERAL_CHAR:
            return llvm::ConstantInt::get(i8ty, expr->literal_char.value);
        case ExprKind::LITERAL_BOOL:
            return llvm::ConstantInt::get(boolty, expr->literal_bool.value);
        case ExprKind::LITERAL_ARRAY: {
            auto ty = llvm::ArrayType::get(
                typeConvertion(expr->resolved_type->array.element,
                               &expr->resolved_type->span),
                expr->literal_array.value_count);

            llvm::Value* arrayPtr =
                builder->CreateAlloca(ty, nullptr, "array_tmp_literal");

            for (size_t i = 0; i < expr->literal_array.value_count; i++) {
                auto field = expr->literal_array.values[i];

                llvm::Value* zero = builder->getInt32(0);
                llvm::Value* idx = builder->getInt32(static_cast<uint32_t>(i));

                llvm::Value* elementPtr = builder->CreateInBoundsGEP(
                    ty, arrayPtr, {zero, idx}, "ptr_idx5");

                builder->CreateStore(genExpr(field), elementPtr);
            }

            return arrayPtr;
        }
        case ExprKind::IDENTIFIER: {
            if (expr->identifier.is_global) {
                auto function =
                    module->getFunction(std::string(expr->identifier.name));

                if (function) {
                    return function;
                }

                auto globalVar = module->getNamedGlobal(expr->identifier.name);
                llvm::Value* runtimeValue =
                    builder->CreateLoad(globalVar->getValueType(), globalVar,
                                        expr->identifier.name);

                return runtimeValue;
            } else {
                auto localVar = lookup(expr->identifier.name);
                if (localVar) {
                    llvm::Value* localValue = builder->CreateLoad(
                        typeConvertion(expr->resolved_type, &expr->span),
                        localVar,
                        std::format("loaded_{}", expr->identifier.name));

                    return localValue;
                } else {
                    auto function =
                        module->getFunction(std::string(expr->identifier.name));
                    if (function)
                        return function;
                    error.error(std::format("undeclared identifier '{}'",
                                            expr->identifier.name),
                                expr->span);
                    return nullptr;
                }
            }
        }
        case ExprKind::CALL: {
            llvm::Value* calleeVal = genExpr(expr->call.callee);
            auto* F = llvm::dyn_cast_or_null<llvm::Function>(calleeVal);

            if (!F) {
                error.error("cannot call a non-function value", expr->span);
                return nullptr;
            }

            std::vector<llvm::Value*> args;
            args.reserve(expr->call.arg_count);

            for (size_t i = 0; i < expr->call.arg_count; i++) {
                llvm::Value* argVal = genExpr(expr->call.args[i]);
                if (!argVal)
                    return nullptr;

                if (F->isVarArg() && i >= F->arg_size()) {
                    llvm::Type* argTy = argVal->getType();

                    if (argTy->isFloatTy()) {
                        argVal = builder->CreateFPExt(argVal, f64ty,
                                                      "vararg_promote");
                    } else if (argTy->isIntegerTy() &&
                               argTy->getIntegerBitWidth() < 32) {
                        argVal = builder->CreateSExt(argVal, i32ty,
                                                     "vararg_promote");
                    }
                }

                args.push_back(argVal);
            }

            bool isVoidCall = F->getReturnType()->isVoidTy();
            return builder->CreateCall(F, args, isVoidCall ? "" : "call_tmp");
        }
        case ExprKind::BINARY:
            return genBinop(expr, expr->binary.left, expr->binary.right,
                            expr->binary.op);
        case ExprKind::CAST:
            return genCast(expr);
        case ExprKind::SIZEOF:
            return genSizeof(expr);
        case ExprKind::PRE_INC_DEC:
            return genIncDec(expr->pre_inc_dec.operand, expr->pre_inc_dec.op,
                             /*isPrefix=*/true);
        case ExprKind::POST_INC_DEC:
            return genIncDec(expr->post_inc_dec.operand, expr->post_inc_dec.op,
                             /*isPrefix=*/false);
        case ExprKind::ASSIGN: {
            auto target = expr->assign.target;
            auto value = expr->assign.value;
            llvm::Value* assign = nullptr;

            switch (expr->assign.op) {
                case TokenKind::PLUS_ASSIGN_TOKEN:
                    assign =
                        genBinop(expr, target, value, TokenKind::PLUS_TOKEN);
                    break;
                case TokenKind::MINUS_ASSIGN_TOKEN:
                    assign =
                        genBinop(expr, target, value, TokenKind::MINUS_TOKEN);
                    break;
                case TokenKind::MULTIPLY_ASSIGN_TOKEN:
                    assign = genBinop(expr, target, value,
                                      TokenKind::MULTIPLY_TOKEN);
                    break;
                case TokenKind::DIVIDE_ASSIGN_TOKEN:
                    assign =
                        genBinop(expr, target, value, TokenKind::DIVIDE_TOKEN);
                    break;
                case TokenKind::MODULO_ASSIGN_TOKEN:
                    assign =
                        genBinop(expr, target, value, TokenKind::MODULO_TOKEN);
                    break;
                case TokenKind::AND_ASSIGN_TOKEN:
                    assign = genBinop(expr, target, value,
                                      TokenKind::AMPERSAND_TOKEN);
                    break;
                case TokenKind::OR_ASSIGN_TOKEN:
                    assign =
                        genBinop(expr, target, value, TokenKind::PIPE_TOKEN);
                    break;
                case TokenKind::XOR_ASSIGN_TOKEN:
                    assign =
                        genBinop(expr, target, value, TokenKind::CARET_TOKEN);
                    break;
                case TokenKind::LEFT_SHIFT_ASSIGN_TOKEN:
                    assign = genBinop(expr, target, value,
                                      TokenKind::LEFT_SHIFT_TOKEN);
                    break;
                case TokenKind::RIGHT_SHIFT_ASSIGN_TOKEN:
                    assign = genBinop(expr, target, value,
                                      TokenKind::RIGHT_SHIFT_ASSIGN_TOKEN);
                    break;
                default:
                    assign = genExpr(value);
                    break;
            }

            switch (target->kind) {
                case ExprKind::IDENTIFIER: {
                    auto id = target->identifier;

                    if (id.is_global) {
                        return builder->CreateStore(
                            assign, module->getGlobalVariable(id.name));
                    } else {
                        if (auto* v = llvm::dyn_cast<llvm::AllocaInst>(
                                lookup(id.name))) {
                            return builder->CreateStore(assign, v);
                        } else {
                            error.error(
                                std::format("local {} isn't a alloca (lol?)",
                                            name),
                                expr->span);
                            return nullptr;
                        }
                    }
                }
                case ExprKind::INDEX:
                case ExprKind::ARROW_MEMBER:
                case ExprKind::MEMBER: {
                    llvm::Value* addr = genAddr(target);
                    if (!addr)
                        return nullptr;
                    return builder->CreateStore(assign, addr);
                }
                case ExprKind::UNARY:
                    error.error("unhandled assign (deref)", expr->span);
                    return nullptr;
                default:
                    error.error(
                        "left-hand side of assignment must be a variable, "
                        "index, "
                        "member, "
                        "or dereference expression",
                        expr->span);
                    return nullptr;
            }
        }
        case ExprKind::MEMBER:
        case ExprKind::ARROW_MEMBER: {
            llvm::Value* addr = genAddr(expr);
            if (!addr)
                return nullptr;

            llvm::Type* fieldTy =
                typeConvertion(expr->resolved_type, &expr->span);
            return builder->CreateLoad(
                fieldTy, addr, std::format("{}_val", expr->member.field));
        }

        case ExprKind::INDEX: {
            llvm::Value* addr = genAddr(expr);
            if (!addr)
                return nullptr;

            llvm::Type* elemTy =
                typeConvertion(expr->resolved_type, &expr->span);
            return builder->CreateLoad(elemTy, addr, "idx_val");
        }

        case ExprKind::LITERAL_STRUCT: {
            llvm::Type* ty =
                typeConvertion(expr->literal_struct.target, &expr->span);
            auto* structTy = llvm::dyn_cast<llvm::StructType>(ty);
            if (!structTy) {
                error.error("literal isn't a struct type", expr->span);
                return nullptr;
            }

            auto it =
                topLevelStructs.find(expr->literal_struct.target->named.name);
            if (it == topLevelStructs.end()) {
                error.error("unknown struct type in literal", expr->span);
                return nullptr;
            }
            auto& structInfo = it->second;

            bool isGlobalCtx = builder->GetInsertBlock() == nullptr;

            if (isGlobalCtx) {
                std::vector<llvm::Constant*> fieldConsts(
                    structInfo.fieldIndices.size(), nullptr);

                for (size_t i = 0; i < expr->literal_struct.field_count; i++) {
                    Field& field = expr->literal_struct.fields[i];

                    auto idxIt = structInfo.fieldIndices.find(field.name);
                    if (idxIt == structInfo.fieldIndices.end()) {
                        error.error(
                            std::format("struct has no field '{}'", field.name),
                            field.span);
                        continue;
                    }

                    llvm::Value* fieldVal = genExpr(field.default_value);
                    auto* fieldConst =
                        llvm::dyn_cast_or_null<llvm::Constant>(fieldVal);
                    if (!fieldConst) {
                        error.error(
                            std::format(
                                "global struct field '{}' needs a constant",
                                field.name),
                            field.span);
                        continue;
                    }
                    fieldConsts[idxIt->second] = fieldConst;
                }

                for (unsigned i = 0; i < fieldConsts.size(); i++) {
                    if (!fieldConsts[i]) {
                        fieldConsts[i] = llvm::Constant::getNullValue(
                            structTy->getElementType(i));
                    }
                }

                return llvm::ConstantStruct::get(structTy, fieldConsts);
            }

            llvm::Value* structPtr =
                builder->CreateAlloca(structTy, nullptr, "struct_tmp_literal");

            for (size_t i = 0; i < expr->literal_struct.field_count; i++) {
                Field& field = expr->literal_struct.fields[i];

                auto idxIt = structInfo.fieldIndices.find(field.name);
                if (idxIt == structInfo.fieldIndices.end()) {
                    error.error(
                        std::format("struct has no field '{}'", field.name),
                        field.span);
                    continue;
                }

                llvm::Value* fieldPtr =
                    builder->CreateStructGEP(structTy, structPtr, idxIt->second,
                                             std::format("{}_ptr", field.name));

                llvm::Value* fieldVal = genExpr(field.default_value);
                if (fieldVal) {
                    builder->CreateStore(fieldVal, fieldPtr);
                }
            }

            return builder->CreateLoad(structTy, structPtr,
                                       "struct_literal_loaded");
        }
        default:
            return nullptr;
    }
}

llvm::Value* Codegen::genAddr(Expr* expr) {
    switch (expr->kind) {
        case ExprKind::IDENTIFIER: {
            if (expr->identifier.is_global) {
                llvm::GlobalVariable* gv =
                    module->getNamedGlobal(expr->identifier.name);
                if (!gv) {
                    error.error(std::format("undeclared global '{}'",
                                            expr->identifier.name),
                                expr->span);
                    return nullptr;
                }
                return gv;
            }

            llvm::Value* v = lookup(expr->identifier.name);
            if (!v) {
                error.error(std::format("undeclared identifier '{}'",
                                        expr->identifier.name),
                            expr->span);
                return nullptr;
            }
            return v;
        }

        case ExprKind::MEMBER: {
            Expr* object = expr->member.object;

            if (!object->resolved_type) {
                error.error(
                    "member access on an expression without a resolved type",
                    expr->span);
                return nullptr;
            }

            bool objIsPtr = object->resolved_type->kind == TypeKind::POINTER;
            Type* structType = objIsPtr ? object->resolved_type->wrapper.inner
                                        : object->resolved_type;

            if (!structType || structType->kind != TypeKind::NAMED) {
                error.error("member access on a non-struct value", expr->span);
                return nullptr;
            }

            auto it = topLevelStructs.find(structType->named.name);
            if (it == topLevelStructs.end()) {
                error.error(std::format("unknown struct type '{}'",
                                        structType->named.name),
                            expr->span);
                return nullptr;
            }
            auto& structInfo = it->second;

            auto idxIt = structInfo.fieldIndices.find(expr->member.field);
            if (idxIt == structInfo.fieldIndices.end()) {
                error.error(
                    std::format("struct has no field '{}'", expr->member.field),
                    expr->span);
                return nullptr;
            }

            llvm::Value* objAddr = objIsPtr ? genExpr(object) : genAddr(object);
            if (!objAddr)
                return nullptr;

            return builder->CreateStructGEP(
                structInfo.llvmType, objAddr, idxIt->second,
                std::format("{}_ptr", expr->member.field));
        }

        case ExprKind::ARROW_MEMBER: {
            Expr* object = expr->member.object;

            if (!object->resolved_type ||
                object->resolved_type->kind != TypeKind::POINTER ||
                object->resolved_type->wrapper.inner->kind != TypeKind::NAMED) {
                error.error("arrow access on a non-pointer-to-struct value",
                            expr->span);
                return nullptr;
            }

            Type* pointee = object->resolved_type->wrapper.inner;
            auto it = topLevelStructs.find(pointee->named.name);
            if (it == topLevelStructs.end()) {
                error.error(std::format("unknown struct type '{}'",
                                        pointee->named.name),
                            expr->span);
                return nullptr;
            }
            auto& structInfo = it->second;

            auto idxIt = structInfo.fieldIndices.find(expr->member.field);
            if (idxIt == structInfo.fieldIndices.end()) {
                error.error(
                    std::format("struct has no field '{}'", expr->member.field),
                    expr->span);
                return nullptr;
            }

            llvm::Value* objPtr = genExpr(object);
            if (!objPtr)
                return nullptr;

            return builder->CreateStructGEP(
                structInfo.llvmType, objPtr, idxIt->second,
                std::format("{}_ptr", expr->member.field));
        }

        case ExprKind::INDEX: {
            Expr* arrayExpr = expr->index.array;
            llvm::Value* idx = genExpr(expr->index.index);
            if (!idx)
                return nullptr;

            if (!arrayExpr->resolved_type) {
                error.error("indexing an expression without a resolved type",
                            expr->span);
                return nullptr;
            }

            if (arrayExpr->resolved_type->kind == TypeKind::ARRAY) {
                llvm::Value* arrAddr = genAddr(arrayExpr);
                if (!arrAddr)
                    return nullptr;

                llvm::Type* arrTy =
                    typeConvertion(arrayExpr->resolved_type, &arrayExpr->span);
                llvm::Value* zero = builder->getInt32(0);

                return builder->CreateInBoundsGEP(arrTy, arrAddr, {zero, idx},
                                                  "idx_ptr");
            }

            if (arrayExpr->resolved_type->kind == TypeKind::POINTER) {
                llvm::Value* ptrVal = genExpr(arrayExpr);
                if (!ptrVal)
                    return nullptr;

                llvm::Type* elemTy = typeConvertion(
                    arrayExpr->resolved_type->wrapper.inner, &arrayExpr->span);

                return builder->CreateInBoundsGEP(elemTy, ptrVal, {idx},
                                                  "idx_ptr");
            }

            error.error("cannot index a non-array, non-pointer value",
                        expr->span);
            return nullptr;
        }

        default:
            error.error("expression is not addressable", expr->span);
            return nullptr;
    }
}

namespace {
bool isFloatType(Type* t) {
    return t && (t->kind == TypeKind::F32 || t->kind == TypeKind::F64 ||
                 t->kind == TypeKind::FLOAT_CONSTANT);
}

bool isUnsignedType(Type* t) {
    return t && (t->kind == TypeKind::U8 || t->kind == TypeKind::U16 ||
                 t->kind == TypeKind::U32 || t->kind == TypeKind::U64);
}
}    // namespace

llvm::Value* Codegen::genIncDec(Expr* operand, TokenKind op, bool isPrefix) {
    llvm::Value* addr = genAddr(operand);
    if (!addr)
        return nullptr;

    llvm::Type* ty = typeConvertion(operand->resolved_type, &operand->span);
    llvm::Value* oldVal =
        builder->CreateLoad(ty, addr, "incdec_load");

    bool isFloat = isFloatType(operand->resolved_type);
    bool isIncrement = op == TokenKind::INCREMENT_TOKEN;

    llvm::Value* newVal;
    if (isFloat) {
        llvm::Value* one = llvm::ConstantFP::get(ty, 1.0);
        newVal = isIncrement ? builder->CreateFAdd(oldVal, one, "finctmp")
                              : builder->CreateFSub(oldVal, one, "fdectmp");
    } else {
        llvm::Value* one = llvm::ConstantInt::get(ty, 1);
        newVal = isIncrement ? builder->CreateAdd(oldVal, one, "inctmp")
                              : builder->CreateSub(oldVal, one, "dectmp");
    }

    builder->CreateStore(newVal, addr);

    return isPrefix ? newVal : oldVal;
}

llvm::Value* Codegen::genCast(Expr* expr) {
    llvm::Value* val = genExpr(expr->cast.operand);
    if (!val)
        return nullptr;

    Type* srcType = expr->cast.operand->resolved_type;
    Type* dstType = expr->cast.target_type;

    if (!srcType || !dstType) {
        error.error("cast requires resolved source and target types",
                    expr->span);
        return nullptr;
    }

    llvm::Type* dstTy = typeConvertion(dstType, &expr->span);
    llvm::Type* srcTy = val->getType();

    if (srcTy == dstTy)
        return val;

    bool srcIsPtr = srcType->kind == TypeKind::POINTER;
    bool dstIsPtr = dstType->kind == TypeKind::POINTER;
    bool srcIsFloat = isFloatType(srcType);
    bool dstIsFloat = isFloatType(dstType);
    bool srcIsBool = srcType->kind == TypeKind::BOOL;
    bool dstIsBool = dstType->kind == TypeKind::BOOL;

    if (srcIsPtr && dstIsPtr) {
        return builder->CreateBitCast(val, dstTy, "ptrcast");
    }
    if (srcIsPtr && dstTy->isIntegerTy()) {
        return builder->CreatePtrToInt(val, dstTy, "ptrtoint_cast");
    }
    if (dstIsPtr && srcTy->isIntegerTy()) {
        return builder->CreateIntToPtr(val, dstTy, "inttoptr_cast");
    }

    if (srcIsFloat && dstIsFloat) {
        if (srcTy->getScalarSizeInBits() > dstTy->getScalarSizeInBits())
            return builder->CreateFPTrunc(val, dstTy, "fptrunc_cast");
        return builder->CreateFPExt(val, dstTy, "fpext_cast");
    }

    if (srcIsFloat && dstIsBool) {
        return builder->CreateFCmpONE(val, llvm::ConstantFP::get(srcTy, 0.0),
                                      "float_to_bool");
    }

    if (srcIsFloat && dstTy->isIntegerTy()) {
        bool dstUnsigned = isUnsignedType(dstType);
        return dstUnsigned ? builder->CreateFPToUI(val, dstTy, "fptoui_cast")
                           : builder->CreateFPToSI(val, dstTy, "fptosi_cast");
    }

    if (srcTy->isIntegerTy() && dstIsFloat) {
        bool srcUnsigned = isUnsignedType(srcType) || srcIsBool;
        return srcUnsigned ? builder->CreateUIToFP(val, dstTy, "uitofp_cast")
                           : builder->CreateSIToFP(val, dstTy, "sitofp_cast");
    }

    if (srcTy->isIntegerTy() && dstIsBool) {
        return builder->CreateICmpNE(val, llvm::ConstantInt::get(srcTy, 0),
                                     "int_to_bool");
    }

    if (srcTy->isIntegerTy() && dstTy->isIntegerTy()) {
        unsigned srcBits = srcTy->getIntegerBitWidth();
        unsigned dstBits = dstTy->getIntegerBitWidth();

        if (srcBits == dstBits)
            return val;
        if (srcBits > dstBits)
            return builder->CreateTrunc(val, dstTy, "trunc_cast");

        bool srcUnsigned = isUnsignedType(srcType) || srcIsBool;
        return srcUnsigned ? builder->CreateZExt(val, dstTy, "zext_cast")
                           : builder->CreateSExt(val, dstTy, "sext_cast");
    }

    error.error("unsupported cast between these types", expr->span);
    return nullptr;
}

llvm::Value* Codegen::genSizeof(Expr* expr) {
    Type* targetType = expr->size_of.is_type
                           ? expr->size_of.type
                           : expr->size_of.expr->resolved_type;

    if (!targetType) {
        error.error("sizeof target has no resolved type", expr->span);
        return nullptr;
    }

    llvm::Type* llvmTy = typeConvertion(targetType, &expr->span);
    if (!llvmTy)
        return nullptr;

    uint64_t sizeInBytes = module->getDataLayout().getTypeAllocSize(llvmTy);

    llvm::Type* resultTy =
        expr->resolved_type ? typeConvertion(expr->resolved_type, &expr->span)
                            : i64ty;

    return llvm::ConstantInt::get(resultTy, sizeInBytes, /*isSigned=*/false);
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

    for (size_t i = 0; i < source->decl_count; i++) {
        Stmt* decl = source->top_level_decls[i];
        genTopLevel(decl);
    }

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
