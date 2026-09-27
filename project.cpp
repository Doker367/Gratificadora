#include "project.h"

#include <cctype>
#include <cmath>
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

// =========================================================
//  Utilidades de texto
// =========================================================
std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string toLower(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return s;
}

bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() &&
           s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::string jsonEsc(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            default: o.push_back(c);
        }
    }
    return o;
}

std::vector<std::string> splitSep(const std::string& line, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == sep) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(trim(cur));
    return out;
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

// =========================================================
//  Parser JSON minimo (objeto/array/string/numero/bool/null)
// =========================================================
struct JVal {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ };
    T t = NUL;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JVal> arr;
    std::map<std::string, JVal> obj;

    const JVal* get(const std::string& k) const {
        if (t != OBJ) return nullptr;
        std::map<std::string, JVal>::const_iterator it = obj.find(k);
        return it == obj.end() ? nullptr : &it->second;
    }
};

struct JParser {
    const std::string& s;
    std::size_t i;
    std::string err;
    explicit JParser(const std::string& text) : s(text), i(0) {}

    void ws() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    bool fail(const char* m) {
        if (err.empty()) err = std::string("JSON: ") + m;
        return false;
    }
    bool parse(JVal& v) {
        ws();
        return value(v);
    }
    bool value(JVal& v) {
        ws();
        if (i >= s.size()) return fail("fin inesperado");
        char c = s[i];
        if (c == '{') return object(v);
        if (c == '[') return array(v);
        if (c == '"') {
            v.t = JVal::STR;
            return string(v.str);
        }
        if (c == 't' || c == 'f') return boolean(v);
        if (c == 'n') {
            if (s.compare(i, 4, "null") == 0) {
                i += 4;
                v.t = JVal::NUL;
                return true;
            }
            return fail("literal");
        }
        return number(v);
    }
    bool string(std::string& out) {
        ++i;  // comilla inicial
        out.clear();
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i >= s.size()) return fail("escape");
                char e = s[i++];
                switch (e) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case '/': out.push_back('/'); break;
                    case '\\': out.push_back('\\'); break;
                    case '"': out.push_back('"'); break;
                    case 'u': {
                        if (i + 4 > s.size()) return fail("\\u");
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                            else return fail("hex");
                        }
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: out.push_back(e);
                }
            } else {
                out.push_back(c);
            }
        }
        return fail("comilla");
    }
    bool number(JVal& v) {
        std::size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && s[i] == '.') {
            ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        if (i == start) return fail("numero");
        v.t = JVal::NUM;
        v.num = std::strtod(s.substr(start, i - start).c_str(), nullptr);
        return true;
    }
    bool boolean(JVal& v) {
        if (s.compare(i, 4, "true") == 0) {
            i += 4;
            v.t = JVal::BOOL;
            v.b = true;
            return true;
        }
        if (s.compare(i, 5, "false") == 0) {
            i += 5;
            v.t = JVal::BOOL;
            v.b = false;
            return true;
        }
        return fail("booleano");
    }
    bool array(JVal& v) {
        ++i;
        v.t = JVal::ARR;
        ws();
        if (i < s.size() && s[i] == ']') {
            ++i;
            return true;
        }
        for (;;) {
            JVal e;
            if (!value(e)) return false;
            v.arr.push_back(e);
            ws();
            if (i >= s.size()) return fail("array");
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == ']') {
                ++i;
                return true;
            }
            return fail("separador de array");
        }
    }
    bool object(JVal& v) {
        ++i;
        v.t = JVal::OBJ;
        ws();
        if (i < s.size() && s[i] == '}') {
            ++i;
            return true;
        }
        for (;;) {
            ws();
            if (i >= s.size() || s[i] != '"') return fail("clave");
            std::string key;
            if (!string(key)) return false;
            ws();
            if (i >= s.size() || s[i] != ':') return fail("':'");
            ++i;
            JVal e;
            if (!value(e)) return false;
            v.obj[key] = e;
            ws();
            if (i >= s.size()) return fail("objeto");
            if (s[i] == ',') {
                ++i;
                continue;
            }
            if (s[i] == '}') {
                ++i;
                return true;
            }
            return fail("separador de objeto");
        }
    }
};

double jnum(const JVal& o, const char* k, double def) {
    const JVal* v = o.get(k);
    return (v && v->t == JVal::NUM) ? v->num : def;
}
int jint(const JVal& o, const char* k, int def) {
    const JVal* v = o.get(k);
    return (v && v->t == JVal::NUM) ? static_cast<int>(std::llround(v->num)) : def;
}
bool jbool(const JVal& o, const char* k, bool def) {
    const JVal* v = o.get(k);
    return (v && v->t == JVal::BOOL) ? v->b : def;
}
std::string jstr(const JVal& o, const char* k, const std::string& def) {
    const JVal* v = o.get(k);
    return (v && v->t == JVal::STR) ? v->str : def;
}

