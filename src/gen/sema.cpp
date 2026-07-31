#include "gen/sema.hpp"
#include <unordered_set>
#include "ast/ast.hpp"

Sema::Sema(ArenaAllocator& arena, ErrorCollector& errors)
    : arena_(arena), errors_(errors), isGlobal(true) {}

void Sema::error(const std::string& message, const Span& span) {
    errors_.error(message, span);
}

Type* Sema::getPrimitive(TypeKind kind) {
    auto it = primitive_cache_.find(kind);
    if (it != primitive_cache_.end())
        return it->second;
    Type* t = Type::makePrimitive(arena_, kind, Span{});
    primitive_cache_[kind] = t;
    return t;
}

int Sema::bitWidth(TypeKind k) {
    switch (k) {
        case TypeKind::I8:
        case TypeKind::U8:
            return 8;
        case TypeKind::I16:
        case TypeKind::U16:
            return 16;
        case TypeKind::I32:
        case TypeKind::U32:
        case TypeKind::F32:
            return 32;
        case TypeKind::I64:
        case TypeKind::U64:
        case TypeKind::F64:
            return 64;
        default:
            return 0;
    }
}

Type* Sema::inferReturnType(Stmt* body, bool& hasReturn) {
    std::vector<Type*> returnTypes;
    bool hasVoidReturn = false;
    collectReturnTypes(body, returnTypes, hasVoidReturn);
    hasReturn = !returnTypes.empty() || hasVoidReturn;

    if (!hasReturn)
        return nullptr;

    if (hasVoidReturn && !returnTypes.empty()) {
        error(
            "function has both 'return' with a value and 'return' without a "
            "value",
            body->span);
        return nullptr;
    }

    if (returnTypes.empty())
        return nullptr;

    Type* common = returnTypes[0];
    for (size_t i = 1; i < returnTypes.size(); ++i) {
        Type* t = returnTypes[i];
        if (typesEqual(common, t))
            continue;
        if (isNumericType(common) && isNumericType(t)) {
            bool ok;
            common = promoteNumeric(common, t, ok);
            if (!ok) {
                error("incompatible return types: '" + typeToString(common) +
                          "' and '" + typeToString(t) + "'",
                      body->span);
                return nullptr;
            }
        } else {
            error("incompatible return types: '" + typeToString(common) +
                      "' and '" + typeToString(t) + "'",
                  body->span);
            return nullptr;
        }
    }
    return common;
}

void Sema::collectReturnTypes(Stmt* s, std::vector<Type*>& types,
                              bool& hasVoid) {
    if (!s)
        return;

    switch (s->kind) {
        case StmtKind::RETURN_STMT: {
            if (s->return_stmt.value) {
                Type* t = checkExpr(s->return_stmt.value);
                if (t)
                    types.push_back(t);
            } else {
                hasVoid = true;
            }
            break;
        }
        case StmtKind::BLOCK:
            for (uint32_t i = 0; i < s->block.count; ++i)
                collectReturnTypes(s->block.stmts[i], types, hasVoid);
            break;
        case StmtKind::IF_STMT:
            collectReturnTypes(s->if_stmt.then_branch, types, hasVoid);
            if (s->if_stmt.else_branch)
                collectReturnTypes(s->if_stmt.else_branch, types, hasVoid);
            break;
        case StmtKind::WHILE_STMT:
        case StmtKind::DO_WHILE_STMT:
        case StmtKind::FOR_STMT:
            collectReturnTypes(s->while_stmt.body, types, hasVoid);
            break;
        case StmtKind::SWITCH_STMT:
        case StmtKind::MATCH_STMT:
            for (uint32_t i = 0; i < s->switch_stmt.case_count; ++i) {
                CaseClause& cc = s->switch_stmt.cases[i];
                for (uint32_t j = 0; j < cc.body_count; ++j)
                    collectReturnTypes(cc.body[j], types, hasVoid);
            }
            break;
        default:
            break;
    }
}

bool Sema::isIntegerType(Type* t) {
    if (!t)
        return false;
    switch (t->kind) {
        case TypeKind::INT_CONSTANT:
        case TypeKind::I8:
        case TypeKind::I16:
        case TypeKind::I32:
        case TypeKind::I64:
        case TypeKind::U8:
        case TypeKind::U16:
        case TypeKind::U32:
        case TypeKind::U64:
            return true;
        default:
            return false;
    }
}

bool Sema::isFloatType(Type* t) {
    return t && (t->kind == TypeKind::FLOAT_CONSTANT ||
                 t->kind == TypeKind::F32 || t->kind == TypeKind::F64);
}

bool Sema::isNumericType(Type* t) {
    return isIntegerType(t) || isFloatType(t);
}

bool Sema::isUnsignedType(Type* t) {
    if (!t)
        return false;
    switch (t->kind) {
        case TypeKind::INT_CONSTANT:
        case TypeKind::U8:
        case TypeKind::U16:
        case TypeKind::U32:
        case TypeKind::U64:
            return true;
        default:
            return false;
    }
}

bool Sema::isBoolType(Type* t) {
    return t && t->kind == TypeKind::BOOL;
}

bool Sema::isScalarType(Type* t) {
    return isNumericType(t) ||
           (t && (t->kind == TypeKind::CHAR || t->kind == TypeKind::BOOL));
}

bool Sema::isConstantType(Type* t) {
    return t && (t->kind == TypeKind::INT_CONSTANT ||
                 t->kind == TypeKind::FLOAT_CONSTANT);
}

bool Sema::literalIntFits(int64_t value, TypeKind kind) {
    switch (kind) {
        case TypeKind::I8:
            return value >= -128 && value <= 127;
        case TypeKind::I16:
            return value >= -32768 && value <= 32767;
        case TypeKind::I32:
            return value >= INT32_MIN && value <= INT32_MAX;
        case TypeKind::I64:
            return true;
        case TypeKind::U8:
            return value >= 0 && value <= 255;
        case TypeKind::U16:
            return value >= 0 && value <= 65535;
        case TypeKind::U32:
            return value >= 0 && value <= UINT32_MAX;
        case TypeKind::U64:
            return value >= 0;
        default:
            return true;
    }
}

void Sema::coerceToType(Expr* e, Type* target) {
    if (!e || !target || !e->resolved_type)
        return;

    if (target->kind == TypeKind::OPTIONAL)
        target = target->wrapper.inner;
    if (!target)
        return;

    if (!isConstantType(e->resolved_type)) {
        switch (e->kind) {
            case ExprKind::TERNARY:
                coerceToType(e->ternary.then_branch, target);
                coerceToType(e->ternary.else_branch, target);
                break;
            case ExprKind::LITERAL_ARRAY:
                if (target->kind == TypeKind::ARRAY ||
                    target->kind == TypeKind::SLICE) {
                    Type* elemTarget = target->kind == TypeKind::ARRAY
                                           ? target->array.element
                                           : target->slice.element;
                    for (uint32_t i = 0; i < e->literal_array.value_count; i++)
                        coerceToType(e->literal_array.values[i], elemTarget);
                }
                break;
            default:
                break;
        }
        return;
    }

    bool wasInt = e->resolved_type->kind == TypeKind::INT_CONSTANT;
    if (wasInt) {
        if (!isIntegerType(target) && !isFloatType(target))
            return;
    } else {
        if (!isFloatType(target))
            return;
    }

    switch (e->kind) {
        case ExprKind::LITERAL_INT:
            if (isIntegerType(target) &&
                !literalIntFits(e->literal_int.value, target->kind))
                error(
                    "integer literal '" + std::to_string(e->literal_int.value) +
                        "' does not fit in type '" + typeToString(target) + "'",
                    e->span);
            e->resolved_type = target;
            break;

        case ExprKind::LITERAL_FLOAT:
            e->resolved_type = target;
            break;

        case ExprKind::UNARY:
            coerceToType(e->unary.operand, target);
            e->resolved_type = target;
            break;

        case ExprKind::BINARY:
            coerceToType(e->binary.left, target);
            coerceToType(e->binary.right, target);
            e->resolved_type = target;
            break;

        case ExprKind::TERNARY:
            coerceToType(e->ternary.then_branch, target);
            coerceToType(e->ternary.else_branch, target);
            e->resolved_type = target;
            break;

        default:
            e->resolved_type = target;
            break;
    }
}

