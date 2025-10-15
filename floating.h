#pragma once

#include <cstdint>
#include <string>
#include <iostream>

enum class fp_format {
    half,
    single
};

enum class rounding_mode {
    to_zero,
    to_nearest_even,
    to_positive_infinity,
    to_negative_infinity
};

class fp_value {
public:
    int64_t exponent{};
    int exponent_width{};
    int mantissa_width{};
    int64_t mantissa{};
    int64_t raw_bits{};

    fp_format format{};
    rounding_mode rounding{rounding_mode::to_zero};
    bool is_negative{false};

    bool is_nan{false};
    bool is_positive_zero{false};
    bool is_negative_zero{false};
    bool is_positive_infinity{false};
    bool is_negative_infinity{false};
    bool is_subnormal{false};

    fp_value() = default;
    fp_value(int64_t initial_bits, fp_format fmt, rounding_mode rnd);

    fp_value operator+(const fp_value &other) const;
    fp_value operator-(const fp_value &other) const;
    fp_value operator*(const fp_value &other) const;
    fp_value operator/(const fp_value &other) const;

    friend std::ostream &operator<<(std::ostream &os, const fp_value &val);
};

fp_value make_quiet_nan(fp_value ref);
fp_value make_positive_infinity(fp_value ref);
fp_value make_negative_infinity(fp_value ref);
fp_value make_positive_zero(fp_value ref);
fp_value make_negative_zero(fp_value ref);

fp_value multiply_then_add(const fp_value &a, const fp_value &b, const fp_value &c);
fp_value fused_multiply_add(const fp_value &a, const fp_value &b, const fp_value &c);

bool needs_rounding(rounding_mode mode, int64_t lost_count, int64_t lost_value, int64_t current_mantissa, bool is_result_negative);
void exit_with_error_message(const std::string& msg);
int64_t parse_hex(std::string hex_str);