// =========================================================
//  Aplicar datos cargados al estado global
// =========================================================
void applyLoaded(std::vector<FunctionRow>& funcs, std::vector<PointRow>& pts,
                 std::vector<SegConnection>& conns, std::vector<ConicRow>& conics,
                 std::vector<DdaLine>& ddas, std::vector<PolyFill>& polys) {
    g_gui.funcs.swap(funcs);
    g_gui.pts.swap(pts);
    g_gui.conns.swap(conns);
    g_gui.conics.swap(conics);
    g_gui.ddas.swap(ddas);
    g_gui.polys.swap(polys);
    g_gui.segs.clear();
    guiRecomputeSegs();
    g_gui.wantResample = true;
}

void setLabel(PointRow& p, const std::string& lab, std::size_t idx) {
    if (!lab.empty()) {
        std::snprintf(p.label, sizeof(p.label), "%s", lab.c_str());
    } else {
        guiPointLabel(static_cast<int>(idx), p.label, sizeof(p.label));
    }
}

// =========================================================
//  .graf (texto plano con secciones)
// =========================================================
bool saveGraf(const std::string& path, std::string& err) {
    std::ofstream f(path.c_str());
    if (!f) {
        err = "no se pudo crear el archivo";
        return false;
    }
    f << "# Graficador - proyecto (.graf)\n";
    f << "# Cada linea = un registro con pares clave=valor.\n";
    f << "# En [funciones], la clave expr= va al final (toma el resto de la linea).\n\n";

    f << "[funciones]\n";
    for (std::size_t i = 0; i < g_gui.funcs.size(); ++i) {
        const FunctionRow& r = g_gui.funcs[i];
        f << "visible=" << (r.visible ? 1 : 0)
          << " color=" << r.color << " style=" << r.style
          << " width=" << r.width << " expr=" << r.expr << "\n";
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
          << " color=" << c.color << " style=" << c.style
          << " width=" << c.width
          << " showLabel=" << (c.showLabel ? 1 : 0) << "\n";
    }

    f << "\n[conicas]\n";
    for (std::size_t i = 0; i < g_gui.conics.size(); ++i) {
        const ConicRow& c = g_gui.conics[i];
        f << "type=" << c.type << " xc=" << c.xc << " yc=" << c.yc
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
        for (int j = 0; j < 4; ++j)
            f << " x" << j << "=" << p.x[j] << " y" << j << "=" << p.y[j];
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

bool loadGraf(const std::string& path, std::string& err) {
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
            setLabel(p, kvStr(m, "label", ""), pts.size());
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
    }

    applyLoaded(funcs, pts, conns, conics, ddas, polys);
    return true;
}

// =========================================================
//  .csv  (tabla de puntos: x,y,label)
// =========================================================
bool saveCsv(const std::string& path, std::string& err) {
    std::ofstream f(path.c_str());
    if (!f) {
        err = "no se pudo crear el archivo";
        return false;
    }
    f << std::setprecision(9);
    f << "x,y,label\n";
    for (std::size_t i = 0; i < g_gui.pts.size(); ++i) {
        const PointRow& p = g_gui.pts[i];
        f << p.x << "," << p.y << "," << (p.label[0] ? p.label : "") << "\n";
    }
    if (!f.good()) {
        err = "error de escritura";
        return false;
    }
    return true;
}

bool loadCsv(const std::string& path, std::string& err) {
    std::ifstream f(path.c_str());
    if (!f) {
        err = "no se pudo abrir el archivo";
        return false;
    }
    std::vector<PointRow> pts;
    std::string line;
    char sep = ',';
    bool firstData = true;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (firstData) {
            firstData = false;
            if (t.find(',') == std::string::npos) {
                if (t.find(';') != std::string::npos) sep = ';';
                else if (t.find('\t') != std::string::npos) sep = '\t';
            }
        }
        std::vector<std::string> cols = splitSep(t, sep);
        if (cols.empty() || cols[0].empty()) continue;
        char* endp = nullptr;
        std::strtod(cols[0].c_str(), &endp);
        if (endp == cols[0].c_str()) continue;  // encabezado o linea no numerica
        PointRow p;
        p.x = static_cast<float>(std::strtod(cols[0].c_str(), nullptr));
        p.y = (cols.size() > 1) ? static_cast<float>(std::strtod(cols[1].c_str(), nullptr))
                                : 0.0f;
        p.visible = true;
        std::string lab = (cols.size() > 2) ? cols[2] : "";
        setLabel(p, lab, pts.size());
        pts.push_back(p);
    }
    if (pts.empty()) {
        err = "el CSV no contiene puntos (formato esperado: x,y,label)";
        return false;
    }
    std::vector<FunctionRow> funcs;
    std::vector<SegConnection> conns;
    std::vector<ConicRow> conics;
    std::vector<DdaLine> ddas;
    std::vector<PolyFill> polys;
    applyLoaded(funcs, pts, conns, conics, ddas, polys);
    return true;
}

