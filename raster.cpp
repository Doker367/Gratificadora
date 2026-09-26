#include "raster.h"

#include <algorithm>
#include <cmath>

void Canvas::resize(int width, int height) {
    if (width == w && height == h) return;
    w = width;
    h = height;
    px.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 255);
}

void Canvas::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for (std::size_t i = 0; i < n; ++i) {
        px[i * 4 + 0] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = a;
    }
}

void Canvas::setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4;
    px[idx + 0] = r;
    px[idx + 1] = g;
    px[idx + 2] = b;
    px[idx + 3] = a;
}

namespace {

bool clipEdge(double& t0, double& t1, double p, double q) {
    if (p == 0.0) return q >= 0.0;
    double t = q / p;
    if (p < 0.0) {
        if (t > t1) return false;
        if (t > t0) t0 = t;
    } else {
        if (t < t0) return false;
        if (t < t1) t1 = t;
    }
    return true;
}

bool clipLineT(double x0, double y0, double x1, double y1,
               double xmin, double ymin, double xmax, double ymax,
               double& t0, double& t1) {
    t0 = 0.0;
    t1 = 1.0;
    double dx = x1 - x0;
    double dy = y1 - y0;
    if (!clipEdge(t0, t1, -dx, x0 - xmin)) return false;
    if (!clipEdge(t0, t1, dx, xmax - x0)) return false;
    if (!clipEdge(t0, t1, -dy, y0 - ymin)) return false;
    if (!clipEdge(t0, t1, dy, ymax - y0)) return false;
    return true;
}

}  // namespace

MidResult midpointEllipse(int rx, int ry) {
    MidResult res;
    res.rx = rx;
    res.ry = ry;
    if (rx <= 0 || ry <= 0) return res;

    long rx2 = static_cast<long>(rx) * rx;
    long ry2 = static_cast<long>(ry) * ry;
    long x = 0;
    long y = ry;
    long p = ry2 - rx2 * ry + rx2 / 4;

    int k = 0;
    res.cuarto.push_back({static_cast<int>(x), static_cast<int>(y)});

    while (2 * ry2 * x < 2 * rx2 * y) {
        if (p < 0) {
            res.pasos.push_back({k, p, static_cast<int>(x + 1),
                                 static_cast<int>(y), 1});
            p = p + 2 * ry2 * (x + 1) + ry2;
            x = x + 1;
        } else {
            res.pasos.push_back({k, p, static_cast<int>(x + 1),
                                 static_cast<int>(y - 1), 1});
            p = p + 2 * ry2 * (x + 1) - 2 * rx2 * (y - 1) + ry2;
            x = x + 1;
            y = y - 1;
        }
        res.cuarto.push_back({static_cast<int>(x), static_cast<int>(y)});
        ++k;
    }

    double p2d = static_cast<double>(ry2) * (x + 0.5) * (x + 0.5) +
                 static_cast<double>(rx2) * (y - 1) * (y - 1) -
                 static_cast<double>(rx2) * ry2;
    long p2 = static_cast<long>(std::llround(p2d));
    k = 0;
    while (y > 0) {
        if (p2 > 0) {
            res.pasos.push_back({k, p2, static_cast<int>(x),
                                 static_cast<int>(y - 1), 2});
            p2 = p2 - 2 * rx2 * (y - 1) + rx2;
            y = y - 1;
        } else {
            res.pasos.push_back({k, p2, static_cast<int>(x + 1),
                                 static_cast<int>(y - 1), 2});
            p2 = p2 + 2 * ry2 * (x + 1) - 2 * rx2 * (y - 1) + rx2;
            x = x + 1;
            y = y - 1;
        }
        res.cuarto.push_back({static_cast<int>(x), static_cast<int>(y)});
        ++k;
    }
    return res;
}

