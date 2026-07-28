#pragma once

#include "span.hpp"
#include <vector>
#include "arena.hpp"   
#include "token.hpp"
#include <string>

std::vector<Token> tokenify(const char *source_name, const char *source, ArenaAllocator &arena);