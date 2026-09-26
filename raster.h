#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ElPt {
    int x = 0;
    int y = 0;
};

struct MidStep {
    int k = 0;
    long p = 0;
    int x = 0;
    int y = 0;
    int region = 1;
};

struct MidResult {
    int rx = 0;
    int ry = 0;
    std::vector<ElPt> cuarto;
    std::vector<MidStep> pasos;
};

MidResult midpointEllipse(int rx, int ry);
MidResult midpointCircle(int r);
std::vector<ElPt> conicBoundary(const MidResult& m, bool circle);

// Curvas uniformes abiertas (punto medio)
// Parabola canonica x^2 = 4*p*y (abre hacia arriba), primer cuadrante.
MidResult midpointParabola(int p, int alcance);
// Hiperbola canonica x^2/a^2 - y^2/b^2 = 1, rama derecha, mitad superior.
MidResult midpointHyperbola(int a, int b, int alcance);
// Polilinea completa a partir de los puntos del algoritmo.
// orient parabola: 0=arriba, 1=abajo, 2=derecha, 3=izquierda.
std::vector<ElPt> parabolaArc(const MidResult& m, int orient);
// orient hiperbola: 0=horizontal, 1=vertical. right = rama derecha/positiva.
std::vector<ElPt> hyperbolaBranch(const MidResult& m, int orient, bool right);

// =============================================================
//  Primitivas de salida: linea por el metodo DDA
// =============================================================
struct DdaStep {
    int k = 0;
    double x = 0.0;  // coordenada calculada (como en los apuntes)
    double y = 0.0;
    int px = 0;      // pixel redondeado
    int py = 0;
};

struct DdaResult {
    int caso = 0;      // 1..8 (subcasos), 9 M=+1, 10 M=0, 11 M=Error, 12 M=-1
    double m = 0.0;
    std::string tipo;  // "+M>1", "-M<1", "M=0", "M=Error", ...
    std::string dir;   // "Izq->Der  Aba->Arr"
    std::vector<DdaStep> pasos;
};

DdaResult ddaLine(double xa, double ya, double xb, double yb);

struct Canvas {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> px;

    void resize(int width, int height);
    void clear(uint8_t r = 255, uint8_t g = 255, uint8_t b = 255, uint8_t a = 255);
    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    void line(double x0, double y0, double x1, double y1,
              uint8_t r, uint8_t g, uint8_t b);
    void fillEllipse(int cx, int cy, int rx, int ry,
                     uint8_t r, uint8_t g, uint8_t b);
    void drawEllipse(int cx, int cy, int rx, int ry,
                     uint8_t r, uint8_t g, uint8_t b, int thickness = 2);
    void fillTriangle(double ax, double ay, double bx, double by, double cx, double cy,
                      uint8_t r, uint8_t g, uint8_t b);
};