std::string Sema::opName(TokenKind k) {
    switch (k) {
        case TokenKind::PLUS_TOKEN:
            return "+";
        case TokenKind::MINUS_TOKEN:
            return "-";
        case TokenKind::MULTIPLY_TOKEN:
            return "*";
        case TokenKind::DIVIDE_TOKEN:
            return "/";
        case TokenKind::MODULO_TOKEN:
            return "%";
        case TokenKind::EQUAL_EQUAL_TOKEN:
            return "==";
        case TokenKind::NOT_EQUAL_TOKEN:
            return "!=";
        case TokenKind::LESS_TOKEN:
            return "<";
        case TokenKind::GREATER_TOKEN:
            return ">";
        case TokenKind::LESS_EQUAL_TOKEN:
            return "<=";
        case TokenKind::GREATER_EQUAL_TOKEN:
            return ">=";
        case TokenKind::LOGICAL_AND_TOKEN:
            return "&&";
        case TokenKind::LOGICAL_OR_TOKEN:
            return "||";
        case TokenKind::LOGICAL_NOT_TOKEN:
            return "!";
        case TokenKind::AMPERSAND_TOKEN:
            return "&";
        case TokenKind::PIPE_TOKEN:
            return "|";
        case TokenKind::CARET_TOKEN:
            return "^";
        case TokenKind::TILDE_TOKEN:
            return "~";
        case TokenKind::LEFT_SHIFT_TOKEN:
            return "<<";
        case TokenKind::RIGHT_SHIFT_TOKEN:
            return ">>";
        case TokenKind::ASSIGN_TOKEN:
            return "=";
        case TokenKind::PLUS_ASSIGN_TOKEN:
            return "+=";
        case TokenKind::MINUS_ASSIGN_TOKEN:
            return "-=";
        case TokenKind::MULTIPLY_ASSIGN_TOKEN:
            return "*=";
        case TokenKind::DIVIDE_ASSIGN_TOKEN:
            return "/=";
        case TokenKind::MODULO_ASSIGN_TOKEN:
            return "%=";
        case TokenKind::AND_ASSIGN_TOKEN:
            return "&=";
        case TokenKind::OR_ASSIGN_TOKEN:
            return "|=";
        case TokenKind::XOR_ASSIGN_TOKEN:
            return "^=";
        case TokenKind::LEFT_SHIFT_ASSIGN_TOKEN:
            return "<<=";
        case TokenKind::RIGHT_SHIFT_ASSIGN_TOKEN:
            return ">>=";
        case TokenKind::INCREMENT_TOKEN:
            return "++";
        case TokenKind::DECREMENT_TOKEN:
            return "--";
        default:
            return "?op?";
    }
}

std::string Sema::typeToString(Type* t) {
    if (!t)
        return "<unknown>";
    switch (t->kind) {
        case TypeKind::INT_CONSTANT:
            return "int constant";
        case TypeKind::FLOAT_CONSTANT:
            return "float constant";
        case TypeKind::I8:
            return "i8";
        case TypeKind::I16:
            return "i16";
        case TypeKind::I32:
            return "i32";
        case TypeKind::I64:
            return "i64";
        case TypeKind::U8:
            return "u8";
        case TypeKind::U16:
            return "u16";
        case TypeKind::U32:
            return "u32";
        case TypeKind::U64:
            return "u64";
        case TypeKind::F32:
            return "f32";
        case TypeKind::F64:
            return "f64";
        case TypeKind::BOOL:
            return "bool";
        case TypeKind::CHAR:
            return "char";
        case TypeKind::VOID:
            return "void";
        case TypeKind::NAMED:
            return std::string(t->named.name);
        case TypeKind::POINTER:
            return typeToString(t->wrapper.inner) + "*";
        case TypeKind::OPTIONAL:
            return typeToString(t->wrapper.inner) + "?";
        case TypeKind::VARIADIC:
            return typeToString(t->wrapper.inner) + "[...]";
        case TypeKind::ARRAY:
            return typeToString(t->array.element) + "[" +
                   std::to_string(t->array.size) + "]";
        case TypeKind::SLICE:
            return typeToString(t->slice.element) + "[]";
        case TypeKind::FUNC: {
            std::string s = "func(";
            for (uint32_t i = 0; i < t->func.param_count; i++) {
                if (i > 0)
                    s += ", ";
                s += typeToString(t->func.params[i]);
            }
            s += ") -> " + typeToString(t->func.return_type);
            return s;
        }
        default:
            return "<invalid>";
    }
}

Type* Sema::resolveType(Type* t) {
    if (!t)
        return nullptr;

    switch (t->kind) {
        case TypeKind::NAMED: {
            std::string name(t->named.name);
            if (types_.find(name) == types_.end()) {
                error("unknown type '" + name + "'", t->span);
                return nullptr;
            }
            return t;
        }
        case TypeKind::POINTER:
        case TypeKind::OPTIONAL:
        case TypeKind::VARIADIC:
            resolveType(t->wrapper.inner);
            return t;
        case TypeKind::ARRAY:
            resolveType(t->array.element);
            return t;
        case TypeKind::SLICE:
            resolveType(t->slice.element);
            return t;
        case TypeKind::FUNC:
            for (uint32_t i = 0; i < t->func.param_count; i++)
                resolveType(t->func.params[i]);
            resolveType(t->func.return_type);
            return t;
        case TypeKind::INVALID:
            return nullptr;
        default:
            return t;
    }
}

bool Sema::typesEqual(Type* a, Type* b) {
    if (a->kind == TypeKind::INT_CONSTANT) {
        switch (b->kind) {
            case TypeKind::INT_CONSTANT:

            case TypeKind::I8:
            case TypeKind::I16:
            case TypeKind::I32:
            case TypeKind::I64:

            case TypeKind::U8:
            case TypeKind::U16:
            case TypeKind::U32:
            case TypeKind::U64:
                return true;
            default:
                return false;
        }
    }

    if (b->kind == TypeKind::INT_CONSTANT) {
        switch (a->kind) {
            case TypeKind::INT_CONSTANT:

            case TypeKind::I8:
            case TypeKind::I16:
            case TypeKind::I32:
            case TypeKind::I64:

            case TypeKind::U8:
            case TypeKind::U16:
            case TypeKind::U32:
            case TypeKind::U64:
                return true;
            default:
                return false;
        }
    }

    if (a->kind == TypeKind::FLOAT_CONSTANT) {
        switch (b->kind) {
            case TypeKind::INT_CONSTANT:
            case TypeKind::FLOAT_CONSTANT:

            case TypeKind::F32:
            case TypeKind::F64:
                return true;
            default:
                return false;
        }
    }

    if (b->kind == TypeKind::FLOAT_CONSTANT) {
        switch (a->kind) {
            case TypeKind::INT_CONSTANT:
            case TypeKind::FLOAT_CONSTANT:

            case TypeKind::F32:
            case TypeKind::F64:
                return true;
            default:
                return false;
        }
    }

    if (a == b)
        return true;
    if (!a || !b)
        return false;
    if (a->kind != b->kind)
        return false;

    switch (a->kind) {
        case TypeKind::NAMED:
            return a->named.name == b->named.name;
        case TypeKind::POINTER:
        case TypeKind::OPTIONAL:
        case TypeKind::VARIADIC:
            return typesEqual(a->wrapper.inner, b->wrapper.inner);
        case TypeKind::ARRAY:
            return a->array.size == b->array.size &&
                   typesEqual(a->array.element, b->array.element);
        case TypeKind::SLICE:
            return typesEqual(a->slice.element, b->slice.element);
        case TypeKind::FUNC:
            if (a->func.param_count != b->func.param_count)
                return false;
            for (uint32_t i = 0; i < a->func.param_count; i++)
                if (!typesEqual(a->func.params[i], b->func.params[i]))
                    return false;
            return typesEqual(a->func.return_type, b->func.return_type);
        default:
            return true;
    }
}

