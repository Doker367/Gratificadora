#include "project.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gui.h"
#include "parser.h"

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

void parsePairs(const std::string& s, std::map<std::string, std::string>& out) {
    std::size_t pos = 0;
    const std::size_t n = s.size();
    while (pos < n) {
        while (pos < n && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
        if (pos >= n) break;
        std::size_t eq = s.find('=', pos);
        if (eq == std::string::npos) break;
        std::string key = trim(s.substr(pos, eq - pos));
        std::size_t vstart = eq + 1;
        std::size_t vend = vstart;
        while (vend < n && !std::isspace(static_cast<unsigned char>(s[vend]))) ++vend;
        std::string val = s.substr(vstart, vend - vstart);
        if (!key.empty()) out[key] = val;
        pos = vend;
    }
}

int kvInt(const std::map<std::string, std::string>& m, const char* k, int def) {
    std::map<std::string, std::string>::const_iterator it = m.find(k);
    if (it == m.end()) return def;
    return std::atoi(it->second.c_str());
}

float kvFloat(const std::map<std::string, std::string>& m, const char* k, float def) {
    std::map<std::string, std::string>::const_iterator it = m.find(k);
    if (it == m.end()) return def;
    return static_cast<float>(std::atof(it->second.c_str()));
}

std::string kvStr(const std::map<std::string, std::string>& m, const char* k,
                  const std::string& def) {
    std::map<std::string, std::string>::const_iterator it = m.find(k);
    if (it == m.end()) return def;
    return it->second;
}

}  // namespace

bool projectSave(const std::string& path, std::string& err) {
    std::ofstream f(path.c_str());
    if (!f) {
        err = "no se pudo crear el archivo";
        return false;
    }
    f << "# Graficador - proyecto (.graf)\n";
    f << "# Curvas uniformes: circunferencia, elipse, parabola, hiperbola\n";
    f << "# Cada linea = un registro con pares clave=valor.\n";
    f << "# En [funciones], la clave expr= va al final (toma el resto de la linea).\n\n";

    f << "[funciones]\n";
    for (std::size_t i = 0; i < g_gui.funcs.size(); ++i) {
        const FunctionRow& r = g_gui.funcs[i];
        f << "visible=" << (r.visible ? 1 : 0)
          << " color=" << r.color
          << " style=" << r.style
          << " width=" << r.width
          << " expr=" << r.expr << "\n";
    }

    f << "\n[puntos]\n";
    f << std::setprecision(9);
    for (std::size_t i = 0; i < g_gui.pts.size(); ++i) {
        const PointRow& p = g_gui.pts[i];
        f << "x=" << p.x << " y=" << p.y
          << " visible=" << (p.visible ? 1 : 0)
          << " label=" << (p.label[0] ? p.label : "") << "\n";
    }

    f << "\n[conexiones]\n";
    for (std::size_t i = 0; i < g_gui.conns.size(); ++i) {
        const SegConnection& c = g_gui.conns[i];
        f << "from=" << c.fromIdx << " to=" << c.toIdx
          << " enabled=" << (c.enabled ? 1 : 0)
          << " color=" << c.color
          << " style=" << c.style
          << " width=" << c.width
          << " showLabel=" << (c.showLabel ? 1 : 0) << "\n";
    }

    f << "\n[conicas]\n";
    for (std::size_t i = 0; i < g_gui.conics.size(); ++i) {
        const ConicRow& c = g_gui.conics[i];
        f << "type=" << c.type
          << " xc=" << c.xc << " yc=" << c.yc
          << " r=" << c.r << " rx=" << c.rx << " ry=" << c.ry
          << " p=" << c.p << " a=" << c.a << " b=" << c.b
          << " orient=" << c.orient << " alcance=" << c.alcance
          << " color=" << c.color << " width=" << c.width
          << " fill=" << c.fillMode
          << " visible=" << (c.visible ? 1 : 0) << "\n";
    }

    f << "\n[ddas]\n";
    for (std::size_t i = 0; i < g_gui.ddas.size(); ++i) {
        const DdaLine& d = g_gui.ddas[i];
        f << "xa=" << d.xa << " ya=" << d.ya << " xb=" << d.xb << " yb=" << d.yb
          << " color=" << d.color << " width=" << d.width
          << " visible=" << (d.visible ? 1 : 0) << "\n";
    }

    f << "\n[polys]\n";
    for (std::size_t i = 0; i < g_gui.polys.size(); ++i) {
        const PolyFill& p = g_gui.polys[i];
        f << "kind=" << p.kind << " n=" << p.n << " pivot=" << p.pivot
          << " fill=" << p.fillMode << " color=" << p.color
          << " width=" << p.width
          << " visible=" << (p.visible ? 1 : 0);
        for (int j = 0; j < 4; ++j) {
            f << " x" << j << "=" << p.x[j] << " y" << j << "=" << p.y[j];
        }
        f << "\n";
    }

    f << "\n[vista]\n";
    f << "ra=" << g_gui.ra << " rb=" << g_gui.rb << " rn=" << g_gui.rn
      << " autoFit=" << (g_gui.autoFit ? 1 : 0)
      << " viewMode=" << g_gui.viewMode
      << " vx0=" << g_gui.vx0 << " vx1=" << g_gui.vx1
      << " vy0=" << g_gui.vy0 << " vy1=" << g_gui.vy1
      << " showGrid=" << (g_gui.showGrid ? 1 : 0)
      << " showAxes=" << (g_gui.showAxes ? 1 : 0)
      << " showLabels=" << (g_gui.showLabels ? 1 : 0)
      << " showTicks=" << (g_gui.showTicks ? 1 : 0)
      << " connectAll=" << (g_gui.connectAll ? 1 : 0)
      << " darkCanvas=" << (g_gui.darkCanvas ? 1 : 0)
      << " polyColor=" << g_gui.polyColor
      << " polyStyle=" << g_gui.polyStyle
      << " polyWidth=" << g_gui.polyWidth << "\n";

    if (!f.good()) {
        err = "error de escritura";
        return false;
    }
    return true;
}

