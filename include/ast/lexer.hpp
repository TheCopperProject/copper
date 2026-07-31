#pragma once

#include <vector>
#include "memory/arena.hpp"
#include "ast/token.hpp"

std::vector<Token> tokenify(const char *source_name, const char *source, ArenaAllocator &arena);