Type* Sema::promoteNumeric(Type* a, Type* b, bool& ok) {
    ok = true;
    if (typesEqual(a, b))
        return a;

    bool af = isFloatType(a);
    bool bf = isFloatType(b);

    if (af || bf) {
        if (af && bf)
            return bitWidth(a->kind) >= bitWidth(b->kind) ? a : b;
        return af ? a : b;
    }

    bool au = isUnsignedType(a);
    bool bu = isUnsignedType(b);
    if (au != bu) {
        ok = false;
        return a;
    }

    return bitWidth(a->kind) >= bitWidth(b->kind) ? a : b;
}

bool Sema::isAssignable(Type* from, Type* to) {
    if (!from || !to)
        return true;
    if (typesEqual(from, to))
        return true;

    if (isNumericType(from) && isNumericType(to)) {
        bool ok;
        Type* common = promoteNumeric(from, to, ok);
        return ok && typesEqual(common, to);
    }

    if (from->kind == TypeKind::POINTER && to->kind == TypeKind::POINTER) {
        if (typesEqual(from->wrapper.inner, to->wrapper.inner))
            return true;
        if (from->wrapper.inner && from->wrapper.inner->kind == TypeKind::VOID)
            return true;
        if (to->wrapper.inner && to->wrapper.inner->kind == TypeKind::VOID)
            return true;
        return false;
    }

    if (from->kind == TypeKind::ARRAY && to->kind == TypeKind::POINTER) {
        if (to->wrapper.inner && to->wrapper.inner->kind == TypeKind::VOID)
            return true;
        return typesEqual(from->array.element, to->wrapper.inner);
    }

    if (to->kind == TypeKind::OPTIONAL)
        return isAssignable(from, to->wrapper.inner) ||
               (from->kind == TypeKind::OPTIONAL &&
                isAssignable(from->wrapper.inner, to->wrapper.inner));

    if (from->kind == TypeKind::ARRAY && to->kind == TypeKind::ARRAY)
        return from->array.size <= to->array.size &&
               isAssignable(from->array.element, to->array.element);

    if (from->kind == TypeKind::ARRAY && to->kind == TypeKind::SLICE)
        return isAssignable(from->array.element, to->slice.element);

    return false;
}

bool Sema::isValidCast(Type* from, Type* to) {
    if (!from || !to)
        return true;
    if (typesEqual(from, to))
        return true;
    if (isScalarType(from) && isScalarType(to))
        return true;
    if (from->kind == TypeKind::POINTER && to->kind == TypeKind::POINTER)
        return true;
    if (from->kind == TypeKind::POINTER && isIntegerType(to))
        return true;
    if (isIntegerType(from) && to->kind == TypeKind::POINTER)
        return true;
    return false;
}

bool Sema::isLValue(Expr* e) {
    if (!e)
        return false;
    switch (e->kind) {
        case ExprKind::IDENTIFIER:
        case ExprKind::INDEX:
        case ExprKind::ARROW_MEMBER:
        case ExprKind::MEMBER:
            return true;
        case ExprKind::UNARY:
            return e->unary.op == TokenKind::MULTIPLY_TOKEN;
        default:
            return false;
    }
}

bool Sema::checkAssignableTarget(Expr* target) {
    if (!isLValue(target)) {
        error(
            "left-hand side of assignment must be a variable, index, member, "
            "or dereference expression",
            target->span);
        return false;
    }

    if (target->kind == ExprKind::IDENTIFIER) {
        Symbol* sym = symbols_.lookup(std::string(target->identifier.name));
        if (sym) {
            if (sym->kind == SymbolKind::FUNC) {
                error("cannot assign to function '" +
                          std::string(target->identifier.name) + "'",
                      target->span);
                return false;
            }
            if (!sym->is_mutable) {
                error("cannot assign to immutable variable '" +
                          std::string(target->identifier.name) +
                          "'; declare it with 'var' instead of 'const' if it "
                          "needs to change",
                      target->span);
                return false;
            }
        }
    }

    return true;
}

// IM SCARED
void Sema::analyze(SourceFile* file) {
    isGlobal = true;
    collectTypes(file);

    isGlobal = true;
    validateTypeBodies(file);

    isGlobal = true;
    collectSignatures(file);

    isGlobal = true;
    checkTopLevelBodies(file);
}

void Sema::collectTypes(SourceFile* file) {
    for (uint32_t i = 0; i < file->decl_count; i++) {
        Stmt* d = file->top_level_decls[i];
        if (!d)
            continue;

        if (d->kind == StmtKind::STRUCT_DECL ||
            d->kind == StmtKind::UNION_DECL || d->kind == StmtKind::ENUM_DECL) {
            std::string name = d->kind == StmtKind::ENUM_DECL
                                   ? std::string(d->enum_decl.name)
                                   : std::string(d->struct_union_decl.name);

            if (types_.find(name) != types_.end())
                error("redefinition of type '" + name + "'", d->span);
            else
                types_[name] = d;
        }
    }
}

void Sema::validateTypeBodies(SourceFile* file) {
    for (uint32_t i = 0; i < file->decl_count; i++) {
        Stmt* d = file->top_level_decls[i];
        if (!d)
            continue;

        if (d->kind == StmtKind::STRUCT_DECL ||
            d->kind == StmtKind::UNION_DECL) {
            std::string ownerName(d->struct_union_decl.name);
            const char* kindWord =
                d->kind == StmtKind::STRUCT_DECL ? "struct" : "union";
            std::unordered_set<std::string> seen;

            for (uint32_t f = 0; f < d->struct_union_decl.field_count; f++) {
                Field& field = d->struct_union_decl.fields[f];
                std::string fieldName(field.name);

                if (seen.count(fieldName))
                    error("duplicate field '" + fieldName + "' in " + kindWord +
                              " '" + ownerName + "'",
                          field.span);
                seen.insert(fieldName);

                resolveType(field.type);

                if (field.type && field.type->kind == TypeKind::NAMED &&
                    field.type->named.name == ownerName)
                    error(kindWord + std::string(" '") + ownerName +
                              "' contains itself as field '" + fieldName +
                              "' without indirection; use a pointer, optional, "
                              "or slice instead",
                          field.span);

                if (field.default_value) {
                    Type* defType = checkExpr(field.default_value);
                    if (field.type && defType &&
                        !isAssignable(defType, field.type))
                        error("default value for field '" + fieldName +
                                  "' has type '" + typeToString(defType) +
                                  "' but expected '" +
                                  typeToString(field.type) + "'",
                              field.default_value->span);
                    else if (field.type && defType)
                        coerceToType(field.default_value, field.type);
                }
            }
        } else if (d->kind == StmtKind::ENUM_DECL) {
            std::string ownerName(d->enum_decl.name);
            std::unordered_set<std::string> seen;

            for (uint32_t v = 0; v < d->enum_decl.variant_count; v++) {
                std::string variantName(d->enum_decl.variant_names[v]);

                if (seen.count(variantName))
                    error("duplicate enum variant '" + variantName +
                              "' in enum '" + ownerName + "'",
                          d->span);
                seen.insert(variantName);

                if (d->enum_decl.variant_values[v]) {
                    Type* valType = checkExpr(d->enum_decl.variant_values[v]);
                    if (valType && !isIntegerType(valType))
                        error("enum variant '" + variantName +
                                  "' must have an integer value, got '" +
                                  typeToString(valType) + "'",
                              d->enum_decl.variant_values[v]->span);
                }
            }
        }
    }
}

