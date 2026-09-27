#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "gui.h"
#include "imgui.h"
#include "parser.h"
#include "raster.h"

struct FuncSample {
    bool good = false;
    std::string err;
    std::vector<double> xs;
    std::vector<double> ys;
    std::vector<bool> ok;
};

struct App {
    GLFWwindow* window = nullptr;
    Canvas canvas;

    double s = 1.0;
    double cx = 0.0;
    double cy = 0.0;
    bool dragging = false;
    double lastMx = 0.0;
    double lastMy = 0.0;

    std::vector<FuncSample> funcs;

    GLuint tex = 0;
    int texW = 0;
    int texH = 0;
};

static App g_app;

namespace {

const unsigned char GRID_C[3] = {228, 228, 228};
const unsigned char AXIS_C[3] = {40, 40, 40};
const unsigned char MARK_RING[3] = {70, 70, 70};

void failGl(const char* what, GLuint id) {
    std::fprintf(stderr, "%s: ", what);
    GLint len = 0;
    glGetShaderiv(id, GL_INFO_LOG_LENGTH, &len);
    if (len > 0) {
        std::vector<char> log(len + 1);
        glGetShaderInfoLog(id, len, nullptr, log.data());
        std::fprintf(stderr, "%s\n", log.data());
    }
    std::exit(1);
}

GLuint compileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) failGl("shader", sh);
    return sh;
}