MidResult midpointCircle(int r) {
    MidResult res;
    res.rx = r;
    res.ry = r;
    if (r <= 0) return res;

    long x = 0;
    long y = r;
    long p = 1 - r;
    int k = 0;
    res.cuarto.push_back({0, r});

    while (x < y) {
        if (p < 0) {
            res.pasos.push_back({k, p, static_cast<int>(x + 1),
                                 static_cast<int>(y), 0});
            p = p + 2 * (x + 1) + 1;
            x = x + 1;
        } else {
            res.pasos.push_back({k, p, static_cast<int>(x + 1),
                                 static_cast<int>(y - 1), 0});
            p = p + 2 * (x + 1) + 1 - 2 * (y - 1);
            x = x + 1;
            y = y - 1;
        }
        res.cuarto.push_back({static_cast<int>(x), static_cast<int>(y)});
        ++k;
    }
    return res;
}

std::vector<ElPt> conicBoundary(const MidResult& m, bool circle) {
    std::vector<ElPt> q;
    if (circle) {
        q = m.cuarto;
        for (int i = static_cast<int>(m.cuarto.size()) - 2; i >= 0; --i) {
            q.push_back({m.cuarto[i].y, m.cuarto[i].x});
        }
    } else {
        q = m.cuarto;
    }
    std::vector<ElPt> loop;
    const int n = static_cast<int>(q.size());
    if (n == 0) return loop;
    for (int i = 0; i < n; ++i) loop.push_back({q[i].x, q[i].y});
    for (int i = n - 1; i >= 0; --i) loop.push_back({q[i].x, -q[i].y});
    for (int i = 0; i < n; ++i) loop.push_back({-q[i].x, -q[i].y});
    for (int i = n - 1; i >= 0; --i) loop.push_back({-q[i].x, q[i].y});
    return loop;
}

// =============================================================
//  Parabola: x^2 = 4*p*y  (vertice en el origen, abre hacia arriba)
//  Region 1: pendiente < 1 (x < 2p)   -> pasos en x
//  Region 2: pendiente >= 1 (x >= 2p) -> pasos en y
// =============================================================
MidResult midpointParabola(int p, int alcance) {
    MidResult res;
    res.rx = p;
    res.ry = p;
    if (p <= 0) return res;
    if (alcance < 1) alcance = 1;

    long x = 0;
    long y = 0;
    long P = 1 - 2L * p;  // P0 = 1 - 2p
    int k = 0;
    res.cuarto.push_back({static_cast<int>(x), static_cast<int>(y)});

    // Region 1
    while (x < 2L * p && y < alcance) {
        if (P < 0) {
            res.pasos.push_back({k, P, static_cast<int>(x + 1),
                                 static_cast<int>(y), 1});
            P = P + 2 * x + 3;
            x = x + 1;
        } else {
            res.pasos.push_back({k, P, static_cast<int>(x + 1),
                                 static_cast<int>(y + 1), 1});
            P = P + 2 * x + 3 - 4L * p;
            x = x + 1;
            y = y + 1;
        }
        res.cuarto.push_back({static_cast<int>(x), static_cast<int>(y)});
        ++k;
    }

    // Region 2
    if (x >= 2L * p && y < alcance) {
        double qd = static_cast<double>(x + 0.5) * (x + 0.5) -
                    4.0 * p * static_cast<double>(y + 1);
        long Q = static_cast<long>(std::llround(qd));
        while (y < alcance) {
            if (Q > 0) {
                res.pasos.push_back({k, Q, static_cast<int>(x),
                                     static_cast<int>(y + 1), 2});
                Q = Q - 4L * p;
                y = y + 1;
            } else {
                res.pasos.push_back({k, Q, static_cast<int>(x + 1),
                                     static_cast<int>(y + 1), 2});
                Q = Q + 2 * x + 2 - 4L * p;
                x = x + 1;
                y = y + 1;
            }
            res.cuarto.push_back({static_cast<int>(x), static_cast<int>(y)});
            ++k;
        }
    }
    return res;
}

