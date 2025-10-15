#include "floating.h"
#include "uint128.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace {
    static inline int countl_zero_64(uint64_t val) {
        if (val == 0) return 64;
        int count = 0;
        for (int i = 63; i >= 0; --i) {
            if (!((val >> i) & 1ULL)) {
                count++;
            } else {
                break;
            }
        }
        return count;
    }

    static inline int bit_width_128(uint128_t val) {
        if (val.high == 0) {
            if (val.low == 0) return 0;
            return 64 - countl_zero_64(val.low);
        }
        return 128 - countl_zero_64(val.high);
    }
}

fp_value::fp_value(int64_t initial_bits, fp_format fmt, rounding_mode rnd) {
    this->format = fmt;
    this->rounding = rnd;
    this->raw_bits = initial_bits;

    if (fmt == fp_format::half) {
        exponent_width = 5;
        mantissa_width = 10;
    } else {
        exponent_width = 8;
        mantissa_width = 23;
    }

    const int64_t sign_pos = exponent_width + mantissa_width;
    const int64_t exp_mask = (1LL << exponent_width) - 1;
    const int64_t mant_mask = (1LL << mantissa_width) - 1;
    const int64_t exp_bias = (1LL << (exponent_width - 1)) - 1;

    is_negative = ((initial_bits >> sign_pos) & 1LL) != 0;

    const int64_t raw_exp = (initial_bits >> mantissa_width) & exp_mask;
    const int64_t fraction = initial_bits & mant_mask;

    if (raw_exp == 0) {
        if (fraction != 0) is_subnormal = true;
        else is_negative ? is_negative_zero = true : is_positive_zero = true;
    }

    if (raw_exp == exp_mask) {
        if (fraction != 0) is_nan = true;
        else is_negative ? is_negative_infinity = true : is_positive_infinity = true;
    }

    exponent = (is_positive_zero || is_negative_zero) ? 0 : raw_exp - exp_bias;

    if (!is_subnormal) {
        mantissa = (1LL << mantissa_width) + fraction;
    } else {
        mantissa = fraction;
        if (mantissa != 0) {
            int64_t len = 64 - countl_zero_64(static_cast<uint64_t>(mantissa));
            int64_t shift = mantissa_width - len + 1;
            if (shift > 0) mantissa <<= shift;
            exponent = 1 - exp_bias - shift;
        } else {
            exponent = 1 - exp_bias;
        }
    }
}

fp_value fp_value::operator+(const fp_value &other) const {
    const fp_value &lhs = *this;
    const fp_value &rhs = other;

    if (lhs.is_nan) return lhs;
    if (rhs.is_nan) return rhs;
    if (lhs.is_negative_zero) return rhs;
    if (lhs.is_positive_zero) return rhs.is_negative_zero ? lhs : rhs;
    if (rhs.is_negative_zero || rhs.is_positive_zero) return lhs;
    if ((lhs.is_negative_infinity && rhs.is_positive_infinity) || (lhs.is_positive_infinity && rhs.is_negative_infinity)) return make_quiet_nan(lhs);
    if (lhs.is_negative_infinity || lhs.is_positive_infinity) return lhs;
    if (rhs.is_negative_infinity || rhs.is_positive_infinity) return rhs;

    int m_bits = lhs.mantissa_width;
    int e_bits = lhs.exponent_width;
    const int64_t bias = (1LL << (e_bits - 1)) - 1;

    int64_t common_exp = std::min(lhs.exponent, rhs.exponent);

    uint128_t term1_val = uint128_t(lhs.mantissa) << (lhs.exponent - common_exp);
    int128_t term1 = lhs.is_negative ? -int128_t(term1_val) : int128_t(term1_val);
    
    uint128_t term2_val = uint128_t(rhs.mantissa) << (rhs.exponent - common_exp);
    int128_t term2 = rhs.is_negative ? -int128_t(term2_val) : int128_t(term2_val);

    int128_t sum_mantissa_128 = term1 + term2;
    bool sum_is_negative = sum_mantissa_128.is_negative();
    
    uint128_t sum_mantissa_unsigned = sum_is_negative ? (-sum_mantissa_128).value : sum_mantissa_128.value;
    int sum_len = bit_width_128(sum_mantissa_unsigned);

    int64_t sum_exp_field = common_exp + sum_len - (m_bits + 1) + bias;
    int64_t lost_count = sum_len - (m_bits + 1);
    
    if (sum_exp_field < 1) {
        lost_count += (1 - sum_exp_field);
        sum_exp_field = 0;
    }

    uint64_t lost_value = 0;
    if (lost_count > 0) {
        uint128_t mask = (uint128_t(1) << lost_count) - uint128_t(1);
        lost_value = static_cast<uint64_t>((sum_mantissa_unsigned & mask).low);
        sum_mantissa_unsigned >>= lost_count;
    }
    
    uint64_t sum_mantissa = static_cast<uint64_t>(sum_mantissa_unsigned.low);

    if (needs_rounding(this->rounding, lost_count, lost_value, sum_mantissa, sum_is_negative)) {
        sum_mantissa++;
    }

    int final_len = 64 - countl_zero_64(sum_mantissa);
    if (final_len > 1 + m_bits) {
        sum_mantissa >>= (final_len - 1 - m_bits);
        sum_exp_field += (final_len - 1 - m_bits);
    }
    
    if (sum_mantissa == 0) {
        return (this->rounding == rounding_mode::to_negative_infinity) ? make_negative_zero(lhs) : make_positive_zero(lhs);
    }

    if (sum_exp_field >= (1LL << e_bits) - 1) {
        return sum_is_negative ? make_negative_infinity(lhs) : make_positive_infinity(lhs);
    }

    int64_t final_bits = (sum_mantissa & ((1LL << m_bits) - 1)) + (sum_exp_field << m_bits);
    if (sum_is_negative) final_bits += (1LL << (m_bits + e_bits));
    
    return fp_value(final_bits, lhs.format, lhs.rounding);
}

