#include "codegen.hpp"

Codegen::Codegen(std::string_view name, ErrorCollector& errors)
    : error(errors), name(name) {
    topLevelStructs.clear();

    context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>(name, *context);
    builder = std::make_unique<llvm::IRBuilder<>>(*context);
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
    switch (stmt->kind) {
        case StmtKind::UNION_DECL: {
        }
        case StmtKind::STRUCT_DECL: {
        }
        case StmtKind::FUNC_DECL: {
            llvm::Function* function =
                module->getFunction("my_forward_declared_function");

            if (!function) {
                function = declareFunction(stmt);
            }

            if (!function->isDeclaration()) {
                error.error("function was already declarated", stmt->span);
                return nullptr;
            }

            llvm::BasicBlock* entryBlock =
                llvm::BasicBlock::Create(*context, "entry", function);
            builder->SetInsertPoint(entryBlock);

            return function;
        }
        default:
            return nullptr;
    }
}
llvm::Value* Codegen::genExpr(Expr* expr) {}

Codegen::~Codegen() {}