// =========================================================
//  .json  (proyecto completo)
// =========================================================
bool saveJson(const std::string& path, std::string& err) {
    std::ofstream f(path.c_str());
    if (!f) {
        err = "no se pudo crear el archivo";
        return false;
    }
    f << std::setprecision(9);
    f << "{\n";

    f << "  \"funciones\": [";
    for (std::size_t i = 0; i < g_gui.funcs.size(); ++i) {
        const FunctionRow& r = g_gui.funcs[i];
        if (i) f << ",";
        f << "\n    {\"expr\": \"" << jsonEsc(r.expr) << "\", \"visible\": "
          << (r.visible ? "true" : "false") << ", \"color\": " << r.color
          << ", \"style\": " << r.style << ", \"width\": " << r.width << "}";
    }
    if (!g_gui.funcs.empty()) f << "\n  ";
    f << "],\n";

    f << "  \"puntos\": [";
    for (std::size_t i = 0; i < g_gui.pts.size(); ++i) {
        const PointRow& p = g_gui.pts[i];
        if (i) f << ",";
        f << "\n    {\"x\": " << p.x << ", \"y\": " << p.y
          << ", \"visible\": " << (p.visible ? "true" : "false")
          << ", \"label\": \"" << jsonEsc(p.label) << "\"}";
    }
    if (!g_gui.pts.empty()) f << "\n  ";
    f << "],\n";

    f << "  \"conexiones\": [";
    for (std::size_t i = 0; i < g_gui.conns.size(); ++i) {
        const SegConnection& c = g_gui.conns[i];
        if (i) f << ",";
        f << "\n    {\"from\": " << c.fromIdx << ", \"to\": " << c.toIdx
          << ", \"enabled\": " << (c.enabled ? "true" : "false")
          << ", \"color\": " << c.color << ", \"style\": " << c.style
          << ", \"width\": " << c.width
          << ", \"showLabel\": " << (c.showLabel ? "true" : "false") << "}";
    }
    if (!g_gui.conns.empty()) f << "\n  ";
    f << "],\n";

    f << "  \"conicas\": [";
    for (std::size_t i = 0; i < g_gui.conics.size(); ++i) {
        const ConicRow& c = g_gui.conics[i];
        if (i) f << ",";
        f << "\n    {\"type\": " << c.type << ", \"xc\": " << c.xc
          << ", \"yc\": " << c.yc << ", \"r\": " << c.r << ", \"rx\": " << c.rx
          << ", \"ry\": " << c.ry << ", \"p\": " << c.p << ", \"a\": " << c.a
          << ", \"b\": " << c.b << ", \"orient\": " << c.orient
          << ", \"alcance\": " << c.alcance << ", \"color\": " << c.color
          << ", \"width\": " << c.width << ", \"fill\": " << c.fillMode
          << ", \"visible\": " << (c.visible ? "true" : "false") << "}";
    }
    if (!g_gui.conics.empty()) f << "\n  ";
    f << "],\n";

    f << "  \"ddas\": [";
    for (std::size_t i = 0; i < g_gui.ddas.size(); ++i) {
        const DdaLine& d = g_gui.ddas[i];
        if (i) f << ",";
        f << "\n    {\"xa\": " << d.xa << ", \"ya\": " << d.ya
          << ", \"xb\": " << d.xb << ", \"yb\": " << d.yb
          << ", \"color\": " << d.color << ", \"width\": " << d.width
          << ", \"visible\": " << (d.visible ? "true" : "false") << "}";
    }
    if (!g_gui.ddas.empty()) f << "\n  ";
    f << "],\n";

    f << "  \"polys\": [";
    for (std::size_t i = 0; i < g_gui.polys.size(); ++i) {
        const PolyFill& p = g_gui.polys[i];
        if (i) f << ",";
        f << "\n    {\"kind\": " << p.kind << ", \"n\": " << p.n
          << ", \"pivot\": " << p.pivot << ", \"fill\": " << p.fillMode
          << ", \"color\": " << p.color << ", \"width\": " << p.width
          << ", \"visible\": " << (p.visible ? "true" : "false") << ", \"x\": ["
          << p.x[0] << ", " << p.x[1] << ", " << p.x[2] << ", " << p.x[3]
          << "], \"y\": [" << p.y[0] << ", " << p.y[1] << ", " << p.y[2]
          << ", " << p.y[3] << "]}";
    }
    if (!g_gui.polys.empty()) f << "\n  ";
    f << "],\n";

    f << "  \"vista\": {\"ra\": " << g_gui.ra << ", \"rb\": " << g_gui.rb
      << ", \"rn\": " << g_gui.rn
      << ", \"autoFit\": " << (g_gui.autoFit ? "true" : "false")
      << ", \"viewMode\": " << g_gui.viewMode
      << ", \"vx0\": " << g_gui.vx0 << ", \"vx1\": " << g_gui.vx1
      << ", \"vy0\": " << g_gui.vy0 << ", \"vy1\": " << g_gui.vy1
      << ", \"showGrid\": " << (g_gui.showGrid ? "true" : "false")
      << ", \"showAxes\": " << (g_gui.showAxes ? "true" : "false")
      << ", \"showLabels\": " << (g_gui.showLabels ? "true" : "false")
      << ", \"showTicks\": " << (g_gui.showTicks ? "true" : "false")
      << ", \"connectAll\": " << (g_gui.connectAll ? "true" : "false")
      << ", \"darkCanvas\": " << (g_gui.darkCanvas ? "true" : "false")
      << ", \"polyColor\": " << g_gui.polyColor
      << ", \"polyStyle\": " << g_gui.polyStyle
      << ", \"polyWidth\": " << g_gui.polyWidth << "}\n";

    f << "}\n";
    if (!f.good()) {
        err = "error de escritura";
        return false;
    }
    return true;
}