GLuint makeProgram() {
    const char* vs = R"GLSL(#version 330 core
layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;
out vec2 vUv;
void main() {
    vUv = uv;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)GLSL";
    const char* fs = R"GLSL(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
out vec4 frag;
void main() {
    frag = texture(uTex, vUv);
}
)GLSL";
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) failGl("programa", p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

double pxOfX(double xw) {
    return (xw - g_app.cx) * g_app.s + g_app.canvas.w / 2.0;
}

double pyOfY(double yw) {
    return g_app.canvas.h / 2.0 - (yw - g_app.cy) * g_app.s;
}

double worldX(double px) {
    return g_app.cx + (px - g_app.canvas.w / 2.0) / g_app.s;
}

double worldY(double py) {
    return g_app.cy - (py - g_app.canvas.h / 2.0) / g_app.s;
}

double niceStep(double worldPerPx, double targetPx) {
    double raw = worldPerPx * targetPx;
    if (raw <= 0.0) return 1.0;
    double m = std::pow(10.0, std::floor(std::log10(raw)));
    double c = m;
    for (double cand : {m, 2.0 * m, 5.0 * m, 10.0 * m}) {
        if (cand >= raw - 1e-12) {
            c = cand;
            break;
        }
    }
    return c;
}

std::string trimStr(const std::string& t) {
    std::size_t a = 0, b = t.size();
    while (a < b && std::isspace(static_cast<unsigned char>(t[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(t[b - 1]))) --b;
    return t.substr(a, b - a);
}

void resampleAll() {
    g_app.funcs.clear();
    double lo = g_gui.ra;
    double hi = g_gui.rb;
    if (lo > hi) std::swap(lo, hi);
    bool rangeOk = (hi - lo) > 1e-12;
    int n = std::max(2, std::min(g_gui.rn, 2000000));

    for (std::size_t r = 0; r < g_gui.funcs.size(); ++r) {
        FuncSample fs;
        if (!rangeOk) {
            fs.err = "rango invalido (a >= b)";
            g_app.funcs.push_back(fs);
            g_gui.funcs[r].good = false;
            g_gui.funcs[r].err = fs.err;
            continue;
        }
        Expr e;
        if (!Expr::parse(trimStr(g_gui.funcs[r].expr), e)) {
            fs.err = e.err;
            g_app.funcs.push_back(fs);
            g_gui.funcs[r].good = false;
            g_gui.funcs[r].err = fs.err;
            continue;
        }
        fs.xs.resize(n);
        fs.ys.resize(n);
        fs.ok.resize(n);
        bool any = false;
        for (int i = 0; i < n; ++i) {
            double x = lo + i * (hi - lo) / (n - 1);
            double y = e.eval(x);
            fs.xs[i] = x;
            fs.ys[i] = y;
            fs.ok[i] = std::isfinite(y);
            any = any || fs.ok[i];
        }
        fs.good = any;
        if (!any) fs.err = "f(x) indefinida en todo el rango";
        g_app.funcs.push_back(fs);
        g_gui.funcs[r].good = any;
        g_gui.funcs[r].err = fs.err;
    }
}

double fbScaleX();

void applyView(double xmin, double xmax, double ymin, double ymax) {
    if (xmax - xmin < 1e-12) {
        xmin -= 1.0;
        xmax += 1.0;
    }
    if (ymax - ymin < 1e-12) {
        ymin -= 1.0;
        ymax += 1.0;
    }
    double scale = fbScaleX();
    double panelPx = g_gui.showPanel ? 420.0 * scale : 0.0;
    double toolbarPx = 42.0 * scale;
    double usableW = g_app.canvas.w - panelPx;
    double usableH = g_app.canvas.h - toolbarPx;
    if (usableW < 10.0) usableW = g_app.canvas.w;
    if (usableH < 10.0) usableH = g_app.canvas.h;
    double sx = usableW / (xmax - xmin);
    double sy = usableH / (ymax - ymin);
    double xcw = (xmin + xmax) / 2.0;
    double ycw = (ymin + ymax) / 2.0;
    g_app.s = std::min(sx, sy) * 0.94;
    g_app.s = std::max(1e-9, std::min(1e12, g_app.s));
    g_app.cx = xcw + (panelPx / 2.0) / g_app.s;
    g_app.cy = ycw + (toolbarPx / 2.0) / g_app.s;
}

std::vector<std::vector<ElPt>> conicArcs(const ConicRow& c) {
    std::vector<std::vector<ElPt>> out;
    if (c.type == 0 || c.type == 1) {
        bool circle = (c.type == 1);
        MidResult m = circle ? midpointCircle(c.r) : midpointEllipse(c.rx, c.ry);
        out.push_back(conicBoundary(m, circle));
    } else if (c.type == 2) {
        out.push_back(parabolaArc(midpointParabola(c.p, c.alcance), c.orient));
    } else {
        MidResult m = midpointHyperbola(c.a, c.b, c.alcance);
        out.push_back(hyperbolaBranch(m, c.orient, true));
        out.push_back(hyperbolaBranch(m, c.orient, false));
    }
    return out;
}

void doFitAll() {
    double lo = g_gui.ra;
    double hi = g_gui.rb;
    if (lo > hi) std::swap(lo, hi);
    double xmin = std::numeric_limits<double>::infinity();
    double xmax = -std::numeric_limits<double>::infinity();
    double ymin = std::numeric_limits<double>::infinity();
    double ymax = -std::numeric_limits<double>::infinity();
    bool any = false;
    bool anyFunc = false;

    for (std::size_t r = 0; r < g_app.funcs.size() && r < g_gui.funcs.size(); ++r) {
        if (!g_gui.funcs[r].visible || !g_app.funcs[r].good) continue;
        const FuncSample& fs = g_app.funcs[r];
        for (std::size_t i = 0; i < fs.ok.size(); ++i) {
            if (!fs.ok[i]) continue;
            any = true;
            anyFunc = true;
            xmin = std::min(xmin, fs.xs[i]);
            xmax = std::max(xmax, fs.xs[i]);
            ymin = std::min(ymin, fs.ys[i]);
            ymax = std::max(ymax, fs.ys[i]);
        }
    }
    for (const ConicRow& c : g_gui.conics) {
        if (!c.visible) continue;
        std::vector<std::vector<ElPt>> arcs = conicArcs(c);
        for (const std::vector<ElPt>& arc : arcs) {
            for (const ElPt& e : arc) {
                any = true;
                double wx = static_cast<double>(c.xc) + e.x;
                double wy = static_cast<double>(c.yc) + e.y;
                xmin = std::min(xmin, wx);
                xmax = std::max(xmax, wx);
                ymin = std::min(ymin, wy);
                ymax = std::max(ymax, wy);
            }
        }
    }
    for (const DdaLine& d : g_gui.ddas) {
        if (!d.visible) continue;
        any = true;
        xmin = std::min(xmin, static_cast<double>(d.xa));
        xmax = std::max(xmax, static_cast<double>(d.xa));
        ymin = std::min(ymin, static_cast<double>(d.ya));
        ymax = std::max(ymax, static_cast<double>(d.ya));
        xmin = std::min(xmin, static_cast<double>(d.xb));
        xmax = std::max(xmax, static_cast<double>(d.xb));
        ymin = std::min(ymin, static_cast<double>(d.yb));
        ymax = std::max(ymax, static_cast<double>(d.yb));
    }
    for (const PolyFill& pf : g_gui.polys) {
        if (!pf.visible) continue;
        int nv = (pf.kind == 0) ? 3 : 4;
        for (int i = 0; i < nv; ++i) {
            any = true;
            xmin = std::min(xmin, static_cast<double>(pf.x[i]));
            xmax = std::max(xmax, static_cast<double>(pf.x[i]));
            ymin = std::min(ymin, static_cast<double>(pf.y[i]));
            ymax = std::max(ymax, static_cast<double>(pf.y[i]));
        }
    }
    for (const PointRow& p : g_gui.pts) {
        any = true;
        xmin = std::min(xmin, static_cast<double>(p.x));
        xmax = std::max(xmax, static_cast<double>(p.x));
        ymin = std::min(ymin, static_cast<double>(p.y));
        ymax = std::max(ymax, static_cast<double>(p.y));
    }
    if (!any) {
        xmin = lo;
        xmax = hi;
        ymin = -(hi - lo) / 2.0;
        ymax = (hi - lo) / 2.0;
    } else if (anyFunc) {
        xmin = std::min(xmin, lo);
        xmax = std::max(xmax, hi);
    }
    if (xmax - xmin < 1e-9) {
        xmin -= 1.0;
        xmax += 1.0;
    }
    if (ymax - ymin < 1e-9) {
        ymin -= 1.0;
        ymax += 1.0;
    }
    double xm = 0.08 * (xmax - xmin);
    double ym = 0.08 * (ymax - ymin);
    applyView(xmin - xm, xmax + xm, ymin - ym, ymax + ym);
}

void segThick(double x0, double y0, double x1, double y1,
              const unsigned char* col, int r) {
    Canvas& cv = g_app.canvas;
    for (int oy = -r; oy <= r; ++oy) {
        for (int ox = -r; ox <= r; ++ox) {
            cv.line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, col[0], col[1], col[2]);
        }
    }
}

void dashThick(double x0, double y0, double x1, double y1,
               const unsigned char* col, int r) {
    Canvas& cv = g_app.canvas;
    double dx = x1 - x0;
    double dy = y1 - y0;
    double L = std::hypot(dx, dy);
    if (L < 0.5) {
        cv.line(x0, y0, x1, y1, col[0], col[1], col[2]);
        return;
    }
    const double dash = 9.0, gap = 6.0;
    double pos = 0.0;
    while (pos < L) {
        double e = std::min(pos + dash, L);
        double ax = x0 + dx * (pos / L);
        double ay = y0 + dy * (pos / L);
        double bx = x0 + dx * (e / L);
        double by = y0 + dy * (e / L);
        for (int oy = -r; oy <= r; ++oy) {
            for (int ox = -r; ox <= r; ++ox) {
                cv.line(ax + ox, ay + oy, bx + ox, by + oy, col[0], col[1], col[2]);
            }
        }
        pos = e + gap;
    }
}

void markAt(double pxc, double pyc, const unsigned char* col, int rad) {
    int ix = static_cast<int>(std::lround(pxc));
    int iy = static_cast<int>(std::lround(pyc));
    g_app.canvas.fillEllipse(ix, iy, rad, rad, col[0], col[1], col[2]);
}

void drawFunc(int fi) {
    if (!g_gui.funcs[fi].visible) return;
    const FuncSample& fs = g_app.funcs[fi];
    if (!fs.good) return;
    const unsigned char* col = guiPalette(g_gui.funcs[fi].color);
    int style = g_gui.funcs[fi].style;
    int r = g_gui.funcs[fi].width;
    Canvas& cv = g_app.canvas;
    double xL = worldX(0.0);
    double xR = worldX(cv.w);
    bool wantMarkers = style == SS_POINTS || style == SS_LINE_POINTS;
    bool wantLine = style == SS_LINE || style == SS_LINE_POINTS || style == SS_DASH;

    if (wantLine) {
        for (std::size_t i = 0; i + 1 < fs.xs.size(); ++i) {
            if (!fs.ok[i] || !fs.ok[i + 1]) continue;
            if (fs.xs[i + 1] < xL || fs.xs[i] > xR) continue;
            double ax = pxOfX(fs.xs[i]);
            double ay = pyOfY(fs.ys[i]);
            double bx = pxOfX(fs.xs[i + 1]);
            double by = pyOfY(fs.ys[i + 1]);
            if (style == SS_DASH) {
                dashThick(ax, ay, bx, by, col, r);
            } else {
                segThick(ax, ay, bx, by, col, r);
            }
        }
    }
    if (wantMarkers) {
        double spacing = g_app.s * (fs.xs[1] - fs.xs[0]);
        if (spacing >= 1.0) {
            for (std::size_t i = 0; i < fs.ok.size(); ++i) {
                if (!fs.ok[i]) continue;
                if (fs.xs[i] < xL || fs.xs[i] > xR) continue;
                markAt(pxOfX(fs.xs[i]), pyOfY(fs.ys[i]), col, 2);
            }
        }
    }
}

void drawPointsPoly() {
    const std::size_t n = g_gui.pts.size();
    if (n < 2) {
        // Dibujar puntos individuales aunque no haya lineas
        for (std::size_t i = 0; i < n; ++i) {
            double ax = pxOfX(g_gui.pts[i].x);
            double ay = pyOfY(g_gui.pts[i].y);
            markAt(ax, ay, MARK_RING, 7);
            const unsigned char* col = guiPalette(g_gui.polyColor);
            markAt(ax, ay, col, 5);
            markAt(ax, ay, MARK_RING, 2);
        }
        return;
    }
    const unsigned char* col = guiPalette(g_gui.polyColor);
    int r = g_gui.polyWidth;
    int style = g_gui.polyStyle;
    bool wantLine = style == SS_LINE || style == SS_LINE_POINTS || style == SS_DASH;

    // Solo dibujar lineas secuenciales si connectAll esta activo
    if (wantLine && g_gui.connectAll) {
        for (std::size_t i = 0; i + 1 < n; ++i) {
            double ax = pxOfX(g_gui.pts[i].x);
            double ay = pyOfY(g_gui.pts[i].y);
            double bx = pxOfX(g_gui.pts[i + 1].x);
            double by = pyOfY(g_gui.pts[i + 1].y);
            if (style == SS_DASH) {
                dashThick(ax, ay, bx, by, col, r);
            } else {
                segThick(ax, ay, bx, by, col, r);
            }
        }
    }

    // Dibujar puntos
    for (std::size_t i = 0; i < n; ++i) {
        double ax = pxOfX(g_gui.pts[i].x);
        double ay = pyOfY(g_gui.pts[i].y);
        markAt(ax, ay, MARK_RING, 7);
        markAt(ax, ay, col, 5);
        markAt(ax, ay, MARK_RING, 2);
    }
}

void drawCustomConns() {
    const std::size_t n = g_gui.pts.size();
    for (std::size_t ci = 0; ci < g_gui.conns.size(); ++ci) {
        const SegConnection& c = g_gui.conns[ci];
        if (!c.enabled) continue;
        if (c.fromIdx < 0 || c.fromIdx >= static_cast<int>(n)) continue;
        if (c.toIdx < 0 || c.toIdx >= static_cast<int>(n)) continue;
        if (c.fromIdx == c.toIdx) continue;

        // Evitar duplicar con secuenciales
        if (g_gui.connectAll) {
            bool dup = false;
            for (std::size_t i = 0; i + 1 < n; ++i) {
                if ((c.fromIdx == static_cast<int>(i) && c.toIdx == static_cast<int>(i + 1)) ||
                    (c.toIdx == static_cast<int>(i) && c.fromIdx == static_cast<int>(i + 1))) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
        }

        const unsigned char* col = guiPalette(g_gui.polyColor);
        int r = g_gui.polyWidth;
        int style = g_gui.polyStyle;
        bool wantLine = style == SS_LINE || style == SS_LINE_POINTS || style == SS_DASH;

        double ax = pxOfX(g_gui.pts[c.fromIdx].x);
        double ay = pyOfY(g_gui.pts[c.fromIdx].y);
        double bx = pxOfX(g_gui.pts[c.toIdx].x);
        double by = pyOfY(g_gui.pts[c.toIdx].y);
        
        if (wantLine) {
            if (style == SS_DASH) {
                dashThick(ax, ay, bx, by, col, r);
            } else {
                segThick(ax, ay, bx, by, col, r);
            }
        }
    }
}


void drawConic(const ConicRow& c) {
    if (!c.visible) return;
    const unsigned char* col = guiPalette(c.color);
    bool closed = (c.type == 0 || c.type == 1);
    std::vector<std::vector<ElPt>> arcs = conicArcs(c);
    if (arcs.empty()) return;

    if (c.fillMode == 1) {
        double cxp = pxOfX(c.xc);
        double cyp = pyOfY(c.yc);
        for (const std::vector<ElPt>& arc : arcs) {
            int n = static_cast<int>(arc.size());
            int stride = 1;
            if (n > 2000) stride = (n + 1999) / 2000;
            for (int i = 0; i < n; i += stride) {
                double bx = pxOfX(static_cast<double>(c.xc) + arc[i].x);
                double by = pyOfY(static_cast<double>(c.yc) + arc[i].y);
                g_app.canvas.line(cxp, cyp, bx, by, col[0], col[1], col[2]);
            }
        }
    } else if (c.fillMode == 2) {
        if (c.type == 0 || c.type == 1) {
            int rxr = (c.type == 1) ? c.r : c.rx;
            int ryr = (c.type == 1) ? c.r : c.ry;
            double cxp = pxOfX(c.xc);
            double cyp = pyOfY(c.yc);
            int rxp = static_cast<int>(std::lround(rxr * g_app.s));
            int ryp = static_cast<int>(std::lround(ryr * g_app.s));
            g_app.canvas.fillEllipse(static_cast<int>(std::lround(cxp)),
                                     static_cast<int>(std::lround(cyp)),
                                     std::max(1, rxp), std::max(1, ryp),
                                     col[0], col[1], col[2]);
        } else {
            double ox = pxOfX(c.xc);
            double oy = pyOfY(c.yc);
            for (const std::vector<ElPt>& arc : arcs) {
                for (std::size_t i = 0; i + 1 < arc.size(); ++i) {
                    g_app.canvas.fillTriangle(
                        ox, oy,
                        pxOfX(static_cast<double>(c.xc) + arc[i].x),
                        pyOfY(static_cast<double>(c.yc) + arc[i].y),
                        pxOfX(static_cast<double>(c.xc) + arc[i + 1].x),
                        pyOfY(static_cast<double>(c.yc) + arc[i + 1].y),
                        col[0], col[1], col[2]);
                }
            }
        }
    }

    for (const std::vector<ElPt>& arc : arcs) {
        const int n = static_cast<int>(arc.size());
        if (n < 2) continue;
        int last = closed ? n : n - 1;
        for (int i = 0; i < last; ++i) {
            const ElPt& a = arc[i];
            const ElPt& b = arc[(i + 1) % n];
            segThick(pxOfX(static_cast<double>(c.xc) + a.x),
                     pyOfY(static_cast<double>(c.yc) + a.y),
                     pxOfX(static_cast<double>(c.xc) + b.x),
                     pyOfY(static_cast<double>(c.yc) + b.y), col, c.width);
        }
    }
}

void drawDda(const DdaLine& d) {
    if (!d.visible) return;
    const unsigned char* col = guiPalette(d.color);
    DdaResult r = ddaLine(d.xa, d.ya, d.xb, d.yb);
    const int n = static_cast<int>(r.pasos.size());
    if (n == 0) return;
    if (n == 1) {
        markAt(pxOfX(r.pasos[0].px), pyOfY(r.pasos[0].py), col, d.width);
        return;
    }
    for (int i = 0; i + 1 < n; ++i) {
        segThick(pxOfX(r.pasos[i].px), pyOfY(r.pasos[i].py),
                 pxOfX(r.pasos[i + 1].px), pyOfY(r.pasos[i + 1].py),
                 col, d.width);
    }
}

void polyBoundary(const PolyFill& pf, std::vector<ElPt>& out) {
    int nv = (pf.kind == 0) ? 3 : 4;
    for (int i = 0; i < nv; ++i) {
        int j = (i + 1) % nv;
        DdaResult r = ddaLine(pf.x[i], pf.y[i], pf.x[j], pf.y[j]);
        for (std::size_t k = 0; k + 1 < r.pasos.size(); ++k)
            out.push_back({r.pasos[k].px, r.pasos[k].py});
    }
}

void drawPolyFill(const PolyFill& pf) {
    if (!pf.visible) return;
    const unsigned char* col = guiPalette(pf.color);
    int nv = (pf.kind == 0) ? 3 : 4;

    for (int i = 0; i < nv; ++i) {
        int j = (i + 1) % nv;
        segThick(pxOfX(pf.x[i]), pyOfY(pf.y[i]),
                 pxOfX(pf.x[j]), pyOfY(pf.y[j]), col, pf.width);
    }

    if (pf.fillMode == 0) return;

    std::vector<ElPt> bnd;
    polyBoundary(pf, bnd);
    if (bnd.size() < 2) return;

    int pi = pf.pivot;
    if (pi < 0 || pi >= nv) pi = 0;
    double ox = pxOfX(pf.x[pi]);
    double oy = pyOfY(pf.y[pi]);

    if (pf.fillMode == 1) {
        for (const ElPt& e : bnd) {
            g_app.canvas.line(ox, oy, pxOfX(e.x), pyOfY(e.y),
                              col[0], col[1], col[2]);
        }
    } else {
        for (std::size_t i = 0; i + 1 < bnd.size(); ++i) {
            g_app.canvas.fillTriangle(ox, oy,
                                      pxOfX(bnd[i].x), pyOfY(bnd[i].y),
                                      pxOfX(bnd[i + 1].x), pyOfY(bnd[i + 1].y),
                                      col[0], col[1], col[2]);
        }
    }
}

void renderScene() {
    Canvas& cv = g_app.canvas;
    cv.clear();

    double xL = worldX(0.0);
    double xR = worldX(cv.w);
    double yB = worldY(cv.h);
    double yT = worldY(0.0);

    double stepX = niceStep(1.0 / g_app.s, 60.0);
    double stepY = niceStep(1.0 / g_app.s, 60.0);

    // Cuadricula
    if (g_gui.showGrid) {
        double x = std::ceil(xL / stepX) * stepX;
        for (; x <= xR; x += stepX) {
            double px = pxOfX(x);
            cv.line(px, 0.0, px, cv.h, GRID_C[0], GRID_C[1], GRID_C[2]);
        }
        double y = std::ceil(yB / stepY) * stepY;
        for (; y <= yT; y += stepY) {
            double py = pyOfY(y);
            cv.line(0.0, py, cv.w, py, GRID_C[0], GRID_C[1], GRID_C[2]);
        }
    }

    // Ejes
    if (g_gui.showAxes) {
        if (0.0 >= xL && 0.0 <= xR) {
            double px = pxOfX(0.0);
            cv.line(px, 0.0, px, cv.h, AXIS_C[0], AXIS_C[1], AXIS_C[2]);
        }
        if (0.0 >= yB && 0.0 <= yT) {
            double py = pyOfY(0.0);
            cv.line(0.0, py, cv.w, py, AXIS_C[0], AXIS_C[1], AXIS_C[2]);
        }
    }

    for (std::size_t i = 0; i < g_app.funcs.size(); ++i) {
        if (i < g_gui.funcs.size()) drawFunc(static_cast<int>(i));
    }

    for (const ConicRow& c : g_gui.conics) {
        drawConic(c);
    }

    for (const DdaLine& d : g_gui.ddas) {
        drawDda(d);
    }

    for (const PolyFill& pf : g_gui.polys) {
        drawPolyFill(pf);
    }

    drawPointsPoly();
    drawCustomConns();
}

void gridNumber(char* out, std::size_t sz, double v) {
    if (std::fabs(v) < 1e-12) {
        std::snprintf(out, sz, "0");
        return;
    }
    std::snprintf(out, sz, "%.6g", v);
    char* e = out + std::strlen(out);
    if (std::strchr(out, '.') && e > out) {
        --e;
        while (e > out && *e == '0') {
            *e = '\0';
            --e;
        }
        if (e > out && *e == '.') *e = '\0';
    }
}

void drawPlaneTextOverlay() {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) return;
    if (g_app.canvas.w <= 0 || g_app.canvas.h <= 0) return;

    ImFont* small = nullptr;
    ImFontAtlas* at = io.Fonts;
    if (at && at->Fonts.Size > 1) small = at->Fonts[0];
    ImFont* font = small ? small : io.FontDefault;
    if (!font) font = ImGui::GetFont();

    double fxp = static_cast<double>(io.DisplaySize.x) / g_app.canvas.w;
    double fyp = static_cast<double>(io.DisplaySize.y) / g_app.canvas.h;
    auto toPtX = [&](double fbpx) { return static_cast<float>(fbpx * fxp); };
    auto toPtY = [&](double fbpy) { return static_cast<float>(fbpy * fyp); };

    if (g_gui.showGrid && g_gui.showTicks) {
        double xL = worldX(0.0);
        double xR = worldX(g_app.canvas.w);
        double yB = worldY(g_app.canvas.h);
        double yT = worldY(0.0);
        double stepX = niceStep(1.0 / g_app.s, 60.0);
        double stepY = niceStep(1.0 / g_app.s, 60.0);
        ImU32 colTick = IM_COL32(105, 105, 105, 255);
        float botY = toPtY(g_app.canvas.h - 5.0);
        float lefX = 5.0f;
        double x = std::ceil(xL / stepX) * stepX;
        for (; x <= xR; x += stepX) {
            if (std::fabs(x) < stepX * 1e-9) continue;
            float px = toPtX(pxOfX(x));
            if (px < 2.0f || px > io.DisplaySize.x - 8.0f) continue;
            char buf[32];
            gridNumber(buf, sizeof(buf), x);
            dl->AddText(font, 13.0f, ImVec2(px + 2.0f, botY - 12.0f), colTick, buf);
        }
        double y = std::ceil(yB / stepY) * stepY;
        for (; y <= yT; y += stepY) {
            if (std::fabs(y) < stepY * 1e-9) continue;
            float py = toPtY(pyOfY(y));
            if (py < 10.0f || py > io.DisplaySize.y - 8.0f) continue;
            char buf[32];
            gridNumber(buf, sizeof(buf), y);
            dl->AddText(font, 13.0f, ImVec2(lefX, py - 6.0f), colTick, buf);
        }
    }

    if (g_gui.showLabels) {
        for (std::size_t i = 0; i < g_gui.pts.size(); ++i) {
            double ax = pxOfX(g_gui.pts[i].x);
            double ay = pyOfY(g_gui.pts[i].y);
            if (ax < -10 || ax > g_app.canvas.w + 10 || ay < -10 || ay > g_app.canvas.h + 10)
                continue;
            char lab[16];
            guiPointLabel(static_cast<int>(i), lab, sizeof(lab));
            float px = toPtX(ax) + 3.0f;
            float py = toPtY(ay) - 3.0f;

            // Fondo semi-transparente para mejor legibilidad
            ImVec2 ts = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, lab);
            dl->AddRectFilled(ImVec2(px - 1.0f, py - 1.0f),
                              ImVec2(px + ts.x + 2.0f, py + ts.y + 1.0f),
                              IM_COL32(255, 255, 255, 200), 2.0f);
            dl->AddText(font, 13.0f, ImVec2(px, py),
                        IM_COL32(25, 50, 120, 255), lab);
        }
    }

    float ly = 50.0f;  // debajo del toolbar
    for (std::size_t i = 0; i < g_gui.funcs.size(); ++i) {
        if (!g_gui.funcs[i].visible) continue;
        char name[16];
        std::snprintf(name, sizeof(name), "f%zu", i + 1);
        const unsigned char* c = guiPalette(g_gui.funcs[i].color);
        ImU32 col = IM_COL32(c[0], c[1], c[2], 255);
        std::string txt = std::string(name) + "(x) = " + trimStr(g_gui.funcs[i].expr);
        if (txt.size() > 50) txt = txt.substr(0, 49) + "...";
        ImVec2 pos(8.0f, ly);
        ImVec2 ts = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, txt.c_str());
        // Fondo redondeado
        dl->AddRectFilled(ImVec2(pos.x - 4.0f, pos.y - 3.0f),
                          ImVec2(pos.x + ts.x + 8.0f, pos.y + ts.y + 3.0f),
                          IM_COL32(255, 255, 255, 220), 4.0f);
        // Barra de color lateral
        dl->AddRectFilled(ImVec2(pos.x - 4.0f, pos.y - 3.0f),
                          ImVec2(pos.x - 1.0f, pos.y + ts.y + 3.0f),
                          col, 4.0f, ImDrawFlags_RoundCornersLeft);
        dl->AddText(font, 13.0f, ImVec2(pos.x + 2.0f, pos.y), col, txt.c_str());
        ly += ts.y + 6.0f;
        if (ly > io.DisplaySize.y * 0.4f) break;
    }

    for (std::size_t i = 0; i < g_gui.conics.size(); ++i) {
        const ConicRow& c = g_gui.conics[i];
        if (!c.visible) continue;
        const unsigned char* cc = guiPalette(c.color);
        ImU32 col = IM_COL32(cc[0], cc[1], cc[2], 255);
        char txt[112];
        if (c.type == 1) {
            std::snprintf(txt, sizeof(txt), "Circunferencia: R=%d  (%d,%d)", c.r, c.xc, c.yc);
        } else if (c.type == 0) {
            std::snprintf(txt, sizeof(txt), "Elipse: Rx=%d Ry=%d  (%d,%d)", c.rx, c.ry, c.xc, c.yc);
        } else if (c.type == 2) {
            std::snprintf(txt, sizeof(txt), "Parabola: p=%d  vertice (%d,%d)", c.p, c.xc, c.yc);
        } else {
            std::snprintf(txt, sizeof(txt), "Hiperbola: a=%d b=%d  centro (%d,%d)", c.a, c.b, c.xc, c.yc);
        }
        ImVec2 pos(8.0f, ly);
        ImVec2 ts = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, txt);
        dl->AddRectFilled(ImVec2(pos.x - 4.0f, pos.y - 3.0f),
                          ImVec2(pos.x + ts.x + 8.0f, pos.y + ts.y + 3.0f),
                          IM_COL32(255, 255, 255, 220), 4.0f);
        dl->AddRectFilled(ImVec2(pos.x - 4.0f, pos.y - 3.0f),
                          ImVec2(pos.x - 1.0f, pos.y + ts.y + 3.0f),
                          col, 4.0f, ImDrawFlags_RoundCornersLeft);
        dl->AddText(font, 13.0f, ImVec2(pos.x + 2.0f, pos.y), col, txt);
        ly += ts.y + 6.0f;
        if (ly > io.DisplaySize.y * 0.6f) break;
    }
}

