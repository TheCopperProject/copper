#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include "ast.hpp"

enum class SymbolKind
{
    FUNC,
    VAR,
    STRUCT,
    ENUM,
    UNION,
    TYPE_ALIAS
};

struct Symbol
{
    std::string name;
    SymbolKind kind;
    Type* type;
    bool is_global;
    bool is_mutable;
    bool is_extern;
    Span span;
    Stmt* decl;
};

struct SymbolScope
{
    std::unordered_map<std::string, Symbol> symbols;
    SymbolScope* parent;
};

class SymbolTable
{
public:
    SymbolTable();
    ~SymbolTable();

    void push();
    void pop();

    bool declare(const std::string& name, const Symbol& sym);
    bool declare(std::string_view name, const Symbol& sym);

    Symbol* lookup(const std::string& name);
    Symbol* lookup(std::string_view name);

    Symbol* lookup_local(const std::string& name);
    Symbol* lookup_local(std::string_view name);

    SymbolScope* current() const;

private:
    std::vector<std::unique_ptr<SymbolScope>> scopes_;
    SymbolScope* current_;
};