void Sema::collectSignatures(SourceFile* file) {
    for (uint32_t i = 0; i < file->decl_count; i++) {
        Stmt* d = file->top_level_decls[i];
        if (!d)
            continue;

        if (d->kind == StmtKind::FUNC_DECL) {
            std::string name(d->func_decl.name);

            if (symbols_.lookup_local(name)) {
                error("redefinition of function '" + name + "'", d->span);
                continue;
            }

            std::unordered_set<std::string> paramNames;
            for (uint32_t p = 0; p < d->func_decl.param_count; p++) {
                Field& param = d->func_decl.params[p];
                std::string pname(param.name);
                if (paramNames.count(pname))
                    error("duplicate parameter name '" + pname +
                              "' in function '" + name + "'",
                          param.span);
                paramNames.insert(pname);
                resolveType(param.type);
            }

            Type* retType = d->func_decl.return_type
                                ? resolveType(d->func_decl.return_type)
                                : nullptr;

            Symbol sym;
            sym.name = name;
            sym.kind = SymbolKind::FUNC;
            sym.type = retType;
            sym.is_global = isGlobal;
            sym.is_mutable = false;
            sym.is_extern = d->func_decl.is_extern;
            sym.span = d->span;
            sym.decl = d;
            symbols_.declare(name, sym);
        } else if (d->kind == StmtKind::CONST_DECL ||
                   d->kind == StmtKind::VAR_DECL) {
            std::string name(d->var_decl.name);

            if (symbols_.lookup_local(name)) {
                error("redefinition of '" + name + "'", d->span);
                continue;
            }

            Type* declaredType =
                d->var_decl.type ? resolveType(d->var_decl.type) : nullptr;
            Type* initType =
                d->var_decl.init ? checkExpr(d->var_decl.init) : nullptr;

            if (!d->var_decl.init && d->kind == StmtKind::CONST_DECL)
                error("constant '" + name + "' must be initialized", d->span);

            Type* finalType = declaredType;

            if (!finalType) {
                if (initType) {
                    if (initType->kind == TypeKind::INT_CONSTANT)
                        finalType = getPrimitive(TypeKind::I32);
                    else if (initType->kind == TypeKind::FLOAT_CONSTANT)
                        finalType = getPrimitive(TypeKind::F32);
                    else
                        finalType = initType;
                } else if (d->kind == StmtKind::VAR_DECL) {
                    error("cannot infer type of '" + name +
                              "' without a type annotation or an initializer",
                          d->span);
                }
            }

            d->var_decl.is_global = isGlobal;
            d->var_decl.type = finalType;

            Symbol sym;
            sym.name = name;
            sym.kind = SymbolKind::VAR;
            sym.type = finalType;
            sym.is_global = isGlobal;
            sym.is_mutable = d->kind == StmtKind::VAR_DECL;
            sym.is_extern = false;
            sym.span = d->span;
            sym.decl = d;
            symbols_.declare(name, sym);
        }
    }
}

void Sema::checkTopLevelBodies(SourceFile* file) {
    for (uint32_t i = 0; i < file->decl_count; i++) {
        isGlobal = true;
        Stmt* d = file->top_level_decls[i];
        if (!d)
            continue;

        if (d->kind == StmtKind::FUNC_DECL)
            checkFuncBody(d);
        else if (d->kind == StmtKind::CONST_DECL ||
                 d->kind == StmtKind::VAR_DECL)
            checkGlobalVarInit(d);
    }
}

void Sema::checkGlobalVarInit(Stmt* d) {
    std::string name(d->var_decl.name);

    if (!d->var_decl.init) {
        if (d->kind == StmtKind::CONST_DECL)
            error("constant '" + name + "' must be initialized", d->span);
        return;
    }

    Type* initType = checkExpr(d->var_decl.init);
    Symbol* sym = symbols_.lookup_local(name);
    if (!sym)
        return;

    if (sym->type) {
        if (initType && !isAssignable(initType, sym->type))
            error("cannot initialize '" + name + "' of type '" +
                      typeToString(sym->type) + "' with a value of type '" +
                      typeToString(initType) + "'",
                  d->var_decl.init->span);
        else
            coerceToType(d->var_decl.init, sym->type);
    } else {
        sym->type = initType;
    }
}

void Sema::checkFuncBody(Stmt* d) {
    std::string name(d->func_decl.name);
    Symbol* sym = symbols_.lookup_local(name);
    Type* retType = sym ? sym->type : nullptr;

    if (!d->func_decl.body)
        return;

    symbols_.push();

    for (uint32_t p = 0; p < d->func_decl.param_count; p++) {
        Field& param = d->func_decl.params[p];
        std::string pname(param.name);

        Symbol sym;
        sym.name = pname;
        sym.kind = SymbolKind::VAR;
        sym.type = param.type;
        sym.is_global = false;
        sym.is_mutable = true;
        sym.span = param.span;
        sym.decl = nullptr;

        symbols_.declare(pname, sym);
    }

    std::string prevFn = current_function_name_;
    Type* prevRet = current_return_type_;
    current_function_name_ = name;

    bool prevGlobal = isGlobal;
    isGlobal = false;

    bool bodyAlreadyChecked = false;

    if (!d->func_decl.has_explicit_return_type && retType == nullptr) {
        inferring_return_ = true;
        inferred_return_types_.clear();
        inferred_return_stmts_.clear();
        inferred_has_void_ = false;

        checkStmt(d->func_decl.body);
        bodyAlreadyChecked = true;

        inferring_return_ = false;

        if (inferred_has_void_ && !inferred_return_types_.empty())
            error(
                "function has both 'return' with a value and 'return' without "
                "a value",
                d->func_decl.body->span);

        Type* inferred = nullptr;
        if (!inferred_return_types_.empty()) {
            inferred = inferred_return_types_[0];
            for (size_t i = 1; i < inferred_return_types_.size(); ++i) {
                Type* t = inferred_return_types_[i];
                if (typesEqual(inferred, t))
                    continue;
                if (isNumericType(inferred) && isNumericType(t)) {
                    bool ok;
                    inferred = promoteNumeric(inferred, t, ok);
                    if (!ok) {
                        error("incompatible return types: '" +
                                  typeToString(inferred) + "' and '" +
                                  typeToString(t) + "'",
                              d->func_decl.body->span);
                        inferred = nullptr;
                        break;
                    }
                } else {
                    error("incompatible return types: '" +
                              typeToString(inferred) + "' and '" +
                              typeToString(t) + "'",
                          d->func_decl.body->span);
                    inferred = nullptr;
                    break;
                }
            }
        }

        if (inferred) {
            if (inferred->kind == TypeKind::INT_CONSTANT)
                inferred = getPrimitive(TypeKind::I32);
            else if (inferred->kind == TypeKind::FLOAT_CONSTANT)
                inferred = getPrimitive(TypeKind::F32);
        }

        d->func_decl.return_type = inferred;
        sym->type = inferred;
        retType = inferred;
        d->func_decl.has_explicit_return_type = true;

        if (retType) {
            for (Stmt* rs : inferred_return_stmts_) {
                Expr* val = rs->return_stmt.value;
                Type* vt = val->resolved_type;
                if (vt && !isAssignable(vt, retType))
                    error("cannot return a value of type '" + typeToString(vt) +
                              "' from function '" + name + "' expecting '" +
                              typeToString(retType) + "'",
                          val->span);
                else if (vt)
                    coerceToType(val, retType);
            }
        }
    }

    current_return_type_ = retType;

    if (!bodyAlreadyChecked)
        checkStmt(d->func_decl.body);

    if (retType && retType->kind != TypeKind::VOID &&
        !stmtAlwaysReturns(d->func_decl.body)) {
        error("function '" + name + "' does not return a value of type '" +
                  typeToString(retType) + "' on all code paths",
              d->span);
    }

    current_function_name_ = prevFn;
    current_return_type_ = prevRet;
    isGlobal = prevGlobal;

    symbols_.pop();
}

