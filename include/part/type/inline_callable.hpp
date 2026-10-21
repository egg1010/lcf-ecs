#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

// inline_callable - 类型擦除可调用体：小对象内联，超出或非平凡析构回退堆
// callable_bind 供调用方自备存储（如 job 的 64 字节内联区）复用同一套 thunk

enum class callable_placement : std::uint8_t
{
    none = 0,
    inline_storage = 1,
    heap = 2,
};

using callable_invoke_fn = void (*)(void* self) noexcept;
using callable_destroy_fn = void (*)(void* self) noexcept;

template <std::size_t StorageSize, std::size_t StorageAlign, typename F>
callable_placement callable_bind_inline(void* storage, F&& fn, callable_invoke_fn& invoke,
                                        callable_destroy_fn& destroy) noexcept
{
    using stored = std::decay_t<F>;
    static_assert(sizeof(stored) <= StorageSize, "capture exceeds inline storage");
    static_assert(alignof(stored) <= StorageAlign, "capture alignment exceeds storage");
    new (storage) stored(std::forward<F>(fn));
    invoke = [](void* self) noexcept { (*static_cast<stored*>(self))(); };
    destroy = [](void* self) noexcept
    {
        if constexpr (!std::is_trivially_destructible_v<stored>)
        {
            static_cast<stored*>(self)->~stored();
        }
        else
        {
            (void)self;
        }
    };
    return callable_placement::inline_storage;
}

template <std::size_t StorageSize, std::size_t StorageAlign, typename F>
callable_placement callable_bind_heap(void* storage, F&& fn, callable_invoke_fn& invoke,
                                      callable_destroy_fn& destroy) noexcept
{
    using stored = std::decay_t<F>;
    static_assert(sizeof(void*) <= StorageSize, "storage must hold an object pointer");
    stored* object = new (std::nothrow) stored(std::forward<F>(fn));
    if (object == nullptr)
    {
        invoke = nullptr;
        destroy = nullptr;
        return callable_placement::none;
    }
    std::memcpy(storage, &object, sizeof(object));
    invoke = [](void* self) noexcept
    {
        void* pointer = nullptr;
        std::memcpy(&pointer, self, sizeof(pointer));
        (*static_cast<stored*>(pointer))();
    };
    destroy = [](void* self) noexcept
    {
        void* pointer = nullptr;
        std::memcpy(&pointer, self, sizeof(pointer));
        static_cast<stored*>(pointer)->~stored();
        ::operator delete(pointer);
    };
    return callable_placement::heap;
}

// 自动选择：平凡可拷贝、平凡析构且能放下才内联，否则堆
template <std::size_t StorageSize, std::size_t StorageAlign, typename F>
callable_placement callable_bind(void* storage, F&& fn, callable_invoke_fn& invoke,
                                 callable_destroy_fn& destroy) noexcept
{
    using stored = std::decay_t<F>;
    if constexpr (sizeof(stored) <= StorageSize && alignof(stored) <= StorageAlign &&
                  std::is_trivially_copyable_v<stored> && std::is_trivially_destructible_v<stored>)
    {
        return callable_bind_inline<StorageSize, StorageAlign, F>(storage, std::forward<F>(fn),
                                                                  invoke, destroy);
    }
    else
    {
        return callable_bind_heap<StorageSize, StorageAlign, F>(storage, std::forward<F>(fn),
                                                                invoke, destroy);
    }
}

template <std::size_t InlineSize = 64, std::size_t InlineAlign = alignof(std::max_align_t)>
class inline_callable
{
    static_assert(InlineSize >= sizeof(void*), "inline storage must hold a pointer");

public:
    inline_callable() noexcept = default;

    ~inline_callable()
    {
        reset();
    }

    inline_callable(const inline_callable&) = delete;
    inline_callable& operator=(const inline_callable&) = delete;

    inline_callable(inline_callable&& other) noexcept
    {
        move_from(other);
    }

    inline_callable& operator=(inline_callable&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            move_from(other);
        }
        return *this;
    }

    template <typename F> bool bind(F&& fn) noexcept
    {
        reset();
        placement_ = callable_bind<InlineSize, InlineAlign, F>(storage_, std::forward<F>(fn),
                                                               invoke_, destroy_);
        return placement_ != callable_placement::none;
    }

    void operator()() noexcept
    {
        if (invoke_ != nullptr)
        {
            invoke_(storage_);
        }
    }

    explicit operator bool() const noexcept
    {
        return placement_ != callable_placement::none;
    }

    bool heap_allocated() const noexcept
    {
        return placement_ == callable_placement::heap;
    }

    void reset() noexcept
    {
        if (destroy_ != nullptr)
        {
            destroy_(storage_);
        }
        invoke_ = nullptr;
        destroy_ = nullptr;
        placement_ = callable_placement::none;
    }

private:
    void move_from(inline_callable& other) noexcept
    {
        std::memcpy(storage_, other.storage_, sizeof(storage_));
        invoke_ = other.invoke_;
        destroy_ = other.destroy_;
        placement_ = other.placement_;
        other.invoke_ = nullptr;
        other.destroy_ = nullptr;
        other.placement_ = callable_placement::none;
    }

    alignas(InlineAlign) unsigned char storage_[InlineSize]{};
    callable_invoke_fn invoke_{nullptr};
    callable_destroy_fn destroy_{nullptr};
    callable_placement placement_{callable_placement::none};
};