void drawCursorReadout() {
    if (guiWantsMouse()) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (!dl) return;
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mp = io.MousePos;
    if (mp.x < 0.0f || mp.y < 0.0f) return;
    if (mp.x > io.DisplaySize.x || mp.y > io.DisplaySize.y) return;
    if (g_app.canvas.w <= 0 || g_app.canvas.h <= 0) return;

    double fxp = static_cast<double>(io.DisplaySize.x) / g_app.canvas.w;
    double fyp = static_cast<double>(io.DisplaySize.y) / g_app.canvas.h;
    double wx = worldX(mp.x / fxp);
    double wy = worldY(mp.y / fyp);

    // Crosshairs tenues
    ImU32 crossCol = IM_COL32(100, 140, 200, 50);
    dl->AddLine(ImVec2(mp.x, 42.0f), ImVec2(mp.x, io.DisplaySize.y), crossCol);
    dl->AddLine(ImVec2(0.0f, mp.y), ImVec2(io.DisplaySize.x, mp.y), crossCol);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "x = %.5g    y = %.5g", wx, wy);

    ImFont* font = io.FontDefault ? io.FontDefault : ImGui::GetFont();
    ImVec2 tsize = font->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, buf);
    ImVec2 pos(8.0f, io.DisplaySize.y - tsize.y - 10.0f);
    dl->AddRectFilled(ImVec2(pos.x - 4.0f, pos.y - 4.0f),
                      ImVec2(pos.x + tsize.x + 8.0f, pos.y + tsize.y + 4.0f),
                      IM_COL32(245, 247, 252, 240), 6.0f);
    dl->AddRect(ImVec2(pos.x - 4.0f, pos.y - 4.0f),
                ImVec2(pos.x + tsize.x + 8.0f, pos.y + tsize.y + 4.0f),
                IM_COL32(180, 195, 220, 200), 6.0f);
    dl->AddText(font, 13.0f, ImVec2(pos.x + 2.0f, pos.y),
                IM_COL32(40, 60, 100, 255), buf);
}