bool Sema::stmtAlwaysReturns(Stmt* s) {
    if (!s)
        return false;

    switch (s->kind) {
        case StmtKind::RETURN_STMT:
            return true;

        case StmtKind::BLOCK:
            for (uint32_t i = 0; i < s->block.count; i++)
                if (stmtAlwaysReturns(s->block.stmts[i]))
                    return true;
            return false;

        case StmtKind::IF_STMT:
            return s->if_stmt.else_branch &&
                   stmtAlwaysReturns(s->if_stmt.then_branch) &&
                   stmtAlwaysReturns(s->if_stmt.else_branch);

        case StmtKind::WHILE_STMT: {
            Expr* cond = s->while_stmt.cond;
            return cond && cond->kind == ExprKind::LITERAL_BOOL &&
                   cond->literal_bool.value;
        }

        case StmtKind::DO_WHILE_STMT:
            return stmtAlwaysReturns(s->while_stmt.body);

        case StmtKind::SWITCH_STMT:
        case StmtKind::MATCH_STMT: {
            bool hasDefault = false;
            bool allCasesReturn = true;

            for (uint32_t i = 0; i < s->switch_stmt.case_count; i++) {
                CaseClause& cc = s->switch_stmt.cases[i];
                if (!cc.value)
                    hasDefault = true;

                bool caseReturns = false;
                for (uint32_t j = 0; j < cc.body_count; j++)
                    if (stmtAlwaysReturns(cc.body[j])) {
                        caseReturns = true;
                        break;
                    }

                if (!caseReturns)
                    allCasesReturn = false;
            }

            return hasDefault && allCasesReturn &&
                   s->switch_stmt.case_count > 0;
        }

        default:
            return false;
    }
}

void Sema::checkStmt(Stmt* s) {
    if (!s)
        return;

    switch (s->kind) {
        case StmtKind::EXPR_STMT:
            checkExpr(s->expr_stmt.expr);
            break;

        case StmtKind::BLOCK:
            checkBlock(s);
            break;

        case StmtKind::CONST_DECL:
        case StmtKind::VAR_DECL:
            checkVarDecl(s);
            break;

        case StmtKind::STRUCT_DECL:
        case StmtKind::UNION_DECL:
        case StmtKind::ENUM_DECL:
        case StmtKind::FUNC_DECL:
            break;

        case StmtKind::IF_STMT:
            checkIf(s);
            break;

        case StmtKind::WHILE_STMT:
            checkWhile(s);
            break;

        case StmtKind::DO_WHILE_STMT:
            checkDoWhile(s);
            break;

        case StmtKind::FOR_STMT:
            checkFor(s);
            break;

        case StmtKind::SWITCH_STMT:
        case StmtKind::MATCH_STMT:
            checkSwitch(s);
            break;

        case StmtKind::BREAK_STMT:
            if (loop_depth_ == 0 && switch_depth_ == 0)
                error(
                    "'break' statement is not allowed outside of a loop or "
                    "switch",
                    s->span);
            break;

        case StmtKind::CONTINUE_STMT:
            if (loop_depth_ == 0)
                error("'continue' statement is not allowed outside of a loop",
                      s->span);
            break;

        case StmtKind::RETURN_STMT:
            checkReturn(s);
            break;

        case StmtKind::DEFER_STMT:
            checkStmt(s->defer_stmt.body);
            break;

        default:
            break;
    }
}

void Sema::checkBlock(Stmt* s) {
    symbols_.push();
    for (uint32_t i = 0; i < s->block.count; i++)
        checkStmt(s->block.stmts[i]);
    symbols_.pop();
}

void Sema::checkVarDecl(Stmt* s) {
    std::string name(s->var_decl.name);

    Type* declaredType =
        s->var_decl.type ? resolveType(s->var_decl.type) : nullptr;
    Type* initType = s->var_decl.init ? checkExpr(s->var_decl.init) : nullptr;

    if (!s->var_decl.init && s->kind == StmtKind::CONST_DECL)
        error("constant '" + name + "' must be initialized", s->span);

    Type* finalType = declaredType;
    if (!finalType) {
        if (s->var_decl.init) {
            if (initType->kind == TypeKind::INT_CONSTANT)
                finalType = getPrimitive(TypeKind::I32);
            else if (initType->kind == TypeKind::FLOAT_CONSTANT)
                finalType = getPrimitive(TypeKind::F32);
            else
                finalType = initType;
        } else if (s->kind == StmtKind::VAR_DECL) {
            error("cannot infer type of '" + name +
                      "' without a type annotation or an initializer",
                  s->span);
        }
    } else if (initType && !isAssignable(initType, finalType)) {
        error("cannot initialize '" + name + "' of type '" +
                  typeToString(finalType) + "' with a value of type '" +
                  typeToString(initType) + "'",
              s->var_decl.init->span);
    }

    if (s->var_decl.init && finalType)
        coerceToType(s->var_decl.init, finalType);

    s->var_decl.type = finalType;

    Symbol sym;
    sym.name = name;
    sym.kind = SymbolKind::VAR;
    sym.type = finalType;
    sym.is_mutable = s->kind == StmtKind::VAR_DECL;
    sym.is_global = isGlobal;
    sym.is_extern = false;
    sym.span = s->span;
    sym.decl = s;

    s->var_decl.is_global = isGlobal;

    if (symbols_.lookup_local(name))
        error("redefinition of '" + name + "' in this scope", s->span);
    else
        symbols_.declare(name, sym);
}
void Sema::checkIf(Stmt* s) {
    Type* condType = checkExpr(s->if_stmt.cond);
    if (condType && !isBoolType(condType))
        error("condition of 'if' statement must be of type 'bool', got '" +
                  typeToString(condType) + "'",
              s->if_stmt.cond->span);

    checkStmt(s->if_stmt.then_branch);
    if (s->if_stmt.else_branch)
        checkStmt(s->if_stmt.else_branch);
}

void Sema::checkWhile(Stmt* s) {
    Type* condType = checkExpr(s->while_stmt.cond);
    if (condType && !isBoolType(condType))
        error("condition of 'while' statement must be of type 'bool', got '" +
                  typeToString(condType) + "'",
              s->while_stmt.cond->span);

    loop_depth_++;
    checkStmt(s->while_stmt.body);
    loop_depth_--;
}

void Sema::checkDoWhile(Stmt* s) {
    loop_depth_++;
    checkStmt(s->while_stmt.body);
    loop_depth_--;

    Type* condType = checkExpr(s->while_stmt.cond);
    if (condType && !isBoolType(condType))
        error(
            "condition of 'do-while' statement must be of type 'bool', got '" +
                typeToString(condType) + "'",
            s->while_stmt.cond->span);
}

void Sema::checkFor(Stmt* s) {
    symbols_.push();

    if (s->for_stmt.init)
        checkStmt(s->for_stmt.init);

    if (s->for_stmt.cond) {
        Type* condType = checkExpr(s->for_stmt.cond);
        if (condType && !isBoolType(condType))
            error("condition of 'for' statement must be of type 'bool', got '" +
                      typeToString(condType) + "'",
                  s->for_stmt.cond->span);
    }

    if (s->for_stmt.step)
        checkExpr(s->for_stmt.step);

    loop_depth_++;
    checkStmt(s->for_stmt.body);
    loop_depth_--;

    symbols_.pop();
}

void Sema::checkSwitch(Stmt* s) {
    Type* subjectType = checkExpr(s->switch_stmt.subject);
    bool sawDefault = false;

    switch_depth_++;

    for (uint32_t i = 0; i < s->switch_stmt.case_count; i++) {
        CaseClause& cc = s->switch_stmt.cases[i];

        if (!cc.value) {
            if (sawDefault)
                error("multiple 'default' cases in the same switch statement",
                      cc.span);
            sawDefault = true;
        } else {
            Type* caseType = checkExpr(cc.value);
            if (subjectType && caseType &&
                !isAssignable(caseType, subjectType) &&
                !isAssignable(subjectType, caseType))
                error("case value of type '" + typeToString(caseType) +
                          "' does not match the switch subject type '" +
                          typeToString(subjectType) + "'",
                      cc.value->span);
        }

        symbols_.push();
        for (uint32_t j = 0; j < cc.body_count; j++)
            checkStmt(cc.body[j]);
        symbols_.pop();
    }

    switch_depth_--;
}

