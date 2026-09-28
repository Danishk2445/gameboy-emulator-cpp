#pragma once

#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

#include "common.hpp"

namespace gb {

// Symmetric binary serializer: every component implements one
// serialize(StateIO&) that is used for both saving and loading.
class StateIO {
public:
    static StateIO writer() { return StateIO(false, {}); }
    static StateIO reader(std::vector<u8> data) { return StateIO(true, std::move(data)); }

    bool loading() const { return loading_; }
    bool ok() const { return ok_; }
    bool at_end() const { return pos_ == buf_.size(); }
    void fail() { ok_ = false; }
    const std::vector<u8>& data() const { return buf_; }

    void raw(void* p, size_t n) {
        if (loading_) {
            if (!ok_ || pos_ + n > buf_.size()) {
                ok_ = false;
                return;
            }
            std::memcpy(p, buf_.data() + pos_, n);
            pos_ += n;
        } else {
            const auto* b = static_cast<const u8*>(p);
            buf_.insert(buf_.end(), b, b + n);
        }
    }

    template <class T>
    void operator()(T& v) {
        static_assert(std::is_trivially_copyable_v<T>, "StateIO only handles trivially copyable types");
        raw(&v, sizeof(T));
    }

    // Byte vector whose size is fixed by the loaded ROM; a size mismatch fails the load.
    void vec(std::vector<u8>& v) {
        u32 n = static_cast<u32>(v.size());
        (*this)(n);
        if (loading_ && n != v.size()) {
            ok_ = false;
            return;
        }
        if (!v.empty()) raw(v.data(), v.size());
    }

private:
    StateIO(bool loading, std::vector<u8> data) : loading_(loading), buf_(std::move(data)) {}

    bool loading_;
    std::vector<u8> buf_;
    size_t pos_ = 0;
    bool ok_ = true;
};

}  // namespace gb