fp_value fp_value::operator-(const fp_value &other) const {
    int shift = other.exponent_width + other.mantissa_width;
    int64_t sign_flipped_bits = other.raw_bits ^ (1LL << shift);
    fp_value negated_other(sign_flipped_bits, other.format, this->rounding);
    return *this + negated_other;
}

fp_value fp_value::operator*(const fp_value &other) const {
    const fp_value &lhs = *this;
    const fp_value &rhs = other;

    if (lhs.is_nan) return lhs;
    if (rhs.is_nan) return rhs;

    bool result_is_negative = lhs.is_negative ^ rhs.is_negative;

    if ((lhs.is_positive_zero || lhs.is_negative_zero) && (rhs.is_positive_infinity || rhs.is_negative_infinity)) return make_quiet_nan(lhs);
    if ((lhs.is_positive_infinity || lhs.is_negative_infinity) && (rhs.is_positive_zero || rhs.is_negative_zero)) return make_quiet_nan(lhs);
    if (lhs.is_positive_zero || lhs.is_negative_zero || rhs.is_positive_zero || rhs.is_negative_zero) return result_is_negative ? make_negative_zero(lhs) : make_positive_zero(lhs);
    if (lhs.is_positive_infinity || lhs.is_negative_infinity || rhs.is_positive_infinity || rhs.is_negative_infinity) return result_is_negative ? make_negative_infinity(lhs) : make_positive_infinity(lhs);

    int m_bits = lhs.mantissa_width;
    int e_bits = lhs.exponent_width;

    uint128_t prod_mantissa = uint128_t::multiply_u64(lhs.mantissa, rhs.mantissa);
    int bit_len = bit_width_128(prod_mantissa);
    
    int64_t lost_count = bit_len - (m_bits + 1);
    int64_t result_exp_field = lhs.exponent + rhs.exponent;
    if (lost_count > m_bits) {
        result_exp_field += lost_count - m_bits;
    }
    result_exp_field += ((1LL << (e_bits - 1)) - 1);
    
    if (result_exp_field < 1) {
        lost_count += (1 - result_exp_field);
        result_exp_field = 0;
    }

    uint64_t result_mantissa = 0;
    uint64_t lost_value = 0;
    if (lost_count > 0) {
        uint128_t mask = (uint128_t(1) << lost_count) - uint128_t(1);
        lost_value = static_cast<uint64_t>((prod_mantissa & mask).low);
        prod_mantissa >>= lost_count;
        result_mantissa = static_cast<uint64_t>(prod_mantissa.low);
    } else {
        result_mantissa = static_cast<uint64_t>(prod_mantissa.low);
    }

    if (needs_rounding(this->rounding, lost_count, lost_value, result_mantissa, result_is_negative)) {
        result_mantissa++;
    }

    int len_after = 64 - countl_zero_64(result_mantissa);
    if (len_after - 1 > m_bits) {
        result_exp_field += (len_after - 1 - m_bits);
        result_mantissa >>= (len_after - 1 - m_bits);
    }

    if (result_exp_field >= (1LL << e_bits) - 1) {
        return result_is_negative ? make_negative_infinity(lhs) : make_positive_infinity(lhs);
    }
    
    int64_t final_bits = (result_mantissa & ((1LL << m_bits) - 1)) + (result_exp_field << m_bits);
    if (result_is_negative) final_bits += (1LL << (m_bits + e_bits));
    
    return fp_value(final_bits, lhs.format, lhs.rounding);
}

