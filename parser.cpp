#include "parser.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace {

struct FnInfo {
    const char* name;
    double (*fn)(double);
};

const double kDeg = 3.14159265358979323846 / 180.0;

double wSind(double v) { return std::sin(v * kDeg); }
double wCosd(double v) { return std::cos(v * kDeg); }
double wTand(double v) { return std::tan(v * kDeg); }
double wAsind(double v) { return std::asin(v) / kDeg; }
double wAcosd(double v) { return std::acos(v) / kDeg; }
double wAtand(double v) { return std::atan(v) / kDeg; }
double wSec(double v) { return 1.0 / std::cos(v); }
double wCsc(double v) { return 1.0 / std::sin(v); }
double wCot(double v) { return 1.0 / std::tan(v); }
double wLog2(double v) { return std::log(v) / std::log(2.0); }
double wLog10(double v) { return std::log(v) / std::log(10.0); }
double wSign(double v) { return v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0); }
double wDeg(double v) { return v / kDeg; }
double wRad(double v) { return v * kDeg; }

const FnInfo kFns[] = {
    {"sin", std::sin},
    {"cos", std::cos},
    {"tan", std::tan},
    {"asin", std::asin},
    {"acos", std::acos},
    {"atan", std::atan},
    {"sind", wSind},
    {"cosd", wCosd},
    {"tand", wTand},
    {"asind", wAsind},
    {"acosd", wAcosd},
    {"atand", wAtand},
    {"sec", wSec},
    {"csc", wCsc},
    {"cot", wCot},
    {"sinh", std::sinh},
    {"cosh", std::cosh},
    {"tanh", std::tanh},
    {"exp", std::exp},
    {"log", std::log},
    {"ln", std::log},
    {"log10", wLog10},
    {"log2", wLog2},
    {"sqrt", std::sqrt},
    {"cbrt", std::cbrt},
    {"abs", std::fabs},
    {"sign", wSign},
    {"floor", std::floor},
    {"ceil", std::ceil},
    {"round", std::round},
    {"deg", wDeg},
    {"rad", wRad},
};

bool eqIgnoreCase(const std::string& a, const char* b) {
    std::size_t j = 0;
    while (b[j] != '\0') {
        if (j >= a.size()) return false;
        if (std::tolower(static_cast<unsigned char>(a[j])) !=
            std::tolower(static_cast<unsigned char>(b[j])))
            return false;
        ++j;
    }
    return j == a.size();
}

int findFunc(const std::string& name) {
    for (int i = 0; i < static_cast<int>(sizeof(kFns) / sizeof(kFns[0])); ++i) {
        if (eqIgnoreCase(name, kFns[i].name)) return i;
    }
    return -1;
}

class Parser {
public:
    Parser(const std::string& s, std::vector<Expr::Node>& out) : src(s), nodes(out) {}

    int run() {
        skipWs();
        int e = parseExpr();
        skipWs();
        if (i != src.size()) fail("caracter inesperado");
        return e;
    }

private:
    const std::string& src;
    std::vector<Expr::Node>& nodes;
    std::size_t i = 0;

    void fail(const std::string& msg) {
        throw std::runtime_error(std::string("error de sintaxis en la posicion ") +
                                 std::to_string(i) + ": " + msg);
    }

    void skipWs() {
        while (i < src.size() && std::isspace(static_cast<unsigned char>(src[i]))) ++i;
    }

    int push(int op, double val, int a = -1, int b = -1,
             double (*fn)(double) = nullptr) {
        Expr::Node n;
        n.op = op;
        n.val = val;
        n.a = a;
        n.b = b;
        n.fn = fn;
        nodes.push_back(n);
        return static_cast<int>(nodes.size()) - 1;
    }

    char peek() {
        skipWs();
        if (i >= src.size()) return '\0';
        return src[i];
    }

    char next() {
        skipWs();
        if (i >= src.size()) fail("fin de expresion inesperado");
        return src[i++];
    }

    int parseExpr() {
        int e = parseTerm();
        for (;;) {
            char c = peek();
            if (c == '+') {
                ++i;
                int t = parseTerm();
                e = push(N_ADD, 0, e, t);
            } else if (c == '-') {
                ++i;
                int t = parseTerm();
                e = push(N_SUB, 0, e, t);
            } else {
                return e;
            }
        }
    }