void Sema::checkReturn(Stmt* s) {
    if (current_function_name_.empty()) {
        error("'return' statement is not allowed outside of a function",
              s->span);
        if (s->return_stmt.value)
            checkExpr(s->return_stmt.value);
        return;
    }

    Type* valType =
        s->return_stmt.value ? checkExpr(s->return_stmt.value) : nullptr;

    if (inferring_return_) {
        if (s->return_stmt.value) {
            if (valType)
                inferred_return_types_.push_back(valType);
            inferred_return_stmts_.push_back(s);
        } else {
            inferred_has_void_ = true;
        }
        return;
    }
    bool isVoid = (current_return_type_ == nullptr ||
                   current_return_type_->kind == TypeKind::VOID);

    if (isVoid) {
        if (s->return_stmt.value)
            error("function '" + current_function_name_ +
                      "' has no return value, but a value of type '" +
                      typeToString(valType) + "' was returned",
                  s->return_stmt.value->span);
    } else {
        if (!s->return_stmt.value)
            error("function '" + current_function_name_ +
                      "' expects a return value of type '" +
                      typeToString(current_return_type_) +
                      "', but none was provided",
                  s->span);
        else if (valType && !isAssignable(valType, current_return_type_))
            error("cannot return a value of type '" + typeToString(valType) +
                      "' from function '" + current_function_name_ +
                      "' expecting '" + typeToString(current_return_type_) +
                      "'",
                  s->return_stmt.value->span);
        else if (valType)
            coerceToType(s->return_stmt.value, current_return_type_);
    }
}

Type* Sema::checkExpr(Expr* e) {
    if (!e)
        return nullptr;

    switch (e->kind) {
        case ExprKind::LITERAL_INT:
            e->resolved_type = getPrimitive(TypeKind::INT_CONSTANT);
            return e->resolved_type;

        case ExprKind::LITERAL_FLOAT:
            e->resolved_type = getPrimitive(TypeKind::FLOAT_CONSTANT);
            return e->resolved_type;

        case ExprKind::LITERAL_CHAR:
            e->resolved_type = getPrimitive(TypeKind::CHAR);
            return e->resolved_type;

        case ExprKind::LITERAL_STRING:
            e->resolved_type = Type::makePointer(
                arena_, getPrimitive(TypeKind::CHAR), e->span);
            return e->resolved_type;

        case ExprKind::LITERAL_BOOL:
            e->resolved_type = getPrimitive(TypeKind::BOOL);
            return e->resolved_type;

        case ExprKind::LITERAL_ARRAY:
            return checkLiteralArray(e);
        case ExprKind::LITERAL_STRUCT:
            return checkLiteralStruct(e);

        case ExprKind::IDENTIFIER: {
            std::string name(e->identifier.name);
            Symbol* sym = symbols_.lookup(name);
            if (!sym) {
                if (types_.count(name))
                    error("'" + name + "' is a type, not a value", e->span);
                else
                    error("use of undeclared identifier '" + name + "'",
                          e->span);
                return nullptr;
            }

            e->identifier.is_global = sym->is_global;
            e->resolved_type = sym->type;

            return sym->type;
        }

        case ExprKind::BINARY:
            return checkBinary(e);

        case ExprKind::UNARY:
            return checkUnary(e);

        case ExprKind::ASSIGN:
            return checkAssign(e);

        case ExprKind::CALL:
            return checkCall(e);

        case ExprKind::INDEX:
            return checkIndex(e);

        case ExprKind::ARROW_MEMBER:
        case ExprKind::MEMBER:
            return checkMember(e);

        case ExprKind::CAST:
            return checkCast(e);

        case ExprKind::SIZEOF:
            return checkSizeof(e);

        case ExprKind::TERNARY:
            return checkTernary(e);

        case ExprKind::PRE_INC_DEC:
            return checkIncDec(e, e->pre_inc_dec.op, e->pre_inc_dec.operand,
                               e->span);

        case ExprKind::POST_INC_DEC:
            return checkIncDec(e, e->post_inc_dec.op, e->post_inc_dec.operand,
                               e->span);

        default:
            return nullptr;
    }
}

Type* Sema::checkLiteralArray(Expr* e) {
    if (e->literal_array.value_count == 0) {
        error("cannot infer the type of an empty array literal", e->span);
        return nullptr;
    }

    Type* elemType = checkExpr(e->literal_array.values[0]);

    for (uint32_t i = 1; i < e->literal_array.value_count; i++) {
        Type* t = checkExpr(e->literal_array.values[i]);
        if (elemType && t && !typesEqual(elemType, t)) {
            bool ok;
            Type* common = promoteNumeric(elemType, t, ok);
            if (isNumericType(elemType) && isNumericType(t) && ok)
                elemType = common;
            else
                error("array literal has mismatched element types: '" +
                          typeToString(elemType) + "' and '" + typeToString(t) +
                          "'",
                      e->literal_array.values[i]->span);
        }
    }

    if (elemType)
        for (uint32_t i = 0; i < e->literal_array.value_count; i++)
            coerceToType(e->literal_array.values[i], elemType);

    Type* arrType = Type::makeArray(arena_, elemType,
                                    e->literal_array.value_count, e->span);
    e->resolved_type = arrType;
    return arrType;
}

Type* Sema::checkLiteralStruct(Expr* e) {
    Type* target = resolveType(e->literal_struct.target);
    if (!target || target->kind != TypeKind::NAMED) {
        for (uint32_t i = 0; i < e->literal_struct.field_count; i++)
            checkExpr(e->literal_struct.fields[i].default_value);
        return target;
    }

    std::string typeName(target->named.name);
    auto it = types_.find(typeName);
    if (it == types_.end())
        return nullptr;

    Stmt* decl = it->second;
    if (decl->kind != StmtKind::STRUCT_DECL &&
        decl->kind != StmtKind::UNION_DECL) {
        error("type '" + typeName +
                  "' is not a struct or union and cannot be used in a struct "
                  "literal",
              e->span);
        return nullptr;
    }

    std::unordered_set<std::string> seen;

    for (uint32_t i = 0; i < e->literal_struct.field_count; i++) {
        Field& lf = e->literal_struct.fields[i];
        std::string fieldName(lf.name);

        Field* declField = nullptr;
        for (uint32_t f = 0; f < decl->struct_union_decl.field_count; f++) {
            if (decl->struct_union_decl.fields[f].name == lf.name) {
                declField = &decl->struct_union_decl.fields[f];
                break;
            }
        }

        Type* valType = checkExpr(lf.default_value);

        if (!declField) {
            error("'" + typeName + "' has no member named '" + fieldName + "'",
                  lf.span);
            continue;
        }

        if (seen.count(fieldName))
            error("duplicate initializer for field '" + fieldName +
                      "' in struct literal",
                  lf.span);
        seen.insert(fieldName);

        if (valType && declField->type &&
            !isAssignable(valType, declField->type))
            error("field '" + fieldName + "' has type '" +
                      typeToString(declField->type) +
                      "' but initializer has type '" + typeToString(valType) +
                      "'",
                  lf.default_value ? lf.default_value->span : lf.span);
        else if (valType && declField->type)
            coerceToType(lf.default_value, declField->type);
    }

    e->resolved_type = target;
    return target;
}