fp_value fp_value::operator/(const fp_value &other) const {
    const fp_value &lhs = *this;
    const fp_value &rhs = other;
    if (lhs.is_nan) return lhs;
    if (rhs.is_nan) return rhs;

    bool result_is_negative = lhs.is_negative ^ rhs.is_negative;

    if (rhs.is_positive_zero || rhs.is_negative_zero) return (lhs.is_positive_zero || lhs.is_negative_zero) ? make_quiet_nan(lhs) : (result_is_negative ? make_negative_infinity(lhs) : make_positive_infinity(lhs));
    if (lhs.is_positive_zero || lhs.is_negative_zero) return result_is_negative ? make_negative_zero(lhs) : make_positive_zero(lhs);
    if (lhs.is_positive_infinity || lhs.is_negative_infinity) return (rhs.is_positive_infinity || rhs.is_negative_infinity) ? make_quiet_nan(lhs) : (result_is_negative ? make_negative_infinity(lhs) : make_positive_infinity(lhs));
    if (rhs.is_positive_infinity || rhs.is_negative_infinity) return result_is_negative ? make_negative_zero(lhs) : make_positive_zero(lhs);

    int m_bits = lhs.mantissa_width;
    int e_bits = lhs.exponent_width;

    int64_t result_exp_field = lhs.exponent - rhs.exponent + ((1LL << (e_bits - 1)) - 1);
    
    int precision_shift = 4 * (m_bits + 2);
    uint128_t num = uint128_t(lhs.mantissa) << precision_shift;
    uint128_t quot = num / rhs.mantissa;

    int bit_len = bit_width_128(quot);
    if (bit_len == 0) bit_len = 1;

    result_exp_field -= ((precision_shift + 1) - bit_len);

    int64_t lost_count = bit_len - m_bits - 1;
    if (result_exp_field < 1) {
        lost_count += (1 - result_exp_field);
        result_exp_field = 0;
    }

    uint64_t result_mantissa;
    uint64_t lost_value = 0;
    if (lost_count > 0) {
        uint128_t mask = (uint128_t(1) << lost_count) - uint128_t(1);
        lost_value = (uint64_t)((quot & mask).low);
        quot >>= lost_count;
    }
    result_mantissa = (uint64_t)quot.low;
    
    if (needs_rounding(this->rounding, lost_count, lost_value, result_mantissa, result_is_negative)) {
        result_mantissa++;
    }

    if (result_exp_field >= (1LL << e_bits) - 1) {
        return result_is_negative ? make_negative_infinity(lhs) : make_positive_infinity(lhs);
    }
    
    int64_t final_bits = (result_mantissa & ((1LL << m_bits) - 1)) + (result_exp_field << m_bits);
    if (result_is_negative) final_bits += (1LL << (m_bits + e_bits));

    return fp_value(final_bits, lhs.format, lhs.rounding);
}

bool needs_rounding(rounding_mode mode, int64_t lost_count, int64_t lost_value, int64_t current_mantissa, bool is_result_negative) {
    if (lost_count <= 0) return false;
    
    bool increment = false;
    switch (mode) {
        case rounding_mode::to_nearest_even: {
            bool is_tie = (lost_value == (1LL << (lost_count - 1)));
            bool is_midpoint = (lost_value >> (lost_count - 1)) & 1LL;
            if (is_midpoint) {
                if (!is_tie || (current_mantissa & 1LL)) {
                    increment = true;
                }
            }
            break;
        }
        case rounding_mode::to_positive_infinity:
            if (!is_result_negative && lost_value != 0) increment = true;
            break;
        case rounding_mode::to_negative_infinity:
            if (is_result_negative && lost_value != 0) increment = true;
            break;
        case rounding_mode::to_zero:
        default:
            break;
    }
    return increment;
}

