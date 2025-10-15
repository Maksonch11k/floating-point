#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

class uint128_t {
public:
    uint64_t high;
    uint64_t low;

    uint128_t() : high(0), low(0) {}
    uint128_t(uint64_t l) : high(0), low(l) {}
    uint128_t(uint64_t h, uint64_t l) : high(h), low(l) {}

    uint128_t operator+(const uint128_t& other) const {
        uint64_t new_low = low + other.low;
        uint64_t new_high = high + other.high + (new_low < low);
        return uint128_t(new_high, new_low);
    }

    uint128_t operator-(const uint128_t& other) const {
        uint64_t new_low = low - other.low;
        uint64_t new_high = high - other.high - (new_low > low);
        return uint128_t(new_high, new_low);
    }

    uint128_t operator/(uint64_t divisor) const {
        if (divisor == 0) {
            return uint128_t(0);
        }

        uint128_t quotient(0);
        uint128_t remainder(0);
        uint128_t divisor_128(0, divisor);

        for (int i = 127; i >= 0; --i) {
            remainder <<= 1;
            bool bit_is_set = (i >= 64) ? ((high >> (i - 64)) & 1) : ((low >> i) & 1);
            if (bit_is_set) {
                remainder.low |= 1;
            }

            if (remainder >= divisor_128) {
                remainder = remainder - divisor_128;
                if (i >= 64) {
                    quotient.high |= (1ULL << (i - 64));
                } else {
                    quotient.low |= (1ULL << i);
                }
            }
        }
        return quotient;
    }

    uint128_t operator<<(int shift) const {
        if (shift == 0) return *this;
        if (shift >= 128) return uint128_t(0);
        if (shift >= 64) {
            return uint128_t(low << (shift - 64), 0);
        }
        uint64_t new_high = (high << shift) | (low >> (64 - shift));
        uint64_t new_low = low << shift;
        return uint128_t(new_high, new_low);
    }

    uint128_t operator>>(int shift) const {
        if (shift == 0) return *this;
        if (shift >= 128) return uint128_t(0);
        if (shift >= 64) {
            return uint128_t(0, high >> (shift - 64));
        }
        uint64_t new_low = (low >> shift) | (high << (64 - shift));
        uint64_t new_high = high >> shift;
        return uint128_t(new_high, new_low);
    }

    uint128_t& operator<<=(int shift) { *this = *this << shift; return *this; }
    uint128_t& operator>>=(int shift) { *this = *this >> shift; return *this; }
    uint128_t operator&(const uint128_t& other) const { return uint128_t(high & other.high, low & other.low); }
    uint128_t operator~() const { return uint128_t(~high, ~low); }

    bool operator==(const uint128_t& other) const { return high == other.high && low == other.low; }
    bool operator!=(const uint128_t& other) const { return !(*this == other); }
    bool operator<(const uint128_t& other) const { return high < other.high || (high == other.high && low < other.low); }
    bool operator>=(const uint128_t& other) const { return !(*this < other); }

    explicit operator uint64_t() const { return low; }

    static uint128_t multiply_u64(uint64_t a, uint64_t b) {
        uint64_t a0 = a & 0xFFFFFFFF;
        uint64_t a1 = a >> 32;
        uint64_t b0 = b & 0xFFFFFFFF;
        uint64_t b1 = b >> 32;

        uint64_t p0 = a0 * b0;
        uint64_t p1 = a0 * b1;
        uint64_t p2 = a1 * b0;
        uint64_t p3 = a1 * b1;

        uint64_t p1_low = p1 & 0xFFFFFFFF;
        uint64_t p1_high = p1 >> 32;
        uint64_t p2_low = p2 & 0xFFFFFFFF;
        uint64_t p2_high = p2 >> 32;

        uint64_t mid_sum = p1_low + p2_low + (p0 >> 32);
        uint64_t low_part = (mid_sum << 32) | (p0 & 0xFFFFFFFF);
        uint64_t high_part = p3 + p1_high + p2_high + (mid_sum >> 32);

        return uint128_t(high_part, low_part);
    }
};

class int128_t {
public:
    uint128_t value;

    int128_t() {}
    int128_t(int64_t val) {
        if (val < 0) {
            value.low = -val;
            value.high = 0;
            value = (~value) + uint128_t(1);
        } else {
            value.low = val;
            value.high = 0;
        }
    }
    int128_t(const uint128_t& val) : value(val) {}

    int128_t operator-() const {
        return int128_t((~value) + uint128_t(1));
    }
    int128_t operator+(const int128_t& other) const {
        return int128_t(value + other.value);
    }
    bool is_negative() const {
        return (value.high >> 63) & 1;
    }
    bool operator<(const int128_t& other) const {
        bool this_neg = is_negative();
        bool other_neg = other.is_negative();
        if (this_neg != other_neg) {
            return this_neg;
        }
        return value < other.value;
    }
};