// =============================================================
//  Hiperbola: x^2/a^2 - y^2/b^2 = 1  (rama derecha, mitad superior)
//  Region 2 primero (cerca del vertice, pendiente > 1) -> pasos en y
//  Region 1 despues (pendiente <= 1)                   -> pasos en x
// =============================================================
MidResult midpointHyperbola(int a, int b, int alcance) {
    MidResult res;
    res.rx = a;
    res.ry = b;
    if (a <= 0 || b <= 0) return res;
    if (alcance <= a) alcance = a + 1;

    long A2 = static_cast<long>(a) * a;
    long B2 = static_cast<long>(b) * b;
    long x = a;
    long y = 0;
    int k = 0;
    res.cuarto.push_back({static_cast<int>(x), static_cast<int>(y)});

    // Region 2: pendiente > 1  <=>  b^2*x > a^2*y
    double qd = static_cast<double>(B2) * (x + 0.5) * (x + 0.5) -
                static_cast<double>(A2) * (y + 1) * (y + 1) -
                static_cast<double>(A2) * B2;
    long Q = static_cast<long>(std::llround(qd));
    while (B2 * x > A2 * y && y < alcance) {
        if (Q > 0) {
            res.pasos.push_back({k, Q, static_cast<int>(x),
                                 static_cast<int>(y + 1), 2});
            Q = Q - A2 * (2 * y + 3);
            y = y + 1;
        } else {
            res.pasos.push_back({k, Q, static_cast<int>(x + 1),
                                 static_cast<int>(y + 1), 2});
            Q = Q + B2 * (2 * x + 2) - A2 * (2 * y + 3);
            x = x + 1;
            y = y + 1;
        }
        res.cuarto.push_back({static_cast<int>(x), static_cast<int>(y)});
        ++k;
    }

    // Region 1: pendiente <= 1
    if (B2 * x <= A2 * y && y < alcance) {
        double pd = static_cast<double>(B2) * (x + 1) * (x + 1) -
                    static_cast<double>(A2) * (y + 0.5) * (y + 0.5) -
                    static_cast<double>(A2) * B2;
        long P = static_cast<long>(std::llround(pd));
        while (x < alcance && y < alcance) {
            if (P > 0) {
                res.pasos.push_back({k, P, static_cast<int>(x + 1),
                                     static_cast<int>(y + 1), 1});
                P = P + B2 * (2 * x + 3) - A2 * (2 * y + 2);
                x = x + 1;
                y = y + 1;
            } else {
                res.pasos.push_back({k, P, static_cast<int>(x + 1),
                                     static_cast<int>(y), 1});
                P = P + B2 * (2 * x + 3);
                x = x + 1;
            }
            res.cuarto.push_back({static_cast<int>(x), static_cast<int>(y)});
            ++k;
        }
    }
    return res;
}

std::vector<ElPt> parabolaArc(const MidResult& m, int orient) {
    std::vector<ElPt> arc;
    const std::vector<ElPt>& q = m.cuarto;
    const int n = static_cast<int>(q.size());
    if (n == 0) return arc;
    // Canonica abre hacia arriba: lado izquierdo (arriba->vertice) y derecho.
    for (int i = n - 1; i >= 0; --i) arc.push_back({-q[i].x, q[i].y});
    for (int i = 1; i < n; ++i) arc.push_back({q[i].x, q[i].y});
    for (ElPt& pt : arc) {
        int x = pt.x;
        int y = pt.y;
        switch (orient) {
            case 1:  pt.x = x;  pt.y = -y; break;  // abajo
            case 2:  pt.x = y;  pt.y = x;  break;  // derecha
            case 3:  pt.x = -y; pt.y = x;  break;  // izquierda
            default: pt.x = x;  pt.y = y;  break;  // arriba
        }
    }
    return arc;
}

std::vector<ElPt> hyperbolaBranch(const MidResult& m, int orient, bool right) {
    std::vector<ElPt> br;
    const std::vector<ElPt>& q = m.cuarto;
    const int n = static_cast<int>(q.size());
    if (n == 0) return br;
    // Rama: mitad superior (vertice->arriba) y mitad inferior (arriba->vertice).
    for (int i = 0; i < n; ++i) br.push_back({q[i].x, q[i].y});
    for (int i = n - 1; i >= 0; --i) br.push_back({q[i].x, -q[i].y});
    if (!right) {
        for (ElPt& pt : br) pt.x = -pt.x;
    }
    if (orient == 1) {
        for (ElPt& pt : br) {
            int t = pt.x;
            pt.x = pt.y;
            pt.y = t;
        }
    }
    return br;
}