Type* Sema::checkBinary(Expr* e) {
    Type* lt = checkExpr(e->binary.left);
    Type* rt = checkExpr(e->binary.right);
    TokenKind op = e->binary.op;

    if (!lt || !rt)
        return nullptr;

    switch (op) {
        case TokenKind::LOGICAL_AND_TOKEN:
        case TokenKind::LOGICAL_OR_TOKEN: {
            if (!isBoolType(lt))
                error("left operand of '" + opName(op) +
                          "' must be of type 'bool', got '" + typeToString(lt) +
                          "'",
                      e->binary.left->span);
            if (!isBoolType(rt))
                error("right operand of '" + opName(op) +
                          "' must be of type 'bool', got '" + typeToString(rt) +
                          "'",
                      e->binary.right->span);
            e->resolved_type = getPrimitive(TypeKind::BOOL);
            return e->resolved_type;
        }

        case TokenKind::EQUAL_EQUAL_TOKEN:
        case TokenKind::NOT_EQUAL_TOKEN: {
            bool comparable = typesEqual(lt, rt) ||
                              (isNumericType(lt) && isNumericType(rt)) ||
                              (lt->kind == TypeKind::POINTER &&
                               rt->kind == TypeKind::POINTER);
            if (!comparable)
                error("cannot compare values of type '" + typeToString(lt) +
                          "' and '" + typeToString(rt) + "'",
                      e->span);
            else if (isNumericType(lt) && isNumericType(rt)) {
                bool ok;
                Type* common = promoteNumeric(lt, rt, ok);
                if (ok) {
                    coerceToType(e->binary.left, common);
                    coerceToType(e->binary.right, common);
                }
            }
            e->resolved_type = getPrimitive(TypeKind::BOOL);
            return e->resolved_type;
        }

        case TokenKind::LESS_TOKEN:
        case TokenKind::GREATER_TOKEN:
        case TokenKind::LESS_EQUAL_TOKEN:
        case TokenKind::GREATER_EQUAL_TOKEN: {
            if (!isNumericType(lt) || !isNumericType(rt)) {
                error("relational operator '" + opName(op) +
                          "' requires numeric operands, got '" +
                          typeToString(lt) + "' and '" + typeToString(rt) + "'",
                      e->span);
                return nullptr;
            }
            bool ok;
            Type* common = promoteNumeric(lt, rt, ok);
            if (!ok)
                error("cannot compare '" + typeToString(lt) + "' with '" +
                          typeToString(rt) +
                          "' because they have different signedness",
                      e->span);
            else {
                coerceToType(e->binary.left, common);
                coerceToType(e->binary.right, common);
            }
            e->resolved_type = getPrimitive(TypeKind::BOOL);
            return e->resolved_type;
        }

        case TokenKind::AMPERSAND_TOKEN:
        case TokenKind::PIPE_TOKEN:
        case TokenKind::CARET_TOKEN:
        case TokenKind::LEFT_SHIFT_TOKEN:
        case TokenKind::RIGHT_SHIFT_TOKEN: {
            if (!isIntegerType(lt) || !isIntegerType(rt)) {
                error("bitwise operator '" + opName(op) +
                          "' requires integer operands, got '" +
                          typeToString(lt) + "' and '" + typeToString(rt) + "'",
                      e->span);
                return nullptr;
            }
            bool ok;
            Type* result = promoteNumeric(lt, rt, ok);
            if (!ok) {
                error("operands of '" + opName(op) +
                          "' have mismatched signedness: '" + typeToString(lt) +
                          "' and '" + typeToString(rt) + "'",
                      e->span);
                return nullptr;
            }
            coerceToType(e->binary.left, result);
            coerceToType(e->binary.right, result);
            e->resolved_type = result;
            return result;
        }

        default: {
            if (!isNumericType(lt) || !isNumericType(rt)) {
                error("invalid operands to binary '" + opName(op) + "': '" +
                          typeToString(lt) + "' and '" + typeToString(rt) + "'",
                      e->span);
                return nullptr;
            }

            if (op == TokenKind::MODULO_TOKEN &&
                (isFloatType(lt) || isFloatType(rt))) {
                error("operator '%' requires integer operands, got '" +
                          typeToString(lt) + "' and '" + typeToString(rt) + "'",
                      e->span);
                return nullptr;
            }

            bool ok;
            Type* result = promoteNumeric(lt, rt, ok);
            if (!ok) {
                error("operands of '" + opName(op) +
                          "' have mismatched signedness: '" + typeToString(lt) +
                          "' and '" + typeToString(rt) + "'",
                      e->span);
                return nullptr;
            }
            coerceToType(e->binary.left, result);
            coerceToType(e->binary.right, result);
            e->resolved_type = result;
            return result;
        }
    }
}

Type* Sema::checkUnary(Expr* e) {
    TokenKind op = e->unary.op;
    Expr* operand = e->unary.operand;

    if (op == TokenKind::AMPERSAND_TOKEN) {
        Type* t = checkExpr(operand);
        if (!t)
            return nullptr;
        if (!isLValue(operand)) {
            error("cannot take the address of a non-lvalue expression",
                  e->span);
            return nullptr;
        }
        Type* result = Type::makePointer(arena_, t, e->span);
        e->resolved_type = result;
        return result;
    }

    if (op == TokenKind::MULTIPLY_TOKEN) {
        Type* t = checkExpr(operand);
        if (!t)
            return nullptr;
        if (t->kind != TypeKind::POINTER) {
            error("cannot dereference a value of non-pointer type '" +
                      typeToString(t) + "'",
                  e->span);
            return nullptr;
        }
        e->resolved_type = t->wrapper.inner;
        return e->resolved_type;
    }

    Type* t = checkExpr(operand);
    if (!t)
        return nullptr;

    switch (op) {
        case TokenKind::LOGICAL_NOT_TOKEN:
            if (!isBoolType(t))
                error("operand of '!' must be of type 'bool', got '" +
                          typeToString(t) + "'",
                      e->span);
            e->resolved_type = getPrimitive(TypeKind::BOOL);
            return e->resolved_type;

        case TokenKind::TILDE_TOKEN:
            if (!isIntegerType(t))
                error("operand of '~' must be an integer type, got '" +
                          typeToString(t) + "'",
                      e->span);
            e->resolved_type = t;
            return t;

        case TokenKind::MINUS_TOKEN:
        case TokenKind::PLUS_TOKEN:
            if (!isNumericType(t))
                error("operand of unary '" + opName(op) +
                          "' must be numeric, got '" + typeToString(t) + "'",
                      e->span);
            e->resolved_type = t;
            return t;

        default:
            e->resolved_type = t;
            return t;
    }
}

Type* Sema::checkAssign(Expr* e) {
    Type* targetType = checkExpr(e->assign.target);
    Type* valueType = checkExpr(e->assign.value);

    bool assignable = checkAssignableTarget(e->assign.target);
    if (!assignable || !targetType || !valueType)
        return targetType;

    TokenKind op = e->assign.op;

    if (op == TokenKind::ASSIGN_TOKEN) {
        if (!isAssignable(valueType, targetType))
            error("cannot assign a value of type '" + typeToString(valueType) +
                      "' to a variable of type '" + typeToString(targetType) +
                      "'",
                  e->assign.value->span);
    } else {
        bool isBitwiseAssign = op == TokenKind::AND_ASSIGN_TOKEN ||
                               op == TokenKind::OR_ASSIGN_TOKEN ||
                               op == TokenKind::XOR_ASSIGN_TOKEN ||
                               op == TokenKind::LEFT_SHIFT_ASSIGN_TOKEN ||
                               op == TokenKind::RIGHT_SHIFT_ASSIGN_TOKEN;

        if (isBitwiseAssign) {
            if (!isIntegerType(targetType) || !isIntegerType(valueType))
                error("compound assignment '" + opName(op) +
                          "' requires integer operands, got '" +
                          typeToString(targetType) + "' and '" +
                          typeToString(valueType) + "'",
                      e->span);
        } else {
            if (!isNumericType(targetType) || !isNumericType(valueType)) {
                error("compound assignment '" + opName(op) +
                          "' requires numeric operands, got '" +
                          typeToString(targetType) + "' and '" +
                          typeToString(valueType) + "'",
                      e->span);
            } else if (!isAssignable(valueType, targetType)) {
                error("cannot apply '" + opName(op) +
                          "' to a variable of type '" +
                          typeToString(targetType) +
                          "' with a value of type '" + typeToString(valueType) +
                          "'",
                      e->span);
            }
        }
    }

    coerceToType(e->assign.value, targetType);

    e->resolved_type = targetType;
    return targetType;
}

