#pragma once

#include <cstdio>
#include <string>
#include <vector>

enum StrokeStyle {
    SS_LINE = 0,
    SS_DASH = 1,
    SS_POINTS = 2,
    SS_LINE_POINTS = 3,
};

struct FunctionRow {
    char expr[160] = {0};
    bool visible = true;
    int color = 0;
    int style = SS_LINE;
    int width = 2;
    bool good = false;
    std::string err;

    FunctionRow() = default;
    explicit FunctionRow(const char* s) {
        std::snprintf(expr, sizeof(expr), "%s", s);
    }
};

struct PointRow {
    float x = 0.0f;
    float y = 0.0f;
    char label[8] = {0};  // etiqueta personalizable
    bool visible = true;
};

struct SegConnection {
    int fromIdx = 0;
    int toIdx = 1;
    bool enabled = true;
    int color = 0;
    int style = SS_LINE;
    int width = 2;
    bool showLabel = true;
};

struct ConicRow {
    int type = 0;      // 0 = elipse, 1 = circunferencia, 2 = parabola, 3 = hiperbola
    int xc = 100;
    int yc = 150;
    int rx = 8;
    int ry = 6;
    int r = 10;
    int p = 5;         // parabola: distancia focal (vertice -> foco)
    int a = 5;         // hiperbola: semieje transversal
    int b = 3;         // hiperbola: semieje conjugado
    int orient = 0;    // parabola: 0=arriba,1=abajo,2=derecha,3=izquierda
                       // hiperbola: 0=horizontal, 1=vertical
    int alcance = 25;  // unidades a trazar (curvas abiertas)
    int color = 0;
    int width = 1;
    int fillMode = 0;  // 0 = contorno, 1 = lineas, 2 = solido
    bool visible = true;
};

struct DdaLine {
    float xa = 0.0f;
    float ya = 0.0f;
    float xb = 1.0f;
    float yb = 1.0f;
    int color = 0;
    int width = 1;
    bool visible = true;
};

struct PolyFill {
    int kind = 0;       // 0 = triangulo, 1 = rombo
    int n = 3;          // numero de vertices (3 o 4)
    float x[4] = {0.0f, 1.0f, 2.0f, 0.0f};
    float y[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    int pivot = 0;      // vertice fijo para el relleno por lineas
    int fillMode = 1;   // 0 = contorno, 1 = lineas, 2 = solido
    int color = 0;
    int width = 1;
    bool visible = true;
};

struct SegRes {
    std::string label;
    double dx = 0.0;
    double dy = 0.0;
    double m = 0.0;
    double thetaDeg = 0.0;
    double dist = 0.0;
    std::string tipo;
    bool degenerate = false;
    bool vertical = false;
    int connIdx = -1;  // indice de la conexion
};

struct GuiData {
    std::vector<FunctionRow> funcs;
    std::vector<PointRow> pts;
    std::vector<SegRes> segs;
    std::vector<SegConnection> conns;
    std::vector<ConicRow> conics;
    std::vector<DdaLine> ddas;
    std::vector<PolyFill> polys;

    char addBuf[160] = {0};
    std::string addErr;

    float ra = -10.0f;
    float rb = 10.0f;
    int rn = 2000;
    bool autoFit = true;
    bool wantResample = false;

    int viewMode = 0;
    float vx0 = 0.0f;
    float vx1 = 10000.0f;
    float vy0 = 0.0f;
    float vy1 = 10000.0f;
    bool wantApplyRange = false;

    int polyColor = 1;
    int polyStyle = SS_LINE;
    int polyWidth = 2;
    bool wantRecalc = false;

    bool showTicks = true;
    bool showPanel = true;
    bool wantFitAll = false;
    bool unicode = true;

    // Modos de conexion
    bool connectAll = true;       // conectar todos secuencialmente
    bool showSegResults = true;   // mostrar resultados detallados
    bool showGrid = true;
    bool showAxes = true;
    bool showLabels = true;
    bool darkCanvas = false;

    // Toolbar
    int activeTool = 0;  // 0=mover, 1=punto, 2=linea

    // Punto rapido
    float quickX = 0.0f;
    float quickY = 0.0f;

    // Archivo (exportar / importar proyecto)
    char filePath[260] = "proyecto.graf";
    char fileMsg[220] = {0};
};

extern GuiData g_gui;

const unsigned char* guiPalette(int idx);
int guiPaletteCount();
void guiPointLabel(int idx, char* out, std::size_t sz);
void guiRecomputeSegs();

bool guiInit(void* window);
void guiBeginFrame();
void guiBuild();
void guiEndFrame();
bool guiWantsMouse();
bool guiWantsKeyboard();