bool loadJson(const std::string& path, std::string& err) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) {
        err = "no se pudo abrir el archivo";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();

    JVal root;
    JParser p(text);
    if (!p.parse(root) || root.t != JVal::OBJ) {
        err = p.err.empty() ? "JSON invalido" : p.err;
        return false;
    }

    std::vector<FunctionRow> funcs;
    std::vector<PointRow> pts;
    std::vector<SegConnection> conns;
    std::vector<ConicRow> conics;
    std::vector<DdaLine> ddas;
    std::vector<PolyFill> polys;

    const JVal* a;

    if ((a = root.get("funciones")) && a->t == JVal::ARR) {
        for (const JVal& e : a->arr) {
            FunctionRow r;
            r.visible = jbool(e, "visible", true);
            r.color = jint(e, "color", 0);
            r.style = jint(e, "style", 0);
            r.width = jint(e, "width", 2);
            std::string expr = jstr(e, "expr", "");
            std::snprintf(r.expr, sizeof(r.expr), "%s", expr.c_str());
            Expr chk;
            if (Expr::parse(trim(expr), chk)) r.good = true;
            else {
                r.good = false;
                r.err = chk.err;
            }
            funcs.push_back(r);
        }
    }

    if ((a = root.get("puntos")) && a->t == JVal::ARR) {
        for (const JVal& e : a->arr) {
            PointRow p2;
            p2.x = static_cast<float>(jnum(e, "x", 0.0));
            p2.y = static_cast<float>(jnum(e, "y", 0.0));
            p2.visible = jbool(e, "visible", true);
            setLabel(p2, jstr(e, "label", ""), pts.size());
            pts.push_back(p2);
        }
    }

    if ((a = root.get("conexiones")) && a->t == JVal::ARR) {
        for (const JVal& e : a->arr) {
            SegConnection c;
            c.fromIdx = jint(e, "from", 0);
            c.toIdx = jint(e, "to", 1);
            c.enabled = jbool(e, "enabled", true);
            c.color = jint(e, "color", 0);
            c.style = jint(e, "style", 0);
            c.width = jint(e, "width", 2);
            c.showLabel = jbool(e, "showLabel", true);
            conns.push_back(c);
        }
    }

    if ((a = root.get("conicas")) && a->t == JVal::ARR) {
        for (const JVal& e : a->arr) {
            ConicRow c;
            c.type = jint(e, "type", 0);
            c.xc = jint(e, "xc", 100);
            c.yc = jint(e, "yc", 150);
            c.r = jint(e, "r", 10);
            c.rx = jint(e, "rx", 8);
            c.ry = jint(e, "ry", 6);
            c.p = jint(e, "p", 5);
            c.a = jint(e, "a", 5);
            c.b = jint(e, "b", 3);
            c.orient = jint(e, "orient", 0);
            c.alcance = jint(e, "alcance", 25);
            c.color = jint(e, "color", 0);
            c.width = jint(e, "width", 1);
            c.fillMode = jint(e, "fill", 0);
            c.visible = jbool(e, "visible", true);
            conics.push_back(c);
        }
    }

    if ((a = root.get("ddas")) && a->t == JVal::ARR) {
        for (const JVal& e : a->arr) {
            DdaLine d;
            d.xa = static_cast<float>(jnum(e, "xa", 0.0));
            d.ya = static_cast<float>(jnum(e, "ya", 0.0));
            d.xb = static_cast<float>(jnum(e, "xb", 1.0));
            d.yb = static_cast<float>(jnum(e, "yb", 1.0));
            d.color = jint(e, "color", 0);
            d.width = jint(e, "width", 1);
            d.visible = jbool(e, "visible", true);
            ddas.push_back(d);
        }
    }

    if ((a = root.get("polys")) && a->t == JVal::ARR) {
        for (const JVal& e : a->arr) {
            PolyFill p3;
            p3.kind = jint(e, "kind", 0);
            p3.n = jint(e, "n", 3);
            p3.pivot = jint(e, "pivot", 0);
            p3.fillMode = jint(e, "fill", 1);
            p3.color = jint(e, "color", 0);
            p3.width = jint(e, "width", 1);
            p3.visible = jbool(e, "visible", true);
            const JVal* xv = e.get("x");
            const JVal* yv = e.get("y");
            if (xv && xv->t == JVal::ARR) {
                for (std::size_t k = 0; k < xv->arr.size() && k < 4; ++k)
                    if (xv->arr[k].t == JVal::NUM)
                        p3.x[k] = static_cast<float>(xv->arr[k].num);
            }
            if (yv && yv->t == JVal::ARR) {
                for (std::size_t k = 0; k < yv->arr.size() && k < 4; ++k)
                    if (yv->arr[k].t == JVal::NUM)
                        p3.y[k] = static_cast<float>(yv->arr[k].num);
            }
            polys.push_back(p3);
        }
    }

    if ((a = root.get("vista")) && a->t == JVal::OBJ) {
        g_gui.ra = static_cast<float>(jnum(*a, "ra", g_gui.ra));
        g_gui.rb = static_cast<float>(jnum(*a, "rb", g_gui.rb));
        g_gui.rn = jint(*a, "rn", g_gui.rn);
        g_gui.autoFit = jbool(*a, "autoFit", g_gui.autoFit);
        g_gui.viewMode = jint(*a, "viewMode", g_gui.viewMode);
        g_gui.vx0 = static_cast<float>(jnum(*a, "vx0", g_gui.vx0));
        g_gui.vx1 = static_cast<float>(jnum(*a, "vx1", g_gui.vx1));
        g_gui.vy0 = static_cast<float>(jnum(*a, "vy0", g_gui.vy0));
        g_gui.vy1 = static_cast<float>(jnum(*a, "vy1", g_gui.vy1));
        g_gui.showGrid = jbool(*a, "showGrid", g_gui.showGrid);
        g_gui.showAxes = jbool(*a, "showAxes", g_gui.showAxes);
        g_gui.showLabels = jbool(*a, "showLabels", g_gui.showLabels);
        g_gui.showTicks = jbool(*a, "showTicks", g_gui.showTicks);
        g_gui.connectAll = jbool(*a, "connectAll", g_gui.connectAll);
        g_gui.darkCanvas = jbool(*a, "darkCanvas", g_gui.darkCanvas);
        g_gui.polyColor = jint(*a, "polyColor", g_gui.polyColor);
        g_gui.polyStyle = jint(*a, "polyStyle", g_gui.polyStyle);
        g_gui.polyWidth = jint(*a, "polyWidth", g_gui.polyWidth);
    }

    applyLoaded(funcs, pts, conns, conics, ddas, polys);
    return true;
}

}  // namespace

// =========================================================
//  API publica: elige el formato segun la extension
// =========================================================
bool projectSave(const std::string& path, std::string& err) {
    std::string low = toLower(path);
    if (endsWith(low, ".json")) return saveJson(path, err);
    if (endsWith(low, ".csv")) return saveCsv(path, err);
    return saveGraf(path, err);
}

bool projectLoad(const std::string& path, std::string& err) {
    std::string low = toLower(path);
    if (endsWith(low, ".json")) return loadJson(path, err);
    if (endsWith(low, ".csv")) return loadCsv(path, err);
    return loadGraf(path, err);
}