void exit_with_error_message(const std::string& msg) {
    std::cerr << msg << std::endl;
    exit(1);
}

int64_t parse_hex(std::string hex_str) {
    if (hex_str.rfind("0x", 0) == 0 || hex_str.rfind("0X", 0) == 0) {
        hex_str = hex_str.substr(2);
    }
    int64_t res = 0;
    std::stringstream ss;
    ss << std::hex << hex_str;
    ss >> res;
    return res & 0xFFFFFFFFLL;
}

fp_value make_quiet_nan(fp_value ref) {
    return fp_value(((1 << (ref.exponent_width + 2)) - 1) << (ref.mantissa_width - 1), ref.format, ref.rounding);
}
fp_value make_positive_infinity(fp_value ref) {
    return fp_value(((1 << ref.exponent_width) - 1) << ref.mantissa_width, ref.format, ref.rounding);
}
fp_value make_negative_infinity(fp_value ref) {
    return fp_value(((1 << (ref.exponent_width + 1)) - 1) << ref.mantissa_width, ref.format, ref.rounding);
}
fp_value make_positive_zero(fp_value ref) {
    return fp_value(0, ref.format, ref.rounding);
}
fp_value make_negative_zero(fp_value ref) {
    return fp_value(1 << (ref.exponent_width + ref.mantissa_width), ref.format, ref.rounding);
}

std::ostream &operator<<(std::ostream &os, const fp_value &val) {
    int total_bits = val.exponent_width + val.mantissa_width + 1;
    int hex_digits = (total_bits + 3) / 4;
    std::ostringstream hex_stream;
    hex_stream << std::hex << std::uppercase << (val.raw_bits & ((1ULL << (total_bits)) - 1));
    std::string hex_str = hex_stream.str();
    if (hex_str.length() < hex_digits) {
        hex_str = std::string(hex_digits - hex_str.length(), '0') + hex_str;
    }
    std::string bits_out = "0x" + hex_str;

    if (val.is_positive_zero) return os << "0x0." << std::string((val.mantissa_width + 3) / 4, '0') << "p+0 " << bits_out;
    if (val.is_negative_zero) return os << "-0x0." << std::string((val.mantissa_width + 3) / 4, '0') << "p+0 " << bits_out;
    if (val.is_positive_infinity) return os << "inf " << bits_out;
    if (val.is_negative_infinity) return os << "-inf " << bits_out;
    if (val.is_nan) return os << "nan " << bits_out;

    uint64_t mant = static_cast<uint64_t>(val.mantissa);
    int mant_bit_len = 64 - countl_zero_64(mant);
    int pad = (4 - ((mant_bit_len - 1) % 4)) % 4;
    uint64_t shifted_mant = mant << pad;
    std::ostringstream mant_stream;
    mant_stream << std::hex << std::nouppercase << shifted_mant;
    std::string mant_str = mant_stream.str();
    if (!mant_str.empty()) mant_str.insert(1, ".");

    std::ostringstream exp_stream;
    exp_stream << (val.exponent >= 0 ? "+" : "") << val.exponent;

    if (val.is_negative) os << "-";
    os << "0x" << mant_str << "p" << exp_stream.str() << " " << bits_out;
    return os;
}

fp_value multiply_then_add(const fp_value &a, const fp_value &b, const fp_value &c) {
    fp_value product = a * b;
    return product + c;
}

