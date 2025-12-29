export module Core:Types;

import std;
import fmt;

export using $u8 = std::uint8_t;
export using $u16 = std::uint16_t;
export using $u32 = std::uint32_t;
export using $u64 = std::uint64_t;


export using $i8 = std::int8_t;
export using $i16 = std::int16_t;
export using $i32 = std::int32_t;
export using $i64 = std::int64_t;


export using $f32 = float;
export using $f64 = double;
export using $f128 = long double;

export using $c = char;
export using $c8 = char8_t;
export using $c16 = char16_t;
export using $c32 = char32_t;
export using $wchar = wchar_t;

export using $s = std::string;
export using $sv = std::string_view;
export using $string = std::string;
export using $stringv = std::string_view;
export using $ws = std::wstring;
export using $wsv = std::wstring_view;
export using $wstring = std::wstring;
export using $wstringv = std::wstring_view;
export using $u8string = std::u8string;
export using $u8stringv = std::u8string_view;
export using $u16string = std::u16string;
export using $u16stringv = std::u16string_view;
export using $u32string = std::u32string;
export using $u32stringv = std::u32string_view;

export template <typename... T> using $format_string = fmt::format_string<T...>;

export using $size = std::size_t;
export using $ptrdiff = std::ptrdiff_t;
export using $uintptr = std::uintptr_t;
export using $intptr = std::intptr_t;

export using $bool = bool;

export using $byte = std::byte;

export using $void = void;
export using $pointer = void *;

export struct $u128 {
    std::uint64_t low;
    std::uint64_t high;

    constexpr $u128() : low(0), high(0) {
    }

    constexpr $u128(const std::uint64_t val) : low(val), high(0) {
    }

    constexpr $u128(const std::uint64_t h, const std::uint64_t l) : low(l), high(h) {
    }

    constexpr $u128 operator+(const $u128 &other) const {
        $u128 result;
        result.low = low + other.low;
        result.high = high + other.high + (result.low < low ? 1 : 0);
        return result;
    }

    constexpr $u128 operator-(const $u128 &other) const {
        $u128 result;
        result.low = low - other.low;
        result.high = high - other.high - (low < other.low ? 1 : 0);
        return result;
    }

    constexpr $u128 operator*(const $u128 &other) const {
        const std::uint64_t a_low = low & 0xFFFFFFFF;
        const std::uint64_t a_high = low >> 32;
        const std::uint64_t b_low = other.low & 0xFFFFFFFF;
        const std::uint64_t b_high = other.low >> 32;

        const std::uint64_t p0 = a_low * b_low;
        const std::uint64_t p1 = a_low * b_high;
        const std::uint64_t p2 = a_high * b_low;
        const std::uint64_t p3 = a_high * b_high;

        const std::uint64_t middle = p1 + (p0 >> 32) + (p2 & 0xFFFFFFFF);

        $u128 result;
        result.low = (middle << 32) | (p0 & 0xFFFFFFFF);
        result.high = p3 + (middle >> 32) + (p2 >> 32);
        return result;
    }

    constexpr $u128 operator/(const $u128 &other) const {
        if (other.high == 0 && high == 0) {
            return $u128(low / other.low);
        }
        return $u128(0);
    }

    constexpr $u128 operator%(const $u128 &other) const {
        if (other.high == 0 && high == 0) {
            return $u128(low % other.low);
        }
        return $u128(0);
    }

    constexpr $u128 operator&(const $u128 &other) const {
        return $u128(high & other.high, low & other.low);
    }

    constexpr $u128 operator|(const $u128 &other) const {
        return $u128(high | other.high, low | other.low);
    }

    constexpr $u128 operator^(const $u128 &other) const {
        return $u128(high ^ other.high, low ^ other.low);
    }

    constexpr $u128 operator~() const {
        return $u128(~high, ~low);
    }

    constexpr $u128 operator<<(const int shift) const {
        if (shift >= 128) return $u128(0);
        if (shift >= 64) return $u128(low << (shift - 64), 0);
        if (shift == 0) return *this;
        return $u128((high << shift) | (low >> (64 - shift)), low << shift);
    }

    constexpr $u128 operator>>(const int shift) const {
        if (shift >= 128) return $u128(0);
        if (shift >= 64) return $u128(0, high >> (shift - 64));
        if (shift == 0) return *this;
        return $u128(high >> shift, (low >> shift) | (high << (64 - shift)));
    }

    constexpr bool operator==(const $u128 &other) const {
        return low == other.low && high == other.high;
    }

    constexpr bool operator!=(const $u128 &other) const {
        return !(*this == other);
    }

    constexpr bool operator<(const $u128 &other) const {
        return high < other.high || (high == other.high && low < other.low);
    }

    constexpr bool operator>(const $u128 &other) const {
        return other < *this;
    }

    constexpr bool operator<=(const $u128 &other) const {
        return !(*this > other);
    }

    constexpr bool operator>=(const $u128 &other) const {
        return !(*this < other);
    }
};

export struct $u256 {
    $u128 low;
    $u128 high;

    constexpr $u256() : low(), high() {
    }

    constexpr $u256(const std::uint64_t val) : low(val), high() {
    }

