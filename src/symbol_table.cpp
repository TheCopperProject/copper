#include "symbol_table.hpp"

SymbolTable::SymbolTable()
    : current_(nullptr)
{
    push();
}

SymbolTable::~SymbolTable() = default;

void SymbolTable::push()
{
    auto scope = std::make_unique<SymbolScope>();
    scope->parent = current_;
    current_ = scope.get();
    scopes_.push_back(std::move(scope));
}

void SymbolTable::pop()
{
    if (current_)
    {
        current_ = current_->parent;
        if (!scopes_.empty())
            scopes_.pop_back();
    }
}

bool SymbolTable::declare(const std::string& name, const Symbol& sym)
{
    if (!current_)
        return false;
    if (current_->symbols.count(name))
        return false;
    current_->symbols[name] = sym;
    return true;
}

bool SymbolTable::declare(std::string_view name, const Symbol& sym)
{
    return declare(std::string(name), sym);
}

Symbol* SymbolTable::lookup(const std::string& name)
{
    for (SymbolScope* s = current_; s != nullptr; s = s->parent)
    {
        auto it = s->symbols.find(name);
        if (it != s->symbols.end())
            return &it->second;
    }
    return nullptr;
}

Symbol* SymbolTable::lookup(std::string_view name)
{
    return lookup(std::string(name));
}

Symbol* SymbolTable::lookup_local(const std::string& name)
{
    if (!current_)
        return nullptr;
    auto it = current_->symbols.find(name);
    if (it != current_->symbols.end())
        return &it->second;
    return nullptr;
}

Symbol* SymbolTable::lookup_local(std::string_view name)
{
    return lookup_local(std::string(name));
}

SymbolScope* SymbolTable::current() const
{
    return current_;
}