double fbScaleX() {
    int ww = 0, wh = 0, fw = 0, fh = 0;
    glfwGetWindowSize(g_app.window, &ww, &wh);
    glfwGetFramebufferSize(g_app.window, &fw, &fh);
    if (ww <= 0) return 1.0;
    return static_cast<double>(fw) / ww;
}

void cursorCallback(GLFWwindow*, double mx, double my) {
    if (guiWantsMouse()) return;
    double k = fbScaleX();
    mx *= k;
    my *= k;
    if (g_app.dragging) {
        double dcx = (mx - g_app.lastMx) / g_app.s;
        double dcy = (my - g_app.lastMy) / g_app.s;
        g_app.cx -= dcx;
        g_app.cy += dcy;
        g_app.lastMx = mx;
        g_app.lastMy = my;
    }
}

void mouseButtonCallback(GLFWwindow*, int button, int action, int) {
    if (guiWantsMouse()) {
        g_app.dragging = false;
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            g_app.dragging = true;
            double k = fbScaleX();
            double mx, my;
            glfwGetCursorPos(g_app.window, &mx, &my);
            g_app.lastMx = mx * k;
            g_app.lastMy = my * k;
        } else if (action == GLFW_RELEASE) {
            g_app.dragging = false;
        }
    }
}

void scrollCallback(GLFWwindow*, double, double yoff) {
    if (yoff == 0.0) return;
    if (guiWantsMouse()) return;
    double k = fbScaleX();
    double mx, my;
    glfwGetCursorPos(g_app.window, &mx, &my);
    mx *= k;
    my *= k;
    double wx = worldX(mx);
    double wy = worldY(my);
    double f = std::pow(1.2, yoff);
    g_app.s = std::max(1e-9, std::min(1e12, g_app.s * f));
    double nx = wx - (mx - g_app.canvas.w / 2.0) / g_app.s;
    double ny = wy + (my - g_app.canvas.h / 2.0) / g_app.s;
    if (nx > -1e308 && nx < 1e308 && std::isfinite(nx)) g_app.cx = nx;
    if (ny > -1e308 && ny < 1e308 && std::isfinite(ny)) g_app.cy = ny;
}