    constexpr $u256(const $u128 h, const $u128 l) : low(l), high(h) {
    }

    constexpr $u256 operator+(const $u256 &other) const {
        $u256 result;
        result.low = low + other.low;
        const bool carry = result.low < low;
        result.high = high + other.high;
        if (carry) result.high = result.high + $u128(1);
        return result;
    }

    constexpr $u256 operator-(const $u256 &other) const {
        $u256 result;
        result.low = low - other.low;
        const bool borrow = low < other.low;
        result.high = high - other.high;
        if (borrow) result.high = result.high - $u128(1);
        return result;
    }

    constexpr $u256 operator&(const $u256 &other) const {
        return $u256(high & other.high, low & other.low);
    }

    constexpr $u256 operator|(const $u256 &other) const {
        return $u256(high | other.high, low | other.low);
    }

    constexpr $u256 operator^(const $u256 &other) const {
        return $u256(high ^ other.high, low ^ other.low);
    }

    constexpr $u256 operator~() const {
        return $u256(~high, ~low);
    }

    constexpr bool operator==(const $u256 &other) const {
        return low == other.low && high == other.high;
    }

    constexpr bool operator!=(const $u256 &other) const {
        return !(*this == other);
    }

    constexpr bool operator<(const $u256 &other) const {
        return high < other.high || (high == other.high && low < other.low);
    }

    constexpr bool operator>(const $u256 &other) const {
        return other < *this;
    }

    constexpr bool operator<=(const $u256 &other) const {
        return !(*this > other);
    }

    constexpr bool operator>=(const $u256 &other) const {
        return !(*this < other);
    }
};


export struct $i128 {
    std::uint64_t low;
    std::int64_t high;

    constexpr $i128() : low(0), high(0) {
    }

    constexpr $i128(const std::int64_t val) : low(val), high(val < 0 ? -1 : 0) {
    }

    constexpr $i128(const std::int64_t h, const std::uint64_t l) : low(l), high(h) {
    }

    constexpr $i128 operator+(const $i128 &other) const {
        $i128 result;
        result.low = low + other.low;
        result.high = high + other.high + (result.low < low ? 1 : 0);
        return result;
    }

    constexpr $i128 operator-(const $i128 &other) const {
        $i128 result;
        result.low = low - other.low;
        result.high = high - other.high - (low < other.low ? 1 : 0);
        return result;
    }

    constexpr $i128 operator-() const {
        $i128 result;
        result.low = ~low + 1;
        result.high = ~high + (result.low == 0 ? 1 : 0);
        return result;
    }

    constexpr $i128 operator&(const $i128 &other) const {
        return $i128(high & other.high, low & other.low);
    }

    constexpr $i128 operator|(const $i128 &other) const {
        return $i128(high | other.high, low | other.low);
    }

    constexpr $i128 operator^(const $i128 &other) const {
        return $i128(high ^ other.high, low ^ other.low);
    }

    constexpr $i128 operator~() const {
        return $i128(~high, ~low);
    }

    constexpr bool operator==(const $i128 &other) const {
        return low == other.low && high == other.high;
    }

    constexpr bool operator!=(const $i128 &other) const {
        return !(*this == other);
    }

    constexpr bool operator<(const $i128 &other) const {
        return high < other.high || (high == other.high && low < other.low);
    }

    constexpr bool operator>(const $i128 &other) const {
        return other < *this;
    }

    constexpr bool operator<=(const $i128 &other) const {
        return !(*this > other);
    }

    constexpr bool operator>=(const $i128 &other) const {
        return !(*this < other);
    }
};

export struct $i256 {
    $u128 low;
    $i128 high;

    constexpr $i256() : low(), high() {
    }

    constexpr $i256(const std::int64_t val) : low(val), high(val < 0 ? $i128(-1) : $i128(0)) {
    }

    constexpr $i256(const $i128 h, const $u128 l) : low(l), high(h) {
    }

    constexpr $i256 operator+(const $i256 &other) const {
        $i256 result;
        result.low = low + other.low;
        const bool carry = result.low < low;
        result.high = high + other.high;
        if (carry) result.high = result.high + $i128(1);
        return result;
    }

    constexpr $i256 operator-(const $i256 &other) const {
        $i256 result;
        result.low = low - other.low;
        const bool borrow = low < other.low;
        result.high = high - other.high;
        if (borrow) result.high = result.high - $i128(1);
        return result;
    }

    constexpr $i256 operator-() const {
        $i256 result;
        result.low = ~low;
        result.high = ~high;
        return result + $i256(1);
    }

    constexpr bool operator==(const $i256 &other) const {
        return low == other.low && high == other.high;
    }

    constexpr bool operator!=(const $i256 &other) const {
        return !(*this == other);
    }

    constexpr bool operator<(const $i256 &other) const {
        return high < other.high || (high == other.high && low < other.low);
    }

    constexpr bool operator>(const $i256 &other) const {
        return other < *this;
    }

    constexpr bool operator<=(const $i256 &other) const {
        return !(*this > other);
    }

    constexpr bool operator>=(const $i256 &other) const {
        return !(*this < other);
    }
};
