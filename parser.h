#pragma once

#include <string>
#include <vector>

enum {
    N_NUM,
    N_VAR,
    N_ADD,
    N_SUB,
    N_MUL,
    N_DIV,
    N_POW,
    N_NEG,
    N_FUN
};

struct Expr {
    struct Node {
        int op;
        double val;
        int a;
        int b;
        double (*fn)(double);
    };

    std::vector<Node> nodes;
    int root = -1;
    std::string err;

    double eval(double x) const;
    static bool parse(const std::string& s, Expr& out);
};