Type* Sema::checkCall(Expr* e) {
    Expr* callee = e->call.callee;

    if (callee->kind != ExprKind::IDENTIFIER) {
        Type* ct = checkExpr(callee);
        for (uint32_t i = 0; i < e->call.arg_count; i++)
            checkExpr(e->call.args[i]);
        if (ct && ct->kind == TypeKind::FUNC) {
            e->resolved_type = ct->func.return_type;
            return ct->func.return_type;
        }
        error("expression is not callable", callee->span);
        return nullptr;
    }

    std::string name(callee->identifier.name);
    Symbol* sym = symbols_.lookup(name);

    if (!sym) {
        error("call to undeclared function '" + name + "'", callee->span);
        for (uint32_t i = 0; i < e->call.arg_count; i++)
            checkExpr(e->call.args[i]);
        return nullptr;
    }

    if (sym->kind != SymbolKind::FUNC) {
        error("'" + name + "' is not a function", callee->span);
        for (uint32_t i = 0; i < e->call.arg_count; i++)
            checkExpr(e->call.args[i]);
        return nullptr;
    }

    Stmt* decl = sym->decl;
    uint32_t expected = decl->func_decl.param_count;
    uint32_t got = e->call.arg_count;
    bool isVariadic = decl->func_decl.is_variadic;
    uint32_t requiredCount = isVariadic ? expected - 1 : expected;

    if (got < requiredCount || (!isVariadic && got != expected)) {
        error("function '" + name + "' expects " +
                  std::to_string(requiredCount) +
                  (isVariadic ? " or more argument(s)" : " argument(s)") +
                  " but " + std::to_string(got) + " were provided",
              e->span);
    }

    uint32_t checkCount = got < requiredCount ? got : requiredCount;
    for (uint32_t i = 0; i < checkCount; i++) {
        Type* argType = checkExpr(e->call.args[i]);
        Type* paramType = decl->func_decl.params[i].type;
        if (argType && paramType && !isAssignable(argType, paramType))
            error("argument " + std::to_string(i + 1) + " to '" + name +
                      "' has type '" + typeToString(argType) + "' but '" +
                      typeToString(paramType) + "' was expected",
                  e->call.args[i]->span);
        else if (argType && paramType)
            coerceToType(e->call.args[i], paramType);
    }

    for (uint32_t i = checkCount; i < got; i++)
        checkExpr(e->call.args[i]);

    e->resolved_type = sym->type;
    return sym->type;
}

bool Sema::isFormattable(Type* t) {
    if (!t)
        return false;

    switch (t->kind) {
        case TypeKind::I8:
        case TypeKind::I16:
        case TypeKind::I32:
        case TypeKind::I64:
        case TypeKind::U8:
        case TypeKind::U16:
        case TypeKind::U32:
        case TypeKind::U64:
        case TypeKind::F32:
        case TypeKind::F64:
        case TypeKind::BOOL:
        case TypeKind::CHAR:
        case TypeKind::POINTER:
            return true;
        default:
            return false;
    }
}

Type* Sema::checkIndex(Expr* e) {
    Type* base = checkExpr(e->index.array);
    Type* idx = checkExpr(e->index.index);

    if (idx && !isIntegerType(idx))
        error("array index must be an integer type, got '" + typeToString(idx) +
                  "'",
              e->index.index->span);

    if (!base)
        return nullptr;

    switch (base->kind) {
        case TypeKind::ARRAY:
            e->resolved_type = base->array.element;
            return e->resolved_type;
        case TypeKind::SLICE:
            e->resolved_type = base->slice.element;
            return e->resolved_type;
        case TypeKind::POINTER:
            e->resolved_type = base->wrapper.inner;
            return e->resolved_type;
        default:
            error("cannot index into a value of type '" + typeToString(base) +
                      "'",
                  e->index.array->span);
            return nullptr;
    }
}

Type* Sema::checkMember(Expr* e) {
    Type* base = checkExpr(e->member.object);
    if (!base)
        return nullptr;

    Type* structType = base;
    if (structType->kind == TypeKind::POINTER)
        structType = structType->wrapper.inner;

    if (!structType || structType->kind != TypeKind::NAMED) {
        error("cannot access member '" + std::string(e->member.field) +
                  "' on non-struct type '" + typeToString(base) + "'",
              e->span);
        return nullptr;
    }

    std::string typeName(structType->named.name);
    auto it = types_.find(typeName);
    if (it == types_.end())
        return nullptr;

    Stmt* decl = it->second;
    if (decl->kind != StmtKind::STRUCT_DECL &&
        decl->kind != StmtKind::UNION_DECL) {
        error("type '" + typeName +
                  "' is not a struct or union and has no members",
              e->span);
        return nullptr;
    }

    for (uint32_t f = 0; f < decl->struct_union_decl.field_count; f++) {
        Field& field = decl->struct_union_decl.fields[f];
        if (field.name == e->member.field) {
            e->resolved_type = field.type;
            return field.type;
        }
    }

    error("'" + typeName + "' has no member named '" +
              std::string(e->member.field) + "'",
          e->span);
    return nullptr;
}

Type* Sema::checkCast(Expr* e) {
    Type* from = checkExpr(e->cast.operand);
    Type* to = resolveType(e->cast.target_type);

    if (!from || !to)
        return to;

    if (!isValidCast(from, to))
        error("invalid cast from '" + typeToString(from) + "' to '" +
                  typeToString(to) + "'",
              e->span);

    e->resolved_type = to;
    return to;
}

Type* Sema::checkSizeof(Expr* e) {
    if (e->size_of.is_type) {
        resolveType(e->size_of.type);
    } else {
        Expr* inner = e->size_of.expr;

        // `sizeof(Nombre)` es ambiguo en la gramática: puede ser el tamaño
        // de una expresión llamada Nombre o el tamaño del tipo Nombre. El
        // parser no sabe distinguirlos (eso es trabajo de Sema), así que
        // siempre llega como is_type = false con un IDENTIFIER adentro.
        // Si no existe un símbolo de valor con ese nombre pero sí un tipo,
        // reinterpretamos el nodo como sizeof de tipo.
        if (inner && inner->kind == ExprKind::IDENTIFIER) {
            std::string name(inner->identifier.name);
            if (!symbols_.lookup(name) && types_.count(name)) {
                Type* named = Type::makeNamed(arena_, inner->identifier.name,
                                              inner->span);
                e->size_of.is_type = true;
                e->size_of.type = named;
                e->size_of.expr = nullptr;
                resolveType(named);
            } else {
                checkExpr(inner);
            }
        } else {
            checkExpr(inner);
        }
    }

    e->resolved_type = getPrimitive(TypeKind::U64);
    return e->resolved_type;
}

Type* Sema::checkTernary(Expr* e) {
    Type* condType = checkExpr(e->ternary.cond);
    if (condType && !isBoolType(condType))
        error("condition of ternary expression must be of type 'bool', got '" +
                  typeToString(condType) + "'",
              e->ternary.cond->span);

    Type* thenType = checkExpr(e->ternary.then_branch);
    Type* elseType = checkExpr(e->ternary.else_branch);

    if (!thenType || !elseType)
        return nullptr;

    if (typesEqual(thenType, elseType)) {
        if (isNumericType(thenType) && isNumericType(elseType)) {
            bool ok;
            Type* common = promoteNumeric(thenType, elseType, ok);
            if (ok) {
                coerceToType(e->ternary.then_branch, common);
                coerceToType(e->ternary.else_branch, common);
                e->resolved_type = common;
                return common;
            }
        }
        e->resolved_type = thenType;
        return thenType;
    }

    if (isNumericType(thenType) && isNumericType(elseType)) {
        bool ok;
        Type* common = promoteNumeric(thenType, elseType, ok);
        if (ok) {
            coerceToType(e->ternary.then_branch, common);
            coerceToType(e->ternary.else_branch, common);
            e->resolved_type = common;
            return common;
        }
    }

    error("branches of ternary expression have incompatible types: '" +
              typeToString(thenType) + "' and '" + typeToString(elseType) + "'",
          e->span);
    return nullptr;
}

Type* Sema::checkIncDec(Expr* e, TokenKind op, Expr* operand, Span span) {
    Type* t = checkExpr(operand);
    if (!t)
        return nullptr;

    if (!isNumericType(t))
        error("operand of '" + opName(op) + "' must be numeric, got '" +
                  typeToString(t) + "'",
              span);

    if (!isLValue(operand))
        error(
            "operand of '" + opName(op) + "' must be an assignable expression",
            span);

    operand->resolved_type = t;

    e->resolved_type = t;
    return t;
}
