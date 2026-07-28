#pragma once

#include <cstdint>

struct Span
{
    uint32_t start_line = 0;
    uint32_t start_column = 0;
    uint32_t end_line = 0;
    uint32_t end_column = 0;

    uint32_t offset = 0; 
    uint32_t length = 0;

    uint32_t file_id = 0;

    static Span merge(const Span &a, const Span &b)
    {
        Span s;
        s.start_line = a.start_line;
        s.start_column = a.start_column;
        s.end_line = b.end_line;
        s.end_column = b.end_column;
        s.offset = a.offset;
        s.length = (b.offset + b.length) - a.offset;
        s.file_id = a.file_id;
        return s;
    }
};