bool projectLoad(const std::string& path, std::string& err) {
    std::ifstream f(path.c_str());
    if (!f) {
        err = "no se pudo abrir el archivo";
        return false;
    }

    std::vector<FunctionRow> funcs;
    std::vector<PointRow> pts;
    std::vector<SegConnection> conns;
    std::vector<ConicRow> conics;
    std::vector<DdaLine> ddas;
    std::vector<PolyFill> polys;

    std::string section;
    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t[0] == '[') {
            std::size_t close = t.find(']');
            if (close == std::string::npos) {
                err = "seccion sin cerrar en la linea " + std::to_string(lineNo);
                return false;
            }
            section = t.substr(1, close - 1);
            continue;
        }

        std::map<std::string, std::string> m;
        if (section == "funciones") {
            std::string expr;
            std::size_t e = t.find("expr=");
            if (e != std::string::npos) {
                expr = trim(t.substr(e + 5));
                t = t.substr(0, e);
            }
            parsePairs(t, m);
            FunctionRow r;
            r.visible = kvInt(m, "visible", 1) != 0;
            r.color = kvInt(m, "color", 0);
            r.style = kvInt(m, "style", 0);
            r.width = kvInt(m, "width", 2);
            std::snprintf(r.expr, sizeof(r.expr), "%s", expr.c_str());
            Expr chk;
            if (Expr::parse(trim(expr), chk)) {
                r.good = true;
            } else {
                r.good = false;
                r.err = chk.err;
            }
            funcs.push_back(r);
        } else if (section == "puntos") {
            parsePairs(t, m);
            PointRow p;
            p.x = kvFloat(m, "x", 0.0f);
            p.y = kvFloat(m, "y", 0.0f);
            p.visible = kvInt(m, "visible", 1) != 0;
            std::string lab = kvStr(m, "label", "");
            if (!lab.empty()) {
                std::snprintf(p.label, sizeof(p.label), "%s", lab.c_str());
            } else {
                guiPointLabel(static_cast<int>(pts.size()), p.label, sizeof(p.label));
            }
            pts.push_back(p);
        } else if (section == "conexiones") {
            parsePairs(t, m);
            SegConnection c;
            c.fromIdx = kvInt(m, "from", 0);
            c.toIdx = kvInt(m, "to", 1);
            c.enabled = kvInt(m, "enabled", 1) != 0;
            c.color = kvInt(m, "color", 0);
            c.style = kvInt(m, "style", 0);
            c.width = kvInt(m, "width", 2);
            c.showLabel = kvInt(m, "showLabel", 1) != 0;
            conns.push_back(c);
        } else if (section == "conicas") {
            parsePairs(t, m);
            ConicRow c;
            c.type = kvInt(m, "type", 0);
            c.xc = kvInt(m, "xc", 100);
            c.yc = kvInt(m, "yc", 150);
            c.r = kvInt(m, "r", 10);
            c.rx = kvInt(m, "rx", 8);
            c.ry = kvInt(m, "ry", 6);
            c.p = kvInt(m, "p", 5);
            c.a = kvInt(m, "a", 5);
            c.b = kvInt(m, "b", 3);
            c.orient = kvInt(m, "orient", 0);
            c.alcance = kvInt(m, "alcance", 25);
            c.color = kvInt(m, "color", 0);
            c.width = kvInt(m, "width", 1);
            c.fillMode = kvInt(m, "fill", 0);
            c.visible = kvInt(m, "visible", 1) != 0;
            conics.push_back(c);
        } else if (section == "ddas") {
            parsePairs(t, m);
            DdaLine d;
            d.xa = kvFloat(m, "xa", 0.0f);
            d.ya = kvFloat(m, "ya", 0.0f);
            d.xb = kvFloat(m, "xb", 1.0f);
            d.yb = kvFloat(m, "yb", 1.0f);
            d.color = kvInt(m, "color", 0);
            d.width = kvInt(m, "width", 1);
            d.visible = kvInt(m, "visible", 1) != 0;
            ddas.push_back(d);
        } else if (section == "polys") {
            parsePairs(t, m);
            PolyFill p;
            p.kind = kvInt(m, "kind", 0);
            p.n = kvInt(m, "n", 3);
            p.pivot = kvInt(m, "pivot", 0);
            p.fillMode = kvInt(m, "fill", 1);
            p.color = kvInt(m, "color", 0);
            p.width = kvInt(m, "width", 1);
            p.visible = kvInt(m, "visible", 1) != 0;
            for (int j = 0; j < 4; ++j) {
                char kx[8];
                char ky[8];
                std::snprintf(kx, sizeof(kx), "x%d", j);
                std::snprintf(ky, sizeof(ky), "y%d", j);
                p.x[j] = kvFloat(m, kx, p.x[j]);
                p.y[j] = kvFloat(m, ky, p.y[j]);
            }
            polys.push_back(p);
        } else if (section == "vista") {
            parsePairs(t, m);
            g_gui.ra = kvFloat(m, "ra", g_gui.ra);
            g_gui.rb = kvFloat(m, "rb", g_gui.rb);
            g_gui.rn = kvInt(m, "rn", g_gui.rn);
            g_gui.autoFit = kvInt(m, "autoFit", g_gui.autoFit ? 1 : 0) != 0;
            g_gui.viewMode = kvInt(m, "viewMode", g_gui.viewMode);
            g_gui.vx0 = kvFloat(m, "vx0", g_gui.vx0);
            g_gui.vx1 = kvFloat(m, "vx1", g_gui.vx1);
            g_gui.vy0 = kvFloat(m, "vy0", g_gui.vy0);
            g_gui.vy1 = kvFloat(m, "vy1", g_gui.vy1);
            g_gui.showGrid = kvInt(m, "showGrid", g_gui.showGrid ? 1 : 0) != 0;
            g_gui.showAxes = kvInt(m, "showAxes", g_gui.showAxes ? 1 : 0) != 0;
            g_gui.showLabels = kvInt(m, "showLabels", g_gui.showLabels ? 1 : 0) != 0;
            g_gui.showTicks = kvInt(m, "showTicks", g_gui.showTicks ? 1 : 0) != 0;
            g_gui.connectAll = kvInt(m, "connectAll", g_gui.connectAll ? 1 : 0) != 0;
            g_gui.darkCanvas = kvInt(m, "darkCanvas", g_gui.darkCanvas ? 1 : 0) != 0;
            g_gui.polyColor = kvInt(m, "polyColor", g_gui.polyColor);
            g_gui.polyStyle = kvInt(m, "polyStyle", g_gui.polyStyle);
            g_gui.polyWidth = kvInt(m, "polyWidth", g_gui.polyWidth);
        }
        // secciones desconocidas: se ignoran
    }

    g_gui.funcs.swap(funcs);
    g_gui.pts.swap(pts);
    g_gui.conns.swap(conns);
    g_gui.conics.swap(conics);
    g_gui.ddas.swap(ddas);
    g_gui.polys.swap(polys);
    g_gui.segs.clear();
    guiRecomputeSegs();
    g_gui.wantResample = true;
    return true;
}