void glfwErrorCallback(int, const char* msg) {
    std::fprintf(stderr, "GLFW: %s\n", msg);
}

}  // namespace

int main() {
    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        std::fprintf(stderr, "no se pudo inicializar GLFW\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    g_app.window = glfwCreateWindow(1280, 800, "Graficador Doker",
                                    nullptr, nullptr);
    if (!g_app.window) {
        std::fprintf(stderr, "no se pudo crear la ventana GLFW\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(g_app.window);
    glfwSwapInterval(1);

    glfwSetCursorPosCallback(g_app.window, cursorCallback);
    glfwSetMouseButtonCallback(g_app.window, mouseButtonCallback);
    glfwSetScrollCallback(g_app.window, scrollCallback);

    guiInit(g_app.window);

    GLuint prog = makeProgram();
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    float quad[24] = {
        -1, -1, 0, 1,
         1, -1, 1, 1,
         1,  1, 1, 0,
        -1, -1, 0, 1,
         1,  1, 1, 0,
        -1,  1, 0, 0,
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void*)(2 * sizeof(float)));

    glGenTextures(1, &g_app.tex);
    glBindTexture(GL_TEXTURE_2D, g_app.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "uTex"), 0);

    g_gui.wantFitAll = true;
    bool wasR = false, wasH = false, wasQ = false, wasEsc = false;

    while (!glfwWindowShouldClose(g_app.window)) {
        glfwPollEvents();

        if (!guiWantsKeyboard()) {
            int stateR = glfwGetKey(g_app.window, GLFW_KEY_R);
            bool downR = stateR == GLFW_PRESS;
            if (downR && !wasR) g_gui.wantFitAll = true;
            wasR = downR;

            int stateH = glfwGetKey(g_app.window, GLFW_KEY_H);
            bool downH = stateH == GLFW_PRESS;
            if (downH && !wasH) g_gui.showPanel = !g_gui.showPanel;
            wasH = downH;

            int stateQ = glfwGetKey(g_app.window, GLFW_KEY_Q);
            bool downQ = stateQ == GLFW_PRESS;
            if (downQ && !wasQ) glfwSetWindowShouldClose(g_app.window, GLFW_TRUE);
            wasQ = downQ;

            int stateEsc = glfwGetKey(g_app.window, GLFW_KEY_ESCAPE);
            bool downEsc = stateEsc == GLFW_PRESS;
            if (downEsc && !wasEsc) glfwSetWindowShouldClose(g_app.window, GLFW_TRUE);
            wasEsc = downEsc;
        } else {
            wasR = wasH = wasQ = wasEsc = false;
        }

        int fw = 0, fh = 0;
        glfwGetFramebufferSize(g_app.window, &fw, &fh);
        if (fw < 1 || fh < 1) continue;
        g_app.canvas.resize(fw, fh);

        guiBeginFrame();
        guiBuild();

        bool resampled = false;
        if (g_gui.wantResample) {
            resampleAll();
            g_gui.wantResample = false;
            resampled = true;
        }
        bool hasVis = false;
        for (std::size_t i = 0; i < g_gui.funcs.size(); ++i) {
            if (g_gui.funcs[i].visible && i < g_app.funcs.size() &&
                g_app.funcs[i].good) {
                hasVis = true;
                break;
            }
        }
        if (resampled && g_gui.autoFit && hasVis && g_gui.viewMode == 0)
            g_gui.wantFitAll = true;

        if (g_gui.wantFitAll) {
            g_gui.viewMode = 0;
            doFitAll();
            g_gui.wantFitAll = false;
        }

        if (g_gui.wantApplyRange) {
            g_gui.viewMode = 1;
            applyView(g_gui.vx0, g_gui.vx1, g_gui.vy0, g_gui.vy1);
            g_gui.wantApplyRange = false;
        }

        renderScene();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_app.tex);
        if (g_app.texW != g_app.canvas.w || g_app.texH != g_app.canvas.h) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_app.canvas.w, g_app.canvas.h,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            g_app.texW = g_app.canvas.w;
            g_app.texH = g_app.canvas.h;
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_app.canvas.w, g_app.canvas.h,
                        GL_RGBA, GL_UNSIGNED_BYTE, g_app.canvas.px.data());

        glViewport(0, 0, fw, fh);
        glUseProgram(prog);
        glBindVertexArray(vao);
        glClearColor(0.97f, 0.97f, 0.97f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        drawPlaneTextOverlay();
        drawCursorReadout();

        guiEndFrame();

        glfwSwapBuffers(g_app.window);
    }

    glfwDestroyWindow(g_app.window);
    glfwTerminate();
    return 0;
}
