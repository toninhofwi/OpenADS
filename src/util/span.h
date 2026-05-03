#pragma once

#include <cstddef>
#include <cassert>
#include <type_traits>

namespace openads::util {

template <class T>
class Span {
public:
    using element_type = T;
    using value_type   = std::remove_cv_t<T>;
    using size_type    = std::size_t;
    using pointer      = T*;
    using reference    = T&;
    using iterator     = T*;

    Span() noexcept = default;
    Span(T* data, size_type n) noexcept : data_(data), size_(n) {}

    pointer    data()  const noexcept { return data_; }
    size_type  size()  const noexcept { return size_; }
    bool       empty() const noexcept { return size_ == 0; }

    reference operator[](size_type i) const noexcept {
        assert(i < size_);
        return data_[i];
    }

    iterator begin() const noexcept { return data_; }
    iterator end()   const noexcept { return data_ + size_; }

    Span subspan(size_type offset) const noexcept {
        assert(offset <= size_);
        return Span(data_ + offset, size_ - offset);
    }
    Span subspan(size_type offset, size_type count) const noexcept {
        assert(offset + count <= size_);
        return Span(data_ + offset, count);
    }

private:
    T*        data_ = nullptr;
    size_type size_ = 0;
};

} // namespace openads::util
