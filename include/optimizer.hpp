#pragma once

#include "ast.hpp"
#include "errors.hpp"

SourceFile *optimizeSourceFile(SourceFile *source, ArenaAllocator &arena, ErrorCollector& errors);