DdaResult ddaLine(double xa, double ya, double xb, double yb) {
    DdaResult res;
    const double dx = xb - xa;
    const double dy = yb - ya;

    std::string dirX;
    if (dx > 0.0) dirX = "Izq->Der";
    else if (dx < 0.0) dirX = "Der->Izq";

    std::string dirY;
    if (dy > 0.0) dirY = "Aba->Arr";
    else if (dy < 0.0) dirY = "Arr->Abj";

    res.pasos.push_back({0, xa, ya, static_cast<int>(std::lround(xa)),
                         static_cast<int>(std::lround(ya))});

    if (dx == 0.0) {
        // Linea vertical: M = Error
        res.caso = 11;
        res.tipo = "M=Error";
        res.dir = dirY;
        res.m = 0.0;
        const double sy = (dy >= 0.0) ? 1.0 : -1.0;
        int n = static_cast<int>(std::lround(std::fabs(dy)));
        double x = xa;
        double y = ya;
        for (int k = 1; k <= n; ++k) {
            y += sy;
            res.pasos.push_back({k, x, y, static_cast<int>(std::lround(x)),
                                 static_cast<int>(std::lround(y))});
        }
        return res;
    }

    const double m = dy / dx;
    res.m = m;
    const double am = std::fabs(m);

    if (dy == 0.0) {
        res.caso = 10;
        res.tipo = "M=0";
        res.dir = dirX;
    } else if (std::fabs(am - 1.0) < 1e-9) {
        res.caso = (m > 0.0) ? 9 : 12;
        res.tipo = (m > 0.0) ? "M=+1" : "M=-1";
        res.dir = dirX + "  " + dirY;
    } else if (m > 0.0 && am < 1.0) {
        res.caso = (dx > 0.0) ? 1 : 2;
        res.tipo = "+M<1";
        res.dir = dirX + "  " + dirY;
    } else if (m > 0.0 && am > 1.0) {
        res.caso = (dx > 0.0) ? 3 : 4;
        res.tipo = "+M>1";
        res.dir = dirX + "  " + dirY;
    } else if (m < 0.0 && am < 1.0) {
        res.caso = (dx > 0.0) ? 5 : 6;
        res.tipo = "-M<1";
        res.dir = dirX + "  " + dirY;
    } else {
        res.caso = (dx > 0.0) ? 7 : 8;
        res.tipo = "-M>1";
        res.dir = dirX + "  " + dirY;
    }

    if (am <= 1.0) {
        // Se muestrea x en pasos unitarios: Y(k+1) = Yk + m*sx
        const double sx = (dx > 0.0) ? 1.0 : -1.0;
        int n = static_cast<int>(std::lround(std::fabs(dx)));
        double x = xa;
        double y = ya;
        for (int k = 1; k <= n; ++k) {
            x += sx;
            y += m * sx;
            res.pasos.push_back({k, x, y, static_cast<int>(std::lround(x)),
                                 static_cast<int>(std::lround(y))});
        }
    } else {
        // Se muestrea y en pasos unitarios: X(k+1) = Xk + (1/m)*sy
        const double sy = (dy > 0.0) ? 1.0 : -1.0;
        int n = static_cast<int>(std::lround(std::fabs(dy)));
        double x = xa;
        double y = ya;
        for (int k = 1; k <= n; ++k) {
            y += sy;
            x += (1.0 / m) * sy;
            res.pasos.push_back({k, x, y, static_cast<int>(std::lround(x)),
                                 static_cast<int>(std::lround(y))});
        }
    }
    return res;
}

void Canvas::line(double x0, double y0, double x1, double y1,
                  uint8_t r, uint8_t g, uint8_t b) {
    double xmin = -0.5;
    double xmax = w - 0.5;
    double ymin = -0.5;
    double ymax = h - 0.5;
    double t0 = 0.0, t1 = 1.0;
    if (!clipLineT(x0, y0, x1, y1, xmin, ymin, xmax, ymax, t0, t1)) return;

    int X0 = static_cast<int>(std::lround(x0 + (x1 - x0) * t0));
    int Y0 = static_cast<int>(std::lround(y0 + (y1 - y0) * t0));
    int X1 = static_cast<int>(std::lround(x0 + (x1 - x0) * t1));
    int Y1 = static_cast<int>(std::lround(y0 + (y1 - y0) * t1));

    int dx = std::abs(X1 - X0);
    int dy = -std::abs(Y1 - Y0);
    int sx = X0 < X1 ? 1 : -1;
    int sy = Y0 < Y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        setPixel(X0, Y0, r, g, b);
        if (X0 == X1 && Y0 == Y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            X0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            Y0 += sy;
        }
    }
}