fp_value fused_multiply_add(const fp_value &a, const fp_value &b, const fp_value &c) {
    if (a.is_nan || b.is_nan || c.is_nan) {
        if (a.is_nan) return a;
        if (b.is_nan) return b;
        return c;
    }
    bool prod_sign = a.is_negative ^ b.is_negative;
    if ((a.is_positive_zero || a.is_negative_zero) && (b.is_positive_infinity || b.is_negative_infinity)) return make_quiet_nan(a);
    if ((b.is_positive_zero || b.is_negative_zero) && (a.is_positive_infinity || a.is_negative_infinity)) return make_quiet_nan(a);
    if (a.is_positive_infinity || a.is_negative_infinity || b.is_positive_infinity || b.is_negative_infinity) {
        if ((prod_sign && c.is_positive_infinity) || (!prod_sign && c.is_negative_infinity)) return make_quiet_nan(a);
        return prod_sign ? make_negative_infinity(a) : make_positive_infinity(a);
    }
    if (c.is_positive_infinity || c.is_negative_infinity) return c;
    if ((a.is_positive_zero || a.is_negative_zero) || (b.is_positive_zero || b.is_negative_zero)) return c;
    if (c.is_positive_zero || c.is_negative_zero) return a * b;

    const int m_bits = a.mantissa_width;
    const int e_bits = a.exponent_width;
    const int64_t bias = (1LL << (e_bits - 1)) - 1;
    const int precision = m_bits + 1;

    uint128_t prod_mantissa = uint128_t::multiply_u64(a.mantissa, b.mantissa);
    int64_t prod_exp = a.exponent + b.exponent;
    uint128_t c_mantissa_ext = uint128_t(c.mantissa) << (precision - 1);

    uint128_t term_a_val = prod_mantissa;
    uint128_t term_c_val = c_mantissa_ext;
    int64_t common_exp;
    int64_t exp_diff = prod_exp - c.exponent;

    if (exp_diff > 0) {
        term_c_val >>= exp_diff;
        common_exp = prod_exp;
    } else {
        term_a_val >>= -exp_diff;
        common_exp = c.exponent;
    }
    
    int128_t term_a = prod_sign ? -int128_t(term_a_val) : int128_t(term_a_val);
    int128_t term_c = c.is_negative ? -int128_t(term_c_val) : int128_t(term_c_val);
    int128_t res_mantissa_128 = term_a + term_c;

    bool res_is_negative = res_mantissa_128.is_negative();
    uint128_t res_mantissa_unsigned = res_is_negative ? (-res_mantissa_128).value : res_mantissa_128.value;

    if (res_mantissa_unsigned == uint128_t(0)) {
        if (a.rounding == rounding_mode::to_negative_infinity) return make_negative_zero(a);
        return make_positive_zero(a);
    }
    
    int bit_len = bit_width_128(res_mantissa_unsigned);
    int64_t final_exp = common_exp + bit_len - (2 * precision - 1);
    
    int64_t norm_shift = bit_len - precision;
    uint64_t res_mantissa;
    uint64_t lost_value = 0;

    if (norm_shift >= 0) {
        uint128_t mask = (uint128_t(1) << norm_shift) - uint128_t(1);
        lost_value = (uint64_t)((res_mantissa_unsigned & mask).low);
        res_mantissa_unsigned >>= norm_shift;
        res_mantissa = (uint64_t)res_mantissa_unsigned.low;
    } else {
        res_mantissa = (uint64_t)((res_mantissa_unsigned << -norm_shift).low);
    }

    if (needs_rounding(a.rounding, norm_shift, lost_value, res_mantissa, res_is_negative)) {
        res_mantissa++;
        if ((64 - countl_zero_64(res_mantissa)) > precision) {
            res_mantissa >>= 1;
            final_exp++;
        }
    }

    int64_t res_exp_field = final_exp + bias;

    if (res_exp_field <= 0) {
        int64_t subnormal_shift = 1 - res_exp_field;
        if (subnormal_shift >= 64) {
             res_mantissa = 0;
             lost_value = 1; 
        } else {
            uint128_t temp_mant(res_mantissa);
            uint128_t mask = (uint128_t(1) << subnormal_shift) - uint128_t(1);
            lost_value = (uint64_t)((temp_mant & mask).low);
            temp_mant >>= subnormal_shift;
            res_mantissa = (uint64_t)temp_mant.low;
        }

        if (needs_rounding(a.rounding, subnormal_shift, lost_value, res_mantissa, res_is_negative)) {
            res_mantissa++;
        }
        res_exp_field = 0;
    }

    if (res_exp_field >= (1LL << e_bits) - 1) {
        return res_is_negative ? make_negative_infinity(a) : make_positive_infinity(a);
    }
    
    if (res_mantissa == 0) {
        return res_is_negative ? make_negative_zero(a) : make_positive_zero(a);
    }

    int64_t final_bits = ((int64_t)res_mantissa & ((1LL << m_bits) - 1)) + (res_exp_field << m_bits);
    if (res_is_negative) final_bits += (1LL << (m_bits + e_bits));
    
    return fp_value(final_bits, a.format, a.rounding);
}