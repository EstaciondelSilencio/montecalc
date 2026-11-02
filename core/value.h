// MonteCalc - a spreadsheet value (number, text, boolean, error, empty or array)
#pragma once

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace mc {

struct Value {
    enum Kind { Empty, Number, String, Bool, Error, Array };
    Kind kind = Empty;
    double num = 0;
    std::string str;   // text, or error code for Error
    std::shared_ptr<std::vector<std::vector<Value>>> arr;

    static Value number(double v) { Value x; x.kind = Number; x.num = v; return x; }
    static Value text(const std::string& s) { Value x; x.kind = String; x.str = s; return x; }
    static Value boolean(bool b) { Value x; x.kind = Bool; x.num = b ? 1 : 0; return x; }
    static Value error(const std::string& e) { Value x; x.kind = Error; x.str = e; return x; }
    static Value array(std::vector<std::vector<Value>> a) {
        Value x; x.kind = Array; x.arr = std::make_shared<std::vector<std::vector<Value>>>(std::move(a)); return x;
    }

    bool isEmpty() const { return kind == Empty || (kind == String && str.empty()); }
    bool isNumeric() const { return kind == Number || kind == Bool; }
    // Numeric value; text that parses as a number is accepted. Throws otherwise.
    double asNumber() const {
        if (kind == Number || kind == Bool) return num;
        if (kind == Empty) return 0;
        if (kind == String) {
            char* end = nullptr;
            double v = std::strtod(str.c_str(), &end);
            if (end && end != str.c_str() && *end == 0) return v;
            throw std::invalid_argument("expected a number but got \"" + str + "\"");
        }
        if (kind == Error) throw std::invalid_argument("argument is an error value " + str);
        if (arr && !arr->empty() && !(*arr)[0].empty()) return (*arr)[0][0].asNumber();
        throw std::invalid_argument("expected a number");
    }
    bool isNumberText() const {
        if (kind != String) return false;
        char* end = nullptr;
        std::strtod(str.c_str(), &end);
        return end && end != str.c_str() && *end == 0;
    }
    // All numbers in an array (or the scalar), row by row; text/empty skipped.
    std::vector<double> numbers() const {
        std::vector<double> r;
        if (kind == Array) {
            for (const auto& row : *arr)
                for (const auto& c : row)
                    if (c.kind == Number) r.push_back(c.num);
        } else if (isNumeric()) {
            r.push_back(num);
        }
        return r;
    }
};

} // namespace mc