void Canvas::fillEllipse(int cx, int cy, int rx, int ry,
                         uint8_t r, uint8_t g, uint8_t b) {
    if (rx <= 0 || ry <= 0) return;
    int y0 = std::max(0, cy - ry);
    int y1 = std::min(h - 1, cy + ry);
    for (int y = y0; y <= y1; ++y) {
        double ty = static_cast<double>(y - cy) / ry;
        double d = 1.0 - ty * ty;
        if (d < 0.0) d = 0.0;
        double hw = rx * std::sqrt(d);
        int xa = std::max(0, static_cast<int>(std::ceil(cx - hw)));
        int xb = std::min(w - 1, static_cast<int>(std::floor(cx + hw)));
        for (int x = xa; x <= xb; ++x) {
            std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4;
            px[idx + 0] = r;
            px[idx + 1] = g;
            px[idx + 2] = b;
            px[idx + 3] = 255;
        }
    }
}

void Canvas::fillTriangle(double ax, double ay, double bx, double by,
                          double cx, double cy,
                          uint8_t r, uint8_t g, uint8_t b) {
    double p[3][2] = {{ax, ay}, {bx, by}, {cx, cy}};
    for (int i = 1; i < 3; ++i) {
        for (int j = i; j > 0; --j) {
            if (p[j][1] < p[j - 1][1]) {
                std::swap(p[j], p[j - 1]);
            }
        }
    }
    double yTop = p[0][1], yMid = p[1][1], yBot = p[2][1];
    if (yBot - yTop < 1e-12) return;

    auto interp = [](double x0, double y0, double x1, double y1, double y) {
        return x0 + (x1 - x0) * (y - y0) / (y1 - y0);
    };

    int yStart = std::max(0, static_cast<int>(std::ceil(yTop)));
    int yEnd = std::min(h - 1, static_cast<int>(std::floor(yBot)));
    for (int y = yStart; y <= yEnd; ++y) {
        double ya = interp(p[0][0], p[0][1], p[2][0], p[2][1], y);
        double xb;
        if (y < yMid) {
            xb = interp(p[0][0], p[0][1], p[1][0], p[1][1], y);
        } else {
            xb = interp(p[1][0], p[1][1], p[2][0], p[2][1], y);
        }
        int x0 = std::max(0, static_cast<int>(std::ceil(std::min(ya, xb))));
        int x1 = std::min(w - 1, static_cast<int>(std::floor(std::max(ya, xb))));
        for (int x = x0; x <= x1; ++x) {
            std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4;
            px[idx + 0] = r;
            px[idx + 1] = g;
            px[idx + 2] = b;
            px[idx + 3] = 255;
        }
    }
}

// Contorno de elipse (algoritmo de punto medio)
void Canvas::drawEllipse(int cx, int cy, int rx, int ry,
                         uint8_t r, uint8_t g, uint8_t b, int thickness) {
    if (rx <= 0 || ry <= 0) return;
    int half = thickness / 2;
    MidResult m = midpointEllipse(rx, ry);
    std::vector<ElPt> loop = conicBoundary(m, false);
    const int n = static_cast<int>(loop.size());
    if (n == 0) return;
    for (int i = 0; i < n; ++i) {
        const ElPt& a = loop[i];
        const ElPt& c = loop[(i + 1) % n];
        double ax = cx + a.x;
        double ay = cy + a.y;
        double bx = cx + c.x;
        double by = cy + c.y;
        for (int oy = -half; oy <= half; ++oy)
            for (int ox = -half; ox <= half; ++ox)
                line(ax + ox, ay + oy, bx + ox, by + oy, r, g, b);
    }
}
