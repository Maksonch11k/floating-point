#include "floating.h"
#include <string>
#include <cstdlib>

bool parse_fp_format(const char* arg, fp_format& format) {
    std::string s(arg);
    if (s == "h") {
        format = fp_format::half;
        return true;
    }
    if (s == "s") {
        format = fp_format::single;
        return true;
    }
    return false;
}

bool parse_rounding_mode(const char* arg, rounding_mode& mode) {
    try {
        int mode_val = std::stoi(arg);
        switch (mode_val) {
            case 0: mode = rounding_mode::to_zero; return true;
            case 1: mode = rounding_mode::to_nearest_even; return true;
            case 2: mode = rounding_mode::to_positive_infinity; return true;
            case 3: mode = rounding_mode::to_negative_infinity; return true;
            default: return false;
        }
    } catch (...) {
        return false;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        exit_with_error_message("Error: Invalid number of arguments.");
    }

    fp_format format;
    if (!parse_fp_format(argv[1], format)) {
        exit_with_error_message("Error: Invalid format specified.");
    }

    rounding_mode rounding;
    if (!parse_rounding_mode(argv[2], rounding)) {
        exit_with_error_message("Error: Invalid rounding mode specified.");
    }

    if (argc == 4) {
        fp_value val(parse_hex(argv[3]), format, rounding);
        std::cout << val << std::endl;
    } else if (argc == 6) {
        fp_value op1(parse_hex(argv[4]), format, rounding);
        fp_value op2(parse_hex(argv[5]), format, rounding);
        std::string op_str = argv[3];
        if (op_str.length() != 1) {
            exit_with_error_message("Error: Invalid operation specified.");
        }
        char op = op_str[0];

        switch (op) {
            case '+': std::cout << (op1 + op2) << std::endl; break;
            case '-': std::cout << (op1 - op2) << std::endl; break;
            case '*': std::cout << (op1 * op2) << std::endl; break;
            case '/': std::cout << (op1 / op2) << std::endl; break;
            default:
                exit_with_error_message("Error: Invalid operation specified.");
        }
    } else if (argc == 7) {
        fp_value op1(parse_hex(argv[4]), format, rounding);
        fp_value op2(parse_hex(argv[5]), format, rounding);
        fp_value op3(parse_hex(argv[6]), format, rounding);
        std::string operation = argv[3];

        if (operation == "fma") {
            std::cout << fused_multiply_add(op1, op2, op3) << std::endl;
        } else if (operation == "mad") {
            std::cout << multiply_then_add(op1, op2, op3) << std::endl;
        } else {
            exit_with_error_message("Error: Invalid operation specified.");
        }
    } else {
        exit_with_error_message("Error: Invalid number of arguments for the operation.");
    }

    return 0;
}