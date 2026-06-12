// Open New Vegas — little-endian binary reader over a byte buffer.

#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace onv {

class BinaryReader {
public:
    BinaryReader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    explicit BinaryReader(const std::vector<std::uint8_t>& buf)
        : BinaryReader(buf.data(), buf.size()) {}

    std::size_t pos() const { return pos_; }
    std::size_t size() const { return size_; }
    std::size_t remaining() const { return size_ - pos_; }
    bool eof() const { return pos_ >= size_; }

    void seek(std::size_t p) {
        if (p > size_) throw std::runtime_error("seek past end of buffer");
        pos_ = p;
    }

    void skip(std::size_t n) { seek(pos_ + n); }

    template <typename T>
    T read() {
        static_assert(std::is_trivially_copyable_v<T>);
        require(sizeof(T));
        T v;
        std::memcpy(&v, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return v;
    }

    void readBytes(void* out, std::size_t n) {
        require(n);
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
    }

    std::vector<std::uint8_t> readVector(std::size_t n) {
        std::vector<std::uint8_t> v(n);
        if (n) readBytes(v.data(), n);
        return v;
    }

    // 4-byte tag like "BSA\0" or "GRUP"
    std::string readTag() {
        char t[4];
        readBytes(t, 4);
        return std::string(t, 4);
    }

    // Length-prefixed string, length byte includes the trailing NUL (bzstring)
    std::string readBzString() {
        const auto len = read<std::uint8_t>();
        require(len);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        while (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    }

    // NUL-terminated string
    std::string readZString() {
        std::string s;
        while (true) {
            const auto c = read<char>();
            if (c == '\0') break;
            s.push_back(c);
        }
        return s;
    }

private:
    void require(std::size_t n) const {
        if (pos_ + n > size_) throw std::runtime_error("read past end of buffer");
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

} // namespace onv