    int parseTerm() {
        int e = parseUnary();
        for (;;) {
            char c = peek();
            if (c == '*') {
                ++i;
                int t = parseUnary();
                e = push(N_MUL, 0, e, t);
            } else if (c == '/') {
                ++i;
                int t = parseUnary();
                e = push(N_DIV, 0, e, t);
            }
            // Multiplicación implícita: 4cos(t), 2t, 3(x+1), 2pi, etc.
            else if (c == '(' ||
                     std::isalpha(static_cast<unsigned char>(c)) ||
                     c == '.') {
                int t = parseUnary();
                e = push(N_MUL, 0, e, t);
            } else {
                return e;
            }
        }
    }

    int parseUnary() {
        char c = peek();
        if (c == '-') {
            ++i;
            int v = parseUnary();
            return push(N_NEG, 0, v);
        }
        if (c == '+') {
            ++i;
            return parseUnary();
        }
        return parsePower();
    }

    int parsePower() {
        int base = parsePrimary();
        if (peek() == '^') {
            ++i;
            int exp = parseUnary();
            return push(N_POW, 0, base, exp);
        }
        return base;
    }

    int parsePrimary() {
        skipWs();
        if (i >= src.size()) fail("expresion incompleta");
        char c = src[i];
        if (c == '(') {
            ++i;
            int e = parseExpr();
            if (next() != ')') fail("se esperaba ')'");
            return e;
        }
        if (c == 'x' || c == 'X') {
            ++i;
            return push(N_VAR, 0);
        }
        // 't' como variable (para curvas paramétricas), pero no si es
        // inicio de función (tan, tanh…): verificar que no siga letra
        if ((c == 't' || c == 'T') &&
            (i + 1 >= src.size() ||
             !std::isalpha(static_cast<unsigned char>(src[i + 1])))) {
            ++i;
            return push(N_VAR, 0);
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            const char* p = src.c_str() + i;
            char* end = nullptr;
            double v = std::strtod(p, &end);
            if (end == p) fail("numero invalido");
            i += static_cast<std::size_t>(end - p);
            return push(N_NUM, v);
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t start = i;
            while (i < src.size() &&
                   (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) {
                ++i;
            }
            std::string name = src.substr(start, i - start);
            if (name == "pi" || name == "PI") return push(N_NUM, 3.14159265358979323846);
            if (name == "e" || name == "E") return push(N_NUM, 2.71828182845904523536);
            int f = findFunc(name);
            if (f < 0) fail("funcion desconocida: " + name);
            skipWs();
            if (next() != '(') fail("se esperaba '(' tras la funcion");
            int a = parseExpr();
            if (next() != ')') fail("se esperaba ')'");
            return push(N_FUN, 0, a, -1, kFns[f].fn);
        }
        fail("simbolo inesperado");
        return -1;
    }
};

}  // namespace

double Expr::eval(double x) const {
    struct Visitor {
        const Expr& e;
        double x;
        double operator()(int idx) const {
            const Node& n = e.nodes[idx];
            switch (n.op) {
                case N_NUM: return n.val;
                case N_VAR: return x;
                case N_ADD: return (*this)(n.a) + (*this)(n.b);
                case N_SUB: return (*this)(n.a) - (*this)(n.b);
                case N_MUL: return (*this)(n.a) * (*this)(n.b);
                case N_DIV: return (*this)(n.a) / (*this)(n.b);
                case N_POW: return std::pow((*this)(n.a), (*this)(n.b));
                case N_NEG: return -(*this)(n.a);
                case N_FUN: return n.fn((*this)(n.a));
                default: return 0.0;
            }
        }
    } vis{*this, x};
    return vis(root);
}

bool Expr::parse(const std::string& raw, Expr& out) {
    out.nodes.clear();
    out.root = -1;
    out.err.clear();
    std::string s;
    std::size_t start = 0;
    std::size_t end = raw.size();
    while (start < end &&
           std::isspace(static_cast<unsigned char>(raw[start]))) ++start;
    while (end > start &&
           std::isspace(static_cast<unsigned char>(raw[end - 1]))) --end;
    std::size_t eq = raw.find('=', start);
    if (eq != std::string::npos && eq < end) start = eq + 1;
    while (start < end &&
           std::isspace(static_cast<unsigned char>(raw[start]))) ++start;
    s = raw.substr(start, end - start);
    if (s.empty()) {
        out.err = "expresion vacia";
        return false;
    }
    try {
        Parser p(s, out.nodes);
        out.root = p.run();
        return true;
    } catch (const std::exception& ex) {
        out.nodes.clear();
        out.root = -1;
        out.err = ex.what();
        return false;
    }
}
