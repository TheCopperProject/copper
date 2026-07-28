#pragma once

#include <cstddef>
#include <utility>
#include <memory>

template <std::size_t N>
class ArenaChunk
{
private:
    alignas(std::max_align_t) std::byte buffer_[N];
    std::size_t offset_ = 0;

public:
    ArenaChunk* next = nullptr; 

    ArenaChunk() = default;
    ~ArenaChunk() = default;

    ArenaChunk(const ArenaChunk&) = delete;
    ArenaChunk& operator=(const ArenaChunk&) = delete;

    void* try_allocate(std::size_t size, std::size_t align)
    {
        std::byte* base   = buffer_ + offset_;
        std::size_t space = N - offset_;

        void* ptr = base;
        if (std::align(align, size, ptr, space) == nullptr)
            return nullptr; 

        std::byte* aligned = static_cast<std::byte*>(ptr);
        offset_ = static_cast<std::size_t>((aligned - buffer_) + size);
        return ptr;
    }

    void reset() { offset_ = 0; }

    static constexpr std::size_t capacity() { return N; }
};

template <std::size_t ChunkSize = 4096>
class ArenaAllocatorT
{
private:
    using Chunk = ArenaChunk<ChunkSize>;

    struct BigChunk
    {
        std::byte* data;
        std::size_t size;
        std::size_t offset = 0;
        BigChunk* next = nullptr;
    };

    Chunk* head_        = nullptr; 
    Chunk* current_     = nullptr; 
    BigChunk* bigHead_  = nullptr; 
    std::size_t chunkCount_ = 0;

    Chunk* allocate_new_chunk()
    {
        Chunk* c = new Chunk();
        if (!head_) {
            head_ = current_ = c;
        } else {
            current_->next = c;
            current_ = c;
        }
        ++chunkCount_;
        return c;
    }

    void* allocate_big(std::size_t size, std::size_t align)
    {
        std::size_t total = size + align;
        std::byte* raw = static_cast<std::byte*>(::operator new(total));

        void* ptr = raw;
        std::size_t space = total;
        std::align(align, size, ptr, space);

        BigChunk* bc = new BigChunk{ raw, total };
        bc->next = bigHead_;
        bigHead_ = bc;

        return ptr;
    }

public:
    ArenaAllocatorT() = default;

    ~ArenaAllocatorT()
    {
        Chunk* c = head_;
        while (c) {
            Chunk* next = c->next;
            delete c;
            c = next;
        }
        BigChunk* bc = bigHead_;
        while (bc) {
            BigChunk* next = bc->next;
            ::operator delete(bc->data);
            delete bc;
            bc = next;
        }
    }

    ArenaAllocatorT(const ArenaAllocatorT&) = delete;
    ArenaAllocatorT& operator=(const ArenaAllocatorT&) = delete;

    void* allocate(std::size_t size, std::size_t align = alignof(std::max_align_t))
    {
        if (size > ChunkSize) {
            return allocate_big(size, align);
        }

        if (!current_) {
            allocate_new_chunk();
        }

        if (void* p = current_->try_allocate(size, align))
            return p;

        Chunk* c = allocate_new_chunk();
        void* p = c->try_allocate(size, align);
        return p; 
    }

    template <typename T, typename... Args>
    T* create(Args&&... args)
    {
        void* mem = allocate(sizeof(T), alignof(T));
        return ::new (mem) T(std::forward<Args>(args)...);
    }

    template <typename T>
    T* create_array(std::size_t n)
    {
        void* mem = allocate(sizeof(T) * n, alignof(T));
        return static_cast<T*>(mem);
    }

    void reset()
    {
        Chunk* c = head_;
        while (c) {
            c->reset();
            c = c->next;
        }
        current_ = head_;

        BigChunk* bc = bigHead_;
        while (bc) {
            BigChunk* next = bc->next;
            ::operator delete(bc->data);
            delete bc;
            bc = next;
        }
        bigHead_ = nullptr;
    }

    std::size_t chunk_count() const { return chunkCount_; }
};

using ArenaAllocator = ArenaAllocatorT<4096>;