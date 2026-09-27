#include "gui.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"
#include "parser.h"
#include "project.h"
#include "raster.h"

GuiData g_gui;

namespace {

// Paleta de colores estilo GeoGebra
const unsigned char PAL[][3] = {
    {21, 101, 192},   // azul GeoGebra
    {211, 47, 47},    // rojo
    {56, 142, 60},    // verde
    {245, 124, 0},    // naranja
    {142, 36, 170},   // morado
    {0, 151, 167},    // teal
    {121, 85, 72},    // cafe
    {194, 24, 91},    // rosa
    {69, 90, 100},    // gris azulado
    {175, 180, 43},   // lima
};
const int PAL_N = 10;

bool fileExists(const char* path) {
    std::ifstream f(path);
    return f.good();
}

ImVec4 colorVec(int idx) {
    idx = idx % PAL_N;
    if (idx < 0) idx = 0;
    return ImVec4(PAL[idx][0] / 255.0f, PAL[idx][1] / 255.0f, PAL[idx][2] / 255.0f, 1.0f);
}

bool colorPicker(int idTag, int* cur) {
    bool changed = false;
    ImGui::PushID(idTag);
    ImVec4 curCol = colorVec(*cur);
    if (ImGui::ColorButton("##pal", curCol,
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                           ImVec2(18.0f, 18.0f))) {
        ImGui::OpenPopup("el");
    }
    if (ImGui::BeginPopup("el")) {
        for (int i = 0; i < PAL_N; ++i) {
            if (i > 0 && i % 5 != 0) ImGui::SameLine();
            ImGui::PushID(i);
            if (ImGui::ColorButton("##c", colorVec(i),
                                   ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(22.0f, 22.0f))) {
                *cur = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool primaryButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f)) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    return r;
}

void recomputeSegs() {
    g_gui.segs.clear();
    const std::size_t n = g_gui.pts.size();

    if (g_gui.connectAll) {
        // Modo secuencial: A->B, B->C, C->D ...
        for (std::size_t i = 0; i + 1 < n; ++i) {
            SegRes r;
            char lab[16], lab2[16];
            guiPointLabel(static_cast<int>(i), lab, sizeof(lab));
            guiPointLabel(static_cast<int>(i + 1), lab2, sizeof(lab2));
            r.label = std::string(lab) + " \xE2\x86\x92 " + lab2;
            double ax = g_gui.pts[i].x;
            double ay = g_gui.pts[i].y;
            double bx = g_gui.pts[i + 1].x;
            double by = g_gui.pts[i + 1].y;
            r.dx = bx - ax;
            r.dy = by - ay;
            r.dist = std::hypot(r.dx, r.dy);
            if (r.dist < 1e-9) {
                r.degenerate = true;
                r.m = 0.0;
                r.thetaDeg = 0.0;
                r.tipo = "Puntos coincidentes";
            } else {
                r.m = r.dy / r.dx;
                r.thetaDeg = std::atan2(r.dy, r.dx) * 180.0 / 3.14159265358979323846;
                if (std::fabs(r.dx) < 1e-9) {
                    r.vertical = true;
                    r.tipo = "Recta vertical";
                } else if (std::fabs(r.dy) < 1e-9) {
                    r.tipo = "Recta horizontal";
                } else if (r.m > 0.0) {
                    r.tipo = "Recta creciente";
                } else {
                    r.tipo = "Recta decreciente";
                }
            }
            r.connIdx = static_cast<int>(i);
            g_gui.segs.push_back(r);
        }
    }

    // Conexiones personalizadas
    for (std::size_t ci = 0; ci < g_gui.conns.size(); ++ci) {
        const SegConnection& c = g_gui.conns[ci];
        if (!c.enabled) continue;
        if (c.fromIdx < 0 || c.fromIdx >= static_cast<int>(n)) continue;
        if (c.toIdx < 0 || c.toIdx >= static_cast<int>(n)) continue;
        if (c.fromIdx == c.toIdx) continue;

        // Evitar duplicados con las conexiones secuenciales
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

        SegRes r;
        char lab[16], lab2[16];
        guiPointLabel(c.fromIdx, lab, sizeof(lab));
        guiPointLabel(c.toIdx, lab2, sizeof(lab2));
        r.label = std::string(lab) + " \xE2\x86\x92 " + lab2;
        double ax = g_gui.pts[c.fromIdx].x;
        double ay = g_gui.pts[c.fromIdx].y;
        double bx = g_gui.pts[c.toIdx].x;
        double by = g_gui.pts[c.toIdx].y;
        r.dx = bx - ax;
        r.dy = by - ay;
        r.dist = std::hypot(r.dx, r.dy);
        if (r.dist < 1e-9) {
            r.degenerate = true;
            r.m = 0.0;
            r.thetaDeg = 0.0;
            r.tipo = "Puntos coincidentes";
        } else {
            r.m = r.dy / r.dx;
            r.thetaDeg = std::atan2(r.dy, r.dx) * 180.0 / 3.14159265358979323846;
            if (std::fabs(r.dx) < 1e-9) {
                r.vertical = true;
                r.tipo = "Recta vertical";
            } else if (std::fabs(r.dy) < 1e-9) {
                r.tipo = "Recta horizontal";
            } else if (r.m > 0.0) {
                r.tipo = "Recta creciente";
            } else {
                r.tipo = "Recta decreciente";
            }
        }
        r.connIdx = static_cast<int>(ci) + 10000;
        g_gui.segs.push_back(r);
    }
}

const char* kStyleNames[] = {"Linea", "Linea punteada", "Puntos", "Linea + puntos"};
const int kStyleNamesN = 4;

// =============================================================
//  Estilo GeoGebra
// =============================================================
void applyGeoGebraStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0.0f;
    st.FrameRounding = 4.0f;
    st.GrabRounding = 3.0f;
    st.PopupRounding = 4.0f;
    st.ScrollbarRounding = 6.0f;
    st.TabRounding = 4.0f;
    st.ChildRounding = 0.0f;
    st.FramePadding = ImVec2(8.0f, 5.0f);
    st.ItemSpacing = ImVec2(6.0f, 5.0f);
    st.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    st.WindowPadding = ImVec2(10.0f, 10.0f);
    st.ScrollbarSize = 12.0f;
    st.GrabMinSize = 10.0f;
    st.WindowBorderSize = 0.0f;
    st.FrameBorderSize = 1.0f;
    st.SeparatorTextBorderSize = 2.0f;

    ImVec4* c = st.Colors;

    // Fondo del panel
    c[ImGuiCol_WindowBg]             = ImVec4(0.98f, 0.98f, 0.98f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);

    // Header / titulo
    c[ImGuiCol_TitleBg]              = ImVec4(0.30f, 0.52f, 0.90f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.25f, 0.47f, 0.85f, 1.00f);

    // Tabs estilo GeoGebra (claras, con subrayado azul al seleccionar)
    c[ImGuiCol_Tab]                  = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.78f, 0.85f, 0.95f, 1.00f);
    c[ImGuiCol_TabSelected]          = ImVec4(0.82f, 0.88f, 0.97f, 1.00f);
    c[ImGuiCol_TabSelectedOverline]  = ImVec4(0.25f, 0.47f, 0.85f, 1.00f);
    c[ImGuiCol_TabDimmed]            = ImVec4(0.94f, 0.95f, 0.97f, 1.00f);
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.86f, 0.90f, 0.96f, 1.00f);

    // Botones
    c[ImGuiCol_Button]               = ImVec4(0.30f, 0.52f, 0.90f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.35f, 0.57f, 0.95f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.20f, 0.42f, 0.80f, 1.00f);

    // Frames (inputs)
    c[ImGuiCol_FrameBg]              = ImVec4(0.94f, 0.95f, 0.97f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.88f, 0.91f, 0.96f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.82f, 0.87f, 0.95f, 1.00f);
    c[ImGuiCol_Border]               = ImVec4(0.78f, 0.82f, 0.88f, 1.00f);

    // Checkmarks y sliders
    c[ImGuiCol_CheckMark]            = ImVec4(0.25f, 0.47f, 0.85f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.30f, 0.52f, 0.90f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.20f, 0.42f, 0.80f, 1.00f);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.94f, 0.95f, 0.97f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.72f, 0.76f, 0.82f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.55f, 0.60f, 0.70f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.40f, 0.45f, 0.55f, 1.00f);

    // Header (colapsables)
    c[ImGuiCol_Header]               = ImVec4(0.88f, 0.91f, 0.96f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.78f, 0.84f, 0.93f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.68f, 0.77f, 0.90f, 1.00f);

    // Separadores
    c[ImGuiCol_Separator]            = ImVec4(0.82f, 0.85f, 0.90f, 1.00f);
    c[ImGuiCol_SeparatorHovered]     = ImVec4(0.30f, 0.52f, 0.90f, 0.80f);
    c[ImGuiCol_SeparatorActive]      = ImVec4(0.25f, 0.47f, 0.85f, 1.00f);

    // Texto
    c[ImGuiCol_Text]                 = ImVec4(0.13f, 0.15f, 0.20f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.53f, 0.58f, 1.00f);

    // Resize grip
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.30f, 0.52f, 0.90f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]    = ImVec4(0.30f, 0.52f, 0.90f, 0.60f);
    c[ImGuiCol_ResizeGripActive]     = ImVec4(0.30f, 0.52f, 0.90f, 0.95f);
}

// =============================================================
//  Toolbar superior
// =============================================================
void drawToolbar() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    float panelW = g_gui.showPanel ? 420.0f : 0.0f;
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - panelW, 42.0f), ImGuiCond_Always);

    ImGuiWindowFlags tbFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.96f, 0.97f, 0.98f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.82f, 0.85f, 0.90f, 1.0f));

    if (ImGui::Begin("##toolbar", nullptr, tbFlags)) {
        auto toolBtn = [](const char* label, const char* tooltip, int toolId) -> bool {
            bool active = (g_gui.activeTool == toolId);
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.47f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.93f, 0.95f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.22f, 0.28f, 1.0f));
            }
            bool clicked = ImGui::Button(label, ImVec2(0.0f, 28.0f));
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(tooltip);
                ImGui::EndTooltip();
            }
            return clicked;
        };

        if (toolBtn("Mover", "Mover (arrastrar el plano)", 0)) g_gui.activeTool = 0;
        ImGui::SameLine();

        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Controles de vista
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.93f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.22f, 0.28f, 1.0f));

        if (ImGui::Button("Ajustar", ImVec2(60.0f, 28.0f))) {
            g_gui.wantFitAll = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Ajustar vista (R)");
            ImGui::EndTooltip();
        }
        ImGui::SameLine();

        ImGui::TextDisabled("|");
        ImGui::SameLine();

        if (ImGui::Button("Todo", ImVec2(52.0f, 28.0f))) {
            g_gui.viewMode = 0;
            g_gui.wantFitAll = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Mostrar todo el plano (auto)");
            ImGui::EndTooltip();
        }
        ImGui::SameLine();

        if (ImGui::Button("0-10k", ImVec2(58.0f, 28.0f))) {
            g_gui.viewMode = 1;
            g_gui.vx0 = 0.0f;
            g_gui.vy0 = 0.0f;
            g_gui.vx1 = 10000.0f;
            g_gui.vy1 = 10000.0f;
            g_gui.wantApplyRange = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Ver x e y de 0 a 10 000");
            ImGui::EndTooltip();
        }
        ImGui::SameLine();

        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // Toggles de vista
        bool gridChanged = ImGui::Checkbox("Cuadricula", &g_gui.showGrid);
        (void)gridChanged;
        ImGui::SameLine();
        ImGui::Checkbox("Ejes", &g_gui.showAxes);
        ImGui::SameLine();
        ImGui::Checkbox("Etiquetas", &g_gui.showLabels);

        ImGui::PopStyleColor(2);

        ImGui::SameLine(ImGui::GetWindowWidth() - 100.0f);
        if (primaryButton(g_gui.showPanel ? "Panel <<" : "Panel >>", ImVec2(90.0f, 28.0f))) {
            g_gui.showPanel = !g_gui.showPanel;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}

// =============================================================
//  Tab: Funciones
// =============================================================
[[maybe_unused]] void funcsTab() {
    float availH = ImGui::GetContentRegionAvail().y;
    if (availH < 10.0f) return;
    if (!ImGui::BeginChild("##sfunc", ImVec2(0.0f, availH), ImGuiChildFlags_None)) {
        ImGui::EndChild();
        return;
    }
    float fullW = ImGui::GetContentRegionAvail().x;
    if (fullW < 1.0f) fullW = 380.0f;

    // --- Entrada de funcion ---
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetNextItemWidth(-1.0f);
    bool enter = ImGui::InputTextWithHint(
        "##add", "Ingresa una funcion, ej: sin(x), x^2-3x+1",
        g_gui.addBuf, sizeof(g_gui.addBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor();

    if (enter) {
        std::string t = g_gui.addBuf;
        std::size_t a = t.find_first_not_of(" \t\r\n");
        std::size_t b = t.find_last_not_of(" \t\r\n");
        t = (a == std::string::npos) ? std::string() : t.substr(a, b - a + 1);
        if (!t.empty()) {
            Expr chk;
            if (Expr::parse(t, chk)) {
                g_gui.funcs.push_back(FunctionRow(t.c_str()));
                g_gui.funcs.back().color = static_cast<int>(g_gui.funcs.size() - 1) % guiPaletteCount();
                g_gui.addBuf[0] = '\0';
                g_gui.wantResample = true;
                if (g_gui.autoFit && g_gui.viewMode == 0) g_gui.wantFitAll = true;
                g_gui.addErr.clear();
            } else {
                g_gui.addErr = chk.err;
            }
        }
    }
    if (!g_gui.addErr.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
        ImGui::TextWrapped("! %s", g_gui.addErr.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();

    // --- Rango (colapsable) ---
    if (ImGui::CollapsingHeader("Rango y muestras", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8.0f);
        ImGui::SetNextItemWidth(fullW * 0.34f);
        ImGui::InputFloat("x min", &g_gui.ra, 0.0f, 0.0f, "%.4g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.34f);
        ImGui::InputFloat("x max", &g_gui.rb, 0.0f, 0.0f, "%.4g");
        ImGui::SetNextItemWidth(fullW * 0.34f);
        ImGui::DragInt("N", &g_gui.rn, 1.0f, 2, 2000000, "%d");
        ImGui::SameLine();
        ImGui::Checkbox("Auto-encuadre", &g_gui.autoFit);
        ImGui::Unindent(8.0f);
    }

    // --- Vista del plano ---
    if (ImGui::CollapsingHeader("Vista del plano", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8.0f);
        int prevMode = g_gui.viewMode;
        ImGui::RadioButton("Mostrar todo", &g_gui.viewMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Rango fijo", &g_gui.viewMode, 1);
        if (g_gui.viewMode != prevMode) {
            if (g_gui.viewMode == 0) {
                g_gui.wantFitAll = true;
            } else {
                g_gui.wantApplyRange = true;
            }
        }
        ImGui::SetNextItemWidth(fullW * 0.34f);
        ImGui::InputFloat("x min", &g_gui.vx0, 0.0f, 0.0f, "%.6g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.34f);
        ImGui::InputFloat("x max", &g_gui.vx1, 0.0f, 0.0f, "%.6g");
        ImGui::SetNextItemWidth(fullW * 0.34f);
        ImGui::InputFloat("y min", &g_gui.vy0, 0.0f, 0.0f, "%.6g");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.34f);
        ImGui::InputFloat("y max", &g_gui.vy1, 0.0f, 0.0f, "%.6g");
        if (primaryButton("Aplicar rango", ImVec2(-1.0f, 0.0f))) {
            g_gui.viewMode = 1;
            g_gui.wantApplyRange = true;
        }
        if (primaryButton("Rango 0 a 10 000 (x e y)", ImVec2(-1.0f, 0.0f))) {
            g_gui.vx0 = 0.0f;
            g_gui.vy0 = 0.0f;
            g_gui.vx1 = 10000.0f;
            g_gui.vy1 = 10000.0f;
            g_gui.viewMode = 1;
            g_gui.wantApplyRange = true;
        }
        ImGui::Unindent(8.0f);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Lista de funciones ---
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "FUNCIONES (%d)",
                       static_cast<int>(g_gui.funcs.size()));
    ImGui::Spacing();

    if (g_gui.funcs.empty()) {
        ImGui::TextDisabled("Escribe una funcion arriba y presiona Enter.");
    }

    for (int i = 0; i < static_cast<int>(g_gui.funcs.size()); ++i) {
        FunctionRow& row = g_gui.funcs[i];
        ImGui::PushID(i);

        // Fondo de tarjeta
        ImVec2 p = ImGui::GetCursorScreenPos();
        float cardH = 72.0f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            p, ImVec2(p.x + fullW, p.y + cardH),
            IM_COL32(245, 247, 250, 255), 6.0f);
        ImGui::GetWindowDrawList()->AddRect(
            p, ImVec2(p.x + fullW, p.y + cardH),
            IM_COL32(210, 215, 225, 255), 6.0f);

        // Barra de color izquierda
        const unsigned char* pc = guiPalette(row.color);
        ImGui::GetWindowDrawList()->AddRectFilled(
            p, ImVec2(p.x + 4.0f, p.y + cardH),
            IM_COL32(pc[0], pc[1], pc[2], 255), 6.0f, ImDrawFlags_RoundCornersLeft);

        ImGui::Indent(10.0f);
        ImGui::Spacing();

        // Fila 1: color + nombre + visibilidad + quitar
        colorPicker(0, &row.color);
        ImGui::SameLine();
        char nm[16];
        std::snprintf(nm, sizeof(nm), "f%d(x) =", i + 1);
        ImGui::TextColored(colorVec(row.color), "%s", nm);
        ImGui::SameLine();
        ImGui::Checkbox("##vis", &row.visible);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(row.visible ? "Visible" : "Oculta");
            ImGui::EndTooltip();
        }
        ImGui::SameLine(fullW - 36.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("\xC3\x97", ImVec2(24.0f, 22.0f))) {
            g_gui.funcs.erase(g_gui.funcs.begin() + i);
            g_gui.wantResample = true;
            --i;
            ImGui::PopStyleColor(2);
            ImGui::Unindent(10.0f);
            ImGui::PopID();
            continue;
        }
        ImGui::PopStyleColor(2);

        // Fila 2: expresion + estilo
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::SetNextItemWidth(fullW * 0.58f - 14.0f);
        if (ImGui::InputText("##expr", row.expr, sizeof(row.expr))) {
            g_gui.wantResample = true;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.22f);
        ImGui::Combo("##stl", &row.style, kStyleNames, kStyleNamesN);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.12f);
        ImGui::SliderInt("##w", &row.width, 1, 4);

        if (!row.good && !row.err.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
            ImGui::TextWrapped("! %s", row.err.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Unindent(10.0f);
        ImGui::Spacing();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));  // separacion entre tarjetas
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// =============================================================
//  Tab: Puntos y Conexiones
// =============================================================
SegRes segBetween(const PointRow& a, const PointRow& b, const std::string& label) {
    SegRes r;
    r.label = label;
    r.dx = static_cast<double>(b.x) - a.x;
    r.dy = static_cast<double>(b.y) - a.y;
    r.dist = std::hypot(r.dx, r.dy);
    if (r.dist < 1e-9) {
        r.degenerate = true;
        r.m = 0.0;
        r.thetaDeg = 0.0;
        r.tipo = "Puntos coincidentes";
    } else {
        r.m = r.dy / r.dx;
        r.thetaDeg = std::atan2(r.dy, r.dx) * 180.0 / 3.14159265358979323846;
        if (std::fabs(r.dx) < 1e-9) {
            r.vertical = true;
            r.tipo = "Recta vertical";
        } else if (std::fabs(r.dy) < 1e-9) {
            r.tipo = "Recta horizontal";
        } else if (r.m > 0.0) {
            r.tipo = "Recta creciente";
        } else {
            r.tipo = "Recta decreciente";
        }
    }
    return r;
}

void pointsTab() {
    float availH = ImGui::GetContentRegionAvail().y;
    if (availH < 10.0f) return;
    if (!ImGui::BeginChild("##spts", ImVec2(0.0f, availH), ImGuiChildFlags_None)) {
        ImGui::EndChild();
        return;
    }
    float fullW = ImGui::GetContentRegionAvail().x;
    if (fullW < 1.0f) fullW = 380.0f;

    // --- Agregar punto rapido ---
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetNextItemWidth(fullW * 0.36f);
    ImGui::InputFloat("##qx", &g_gui.quickX, 0.0f, 0.0f, "%.3f");
    ImGui::SameLine();
    ImGui::TextUnformatted(",");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fullW * 0.36f);
    ImGui::InputFloat("##qy", &g_gui.quickY, 0.0f, 0.0f, "%.3f");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("+", ImVec2(28.0f, 0.0f))) {
        PointRow p;
        p.x = g_gui.quickX;
        p.y = g_gui.quickY;
        int idx = static_cast<int>(g_gui.pts.size());
        guiPointLabel(idx, p.label, sizeof(p.label));
        g_gui.pts.push_back(p);
        recomputeSegs();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Agregar punto con estas coordenadas");
        ImGui::EndTooltip();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Lista de puntos ---
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "PUNTOS (%d)",
                       static_cast<int>(g_gui.pts.size()));
    ImGui::Spacing();

    bool changed = false;

    if (g_gui.pts.empty()) {
        ImGui::TextDisabled("Usa el campo de arriba para agregar puntos.");
    }

    for (int i = 0; i < static_cast<int>(g_gui.pts.size()); ++i) {
        ImGui::PushID(i);

        // Tarjeta de punto
        ImVec2 cp = ImGui::GetCursorScreenPos();
        float cardH = 28.0f;
        const unsigned char* ptcol = guiPalette(g_gui.polyColor);
        ImGui::GetWindowDrawList()->AddRectFilled(
            cp, ImVec2(cp.x + fullW, cp.y + cardH),
            IM_COL32(245, 247, 250, 255), 4.0f);

        char lab[16];
        guiPointLabel(i, lab, sizeof(lab));

        // Circulo de color
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(cp.x + 12.0f, cp.y + cardH / 2.0f), 5.0f,
            IM_COL32(ptcol[0], ptcol[1], ptcol[2], 255));

        ImGui::Indent(24.0f);
        ImGui::TextColored(ImVec4(0.15f, 0.15f, 0.2f, 1.0f), "%s", lab);
        ImGui::SameLine();
        ImGui::TextDisabled("(");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetNextItemWidth(fullW * 0.28f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        changed |= ImGui::InputFloat("##px", &g_gui.pts[i].x, 0.0f, 0.0f, "%.3f");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(",");
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::SetNextItemWidth(fullW * 0.28f);
        changed |= ImGui::InputFloat("##py", &g_gui.pts[i].y, 0.0f, 0.0f, "%.3f");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(")");
        ImGui::SameLine(fullW - 30.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("\xC3\x97##del", ImVec2(22.0f, 22.0f))) {
            g_gui.pts.erase(g_gui.pts.begin() + i);
            changed = true;
            --i;
            ImGui::PopStyleColor(2);
            ImGui::Unindent(24.0f);
            ImGui::PopID();
            continue;
        }
        ImGui::PopStyleColor(2);
        ImGui::Unindent(24.0f);
        ImGui::PopID();
    }

    ImGui::Spacing();

    // Botones de accion
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.52f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("Agregar punto", ImVec2(fullW * 0.48f, 0.0f))) {
        PointRow p;
        if (g_gui.pts.size() >= 2) {
            const PointRow& last = g_gui.pts.back();
            const PointRow& prev = g_gui.pts[g_gui.pts.size() - 2];
            p.x = last.x + (last.x - prev.x);
            p.y = last.y + (last.y - prev.y);
        } else if (g_gui.pts.size() == 1) {
            p.x = g_gui.pts[0].x + 1.0f;
            p.y = g_gui.pts[0].y;
        } else {
            p.x = 1.0f;
            p.y = 0.0f;
        }
        int idx = static_cast<int>(g_gui.pts.size());
        guiPointLabel(idx, p.label, sizeof(p.label));
        g_gui.pts.push_back(p);
        changed = true;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.22f, 0.22f, 1.0f));
    if (ImGui::Button("Limpiar todo", ImVec2(-1.0f, 0.0f))) {
        g_gui.pts.clear();
        g_gui.segs.clear();
        g_gui.conns.clear();
        changed = false;
    }
    ImGui::PopStyleColor(3);

    if (primaryButton("Ajustar vista", ImVec2(-1.0f, 0.0f))) {
        g_gui.wantFitAll = true;
    }

    if (changed) {
        recomputeSegs();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ==========================================================
    //  Seccion: Tabla de datos de los puntos
    // ==========================================================
    if (ImGui::CollapsingHeader("Tabla de datos de los puntos",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (g_gui.pts.empty()) {
            ImGui::TextDisabled("  No hay puntos todavia.");
        } else {
            if (ImGui::BeginTable("##tablapuntos", 3,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Punto");
                ImGui::TableSetupColumn("x");
                ImGui::TableSetupColumn("y");
                ImGui::TableHeadersRow();
                for (int i = 0; i < static_cast<int>(g_gui.pts.size()); ++i) {
                    char lab[16];
                    guiPointLabel(i, lab, sizeof(lab));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(lab);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%.6g", g_gui.pts[i].x);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%.6g", g_gui.pts[i].y);
                }
                ImGui::EndTable();
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ==========================================================
    //  Seccion: Conexiones (A->B, B->C, etc.)
    // ==========================================================
    if (ImGui::CollapsingHeader("Conexiones punto a punto", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8.0f);

        // Config de linea
        colorPicker(100, &g_gui.polyColor);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.36f);
        ImGui::Combo("##lnst", &g_gui.polyStyle, kStyleNames, kStyleNamesN);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.20f);
        ImGui::SliderInt("##lnw", &g_gui.polyWidth, 1, 4);

        ImGui::Spacing();

        // Toggle secuencial
        ImGui::Checkbox("Conectar secuencialmente (A\xE2\x86\x92" "B\xE2\x86\x92" "C...)", &g_gui.connectAll);

        // Mostrar conexiones secuenciales visualmente
        if (g_gui.connectAll && g_gui.pts.size() >= 2) {
            ImGui::Indent(8.0f);
            for (std::size_t i = 0; i + 1 < g_gui.pts.size(); ++i) {
                char la[16], lb[16];
                guiPointLabel(static_cast<int>(i), la, sizeof(la));
                guiPointLabel(static_cast<int>(i + 1), lb, sizeof(lb));

                const unsigned char* lc = guiPalette(g_gui.polyColor);
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(lc[0] / 255.0f, lc[1] / 255.0f, lc[2] / 255.0f, 1.0f));
                ImGui::Text("\xE2\x94\x9C\xE2\x94\x80 %s \xE2\x86\x92 %s", la, lb);
                ImGui::PopStyleColor();
            }
            ImGui::Unindent(8.0f);
        }

        ImGui::Spacing();

        // Conexiones personalizadas
        ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "Conexiones adicionales:");
        for (int ci = 0; ci < static_cast<int>(g_gui.conns.size()); ++ci) {
            SegConnection& c = g_gui.conns[ci];
            ImGui::PushID(1000 + ci);
            ImGui::Checkbox("##en", &c.enabled);
            ImGui::SameLine();
            int maxPt = static_cast<int>(g_gui.pts.size()) - 1;
            if (maxPt < 0) maxPt = 0;

            // Combo "de"
            char fromLab[16];
            if (c.fromIdx >= 0 && c.fromIdx < static_cast<int>(g_gui.pts.size()))
                guiPointLabel(c.fromIdx, fromLab, sizeof(fromLab));
            else
                std::snprintf(fromLab, sizeof(fromLab), "?");

            ImGui::SetNextItemWidth(50.0f);
            if (ImGui::BeginCombo("##from", fromLab)) {
                for (int pi = 0; pi < static_cast<int>(g_gui.pts.size()); ++pi) {
                    char pl[16];
                    guiPointLabel(pi, pl, sizeof(pl));
                    if (ImGui::Selectable(pl, c.fromIdx == pi)) {
                        c.fromIdx = pi;
                        recomputeSegs();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::TextUnformatted("\xE2\x86\x92");
            ImGui::SameLine();

            // Combo "a"
            char toLab[16];
            if (c.toIdx >= 0 && c.toIdx < static_cast<int>(g_gui.pts.size()))
                guiPointLabel(c.toIdx, toLab, sizeof(toLab));
            else
                std::snprintf(toLab, sizeof(toLab), "?");

            ImGui::SetNextItemWidth(50.0f);
            if (ImGui::BeginCombo("##to", toLab)) {
                for (int pi = 0; pi < static_cast<int>(g_gui.pts.size()); ++pi) {
                    char pl[16];
                    guiPointLabel(pi, pl, sizeof(pl));
                    if (ImGui::Selectable(pl, c.toIdx == pi)) {
                        c.toIdx = pi;
                        recomputeSegs();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("\xC3\x97##dc", ImVec2(22.0f, 22.0f))) {
                g_gui.conns.erase(g_gui.conns.begin() + ci);
                recomputeSegs();
                --ci;
                ImGui::PopStyleColor(2);
                ImGui::PopID();
                continue;
            }
            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }

        if (g_gui.pts.size() >= 2) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.88f, 0.91f, 0.96f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.25f, 0.47f, 0.85f, 1.0f));
            if (ImGui::Button("+ Agregar conexion", ImVec2(-1.0f, 0.0f))) {
                SegConnection sc;
                sc.fromIdx = 0;
                sc.toIdx = static_cast<int>(g_gui.pts.size()) - 1;
                g_gui.conns.push_back(sc);
                recomputeSegs();
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::Unindent(8.0f);
    }

    // ==========================================================
    //  Seccion: Resumen general del punto A al ultimo
    // ==========================================================
    if (ImGui::CollapsingHeader("Resumen total: punto A -> ultimo",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        if (g_gui.pts.size() < 2) {
            ImGui::TextDisabled("  Se necesitan al menos 2 puntos.");
        } else {
            char la[16], lb[16];
            guiPointLabel(0, la, sizeof(la));
            guiPointLabel(static_cast<int>(g_gui.pts.size()) - 1, lb, sizeof(lb));
            SegRes r = segBetween(g_gui.pts.front(), g_gui.pts.back(),
                                  std::string(la) + " \xE2\x86\x92 " + lb);

            ImVec2 sp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                sp, ImVec2(sp.x + fullW - 16.0f, sp.y + 20.0f),
                IM_COL32(236, 239, 245, 255), 3.0f);

            ImGui::Indent(6.0f);
            ImGui::TextColored(ImVec4(0.2f, 0.4f, 0.8f, 1.0f), "%s", r.label.c_str());
            ImGui::Unindent(6.0f);

            ImGui::Indent(12.0f);
            if (r.degenerate) {
                ImGui::TextColored(ImVec4(0.6f, 0.4f, 0.1f, 1.0f),
                                   "Distancia = 0 (puntos coincidentes)");
            } else {
                ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f),
                    "\xCE\x94x = %.4g   \xCE\x94y = %.4g", r.dx, r.dy);

                if (r.vertical) {
                    ImGui::Text("Pendiente m = %s",
                                g_gui.unicode ? "\xE2\x88\x9E" : "inf");
                } else {
                    ImGui::Text("Pendiente m = %.6g", r.m);
                }

                ImGui::Text("Angulo = %.2f%s", r.thetaDeg,
                            g_gui.unicode ? "\xC2\xB0" : " grados");
                ImGui::Text("Distancia = %.6g", r.dist);

                ImVec4 tipocol = ImVec4(0.3f, 0.3f, 0.35f, 1.0f);
                if (r.tipo == "Recta creciente") tipocol = ImVec4(0.1f, 0.5f, 0.2f, 1.0f);
                else if (r.tipo == "Recta decreciente") tipocol = ImVec4(0.7f, 0.2f, 0.2f, 1.0f);
                else if (r.tipo == "Recta horizontal") tipocol = ImVec4(0.2f, 0.4f, 0.8f, 1.0f);
                else if (r.tipo == "Recta vertical") tipocol = ImVec4(0.5f, 0.2f, 0.7f, 1.0f);
                ImGui::TextColored(tipocol, "%s", r.tipo.c_str());
            }
            ImGui::Unindent(12.0f);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ==========================================================
    //  Seccion: Resultados
    // ==========================================================
    if (ImGui::CollapsingHeader("Resultados de segmentos", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (g_gui.pts.size() < 2) {
            ImGui::TextDisabled("  Se necesitan al menos 2 puntos.");
        } else if (g_gui.segs.empty()) {
            recomputeSegs();
        }

        for (const SegRes& r : g_gui.segs) {
            ImGui::PushID(r.label.c_str());

            // Encabezado del segmento
            ImVec2 sp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                sp, ImVec2(sp.x + fullW - 16.0f, sp.y + 20.0f),
                IM_COL32(236, 239, 245, 255), 3.0f);

            ImGui::Indent(6.0f);
            ImGui::TextColored(ImVec4(0.2f, 0.4f, 0.8f, 1.0f), "%s", r.label.c_str());
            ImGui::Unindent(6.0f);

            ImGui::Indent(12.0f);
            if (r.degenerate) {
                ImGui::TextColored(ImVec4(0.6f, 0.4f, 0.1f, 1.0f),
                                   "Distancia = 0 (puntos coincidentes)");
            } else {
                ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f),
                    "\xCE\x94x = %.4g   \xCE\x94y = %.4g", r.dx, r.dy);

                if (r.vertical) {
                    ImGui::Text("Pendiente m = %s",
                                g_gui.unicode ? "\xE2\x88\x9E" : "inf");
                } else {
                    ImGui::Text("Pendiente m = %.6g", r.m);
                }

                ImGui::Text("Angulo = %.2f%s", r.thetaDeg,
                            g_gui.unicode ? "\xC2\xB0" : " grados");
                ImGui::Text("Distancia = %.6g", r.dist);

                // Tipo con icono
                ImVec4 tipocol = ImVec4(0.3f, 0.3f, 0.35f, 1.0f);
                if (r.tipo == "Recta creciente") tipocol = ImVec4(0.1f, 0.5f, 0.2f, 1.0f);
                else if (r.tipo == "Recta decreciente") tipocol = ImVec4(0.7f, 0.2f, 0.2f, 1.0f);
                else if (r.tipo == "Recta horizontal") tipocol = ImVec4(0.2f, 0.4f, 0.8f, 1.0f);
                else if (r.tipo == "Recta vertical") tipocol = ImVec4(0.5f, 0.2f, 0.7f, 1.0f);
                ImGui::TextColored(tipocol, "%s", r.tipo.c_str());
            }
            ImGui::Unindent(12.0f);
            ImGui::Spacing();
            ImGui::PopID();
        }
    }


    ImGui::EndChild();
}

MidResult conicMid(const ConicRow& c) {
    if (c.type == 1) return midpointCircle(c.r);
    if (c.type == 2) return midpointParabola(c.p, c.alcance);
    if (c.type == 3) return midpointHyperbola(c.a, c.b, c.alcance);
    return midpointEllipse(c.rx, c.ry);
}

const char* conicName(int t) {
    switch (t) {
        case 1: return "Circunferencia";
        case 2: return "Parabola";
        case 3: return "Hiperbola";
        default: return "Elipse";
    }
}

void drawConicInfo(const ConicRow& c) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.40f, 0.50f, 1.0f));
    if (c.type == 1) {
        ImGui::Text("(x-%d)^2 + (y-%d)^2 = %d^2", c.xc, c.yc, c.r);
    } else if (c.type == 0) {
        ImGui::Text("(x-%d)^2/%d^2 + (y-%d)^2/%d^2 = 1", c.xc, c.rx, c.yc, c.ry);
        double rx2 = static_cast<double>(c.rx) * c.rx;
        double ry2 = static_cast<double>(c.ry) * c.ry;
        double cc = std::sqrt(std::fabs(rx2 - ry2));
        if (c.rx >= c.ry) {
            ImGui::Text("Focos: (%.4g,%d) (%.4g,%d)", c.xc - cc, c.yc, c.xc + cc, c.yc);
        } else {
            ImGui::Text("Focos: (%d,%.4g) (%d,%.4g)", c.xc, c.yc - cc, c.xc, c.yc + cc);
        }
    } else if (c.type == 2) {
        const char* dirs[] = {"arriba", "abajo", "derecha", "izquierda"};
        int o = (c.orient < 0 || c.orient > 3) ? 0 : c.orient;
        ImGui::Text("p = %d  (abre hacia %s)", c.p, dirs[o]);
        if (o == 0 || o == 1) {
            int s = (o == 0) ? 1 : -1;
            ImGui::Text("(x-%d)^2 = %+d(y-%d)", c.xc, 4 * c.p * s, c.yc);
            ImGui::Text("Foco (%.4g,%.4g)  Directriz y = %.4g",
                        static_cast<double>(c.xc),
                        static_cast<double>(c.yc + s * c.p),
                        static_cast<double>(c.yc - s * c.p));
        } else {
            int s = (o == 2) ? 1 : -1;
            ImGui::Text("(y-%d)^2 = %+d(x-%d)", c.yc, 4 * c.p * s, c.xc);
            ImGui::Text("Foco (%.4g,%.4g)  Directriz x = %.4g",
                        static_cast<double>(c.xc + s * c.p),
                        static_cast<double>(c.yc),
                        static_cast<double>(c.xc - s * c.p));
        }
    } else {
        double cc = std::sqrt(static_cast<double>(c.a) * c.a +
                              static_cast<double>(c.b) * c.b);
        if (c.orient == 0) {
            ImGui::Text("(x-%d)^2/%d^2 - (y-%d)^2/%d^2 = 1  (horizontal)",
                        c.xc, c.a, c.yc, c.b);
            ImGui::Text("F1 (%.4g,%d)  F2 (%.4g,%d)  c = %.4g",
                        c.xc - cc, c.yc, c.xc + cc, c.yc, cc);
            ImGui::Text("Vertices (%.4g,%d) (%.4g,%d)",
                        static_cast<double>(c.xc - c.a), c.yc,
                        static_cast<double>(c.xc + c.a), c.yc);
            ImGui::Text("Asintotas: y-%d = +-%.4g(x-%d)",
                        c.yc, static_cast<double>(c.b) / c.a, c.xc);
        } else {
            ImGui::Text("(y-%d)^2/%d^2 - (x-%d)^2/%d^2 = 1  (vertical)",
                        c.yc, c.a, c.xc, c.b);
            ImGui::Text("F1 (%d,%.4g)  F2 (%d,%.4g)  c = %.4g",
                        c.xc, c.yc - cc, c.xc, c.yc + cc, cc);
            ImGui::Text("Vertices (%d,%.4g) (%d,%.4g)",
                        c.xc, static_cast<double>(c.yc - c.a),
                        c.xc, static_cast<double>(c.yc + c.a));
            ImGui::Text("Asintotas: y-%d = +-%.4g(x-%d)",
                        c.yc, static_cast<double>(c.a) / c.b, c.xc);
        }
    }
    ImGui::PopStyleColor();
}

void sendConicToArray(const ConicRow& c) {
    std::vector<std::vector<ElPt>> arcs;
    if (c.type == 0 || c.type == 1) {
        MidResult m = (c.type == 1) ? midpointCircle(c.r) : midpointEllipse(c.rx, c.ry);
        arcs.push_back(conicBoundary(m, c.type == 1));
    } else if (c.type == 2) {
        arcs.push_back(parabolaArc(midpointParabola(c.p, c.alcance), c.orient));
    } else {
        MidResult m = midpointHyperbola(c.a, c.b, c.alcance);
        arcs.push_back(hyperbolaBranch(m, c.orient, true));
        arcs.push_back(hyperbolaBranch(m, c.orient, false));
    }
    PointRow ctr;
    ctr.x = static_cast<float>(c.xc);
    ctr.y = static_cast<float>(c.yc);
    guiPointLabel(static_cast<int>(g_gui.pts.size()), ctr.label, sizeof(ctr.label));
    g_gui.pts.push_back(ctr);
    for (const std::vector<ElPt>& arc : arcs) {
        int total = static_cast<int>(arc.size());
        int stride = 1;
        if (total > 800) stride = (total + 799) / 800;
        for (int i = 0; i < total; i += stride) {
            PointRow p;
            p.x = static_cast<float>(c.xc + arc[i].x);
            p.y = static_cast<float>(c.yc + arc[i].y);
            guiPointLabel(static_cast<int>(g_gui.pts.size()), p.label, sizeof(p.label));
            g_gui.pts.push_back(p);
        }
    }
    recomputeSegs();
}

[[maybe_unused]] void conicsTab() {
    float availH = ImGui::GetContentRegionAvail().y;
    if (availH < 10.0f) return;
    if (!ImGui::BeginChild("##sconics", ImVec2(0.0f, availH), ImGuiChildFlags_None)) {
        ImGui::EndChild();
        return;
    }
    float fullW = ImGui::GetContentRegionAvail().x;
    if (fullW < 1.0f) fullW = 380.0f;

    static int pType = 0;
    static int pXc = 100, pYc = 150, pRx = 8, pRy = 6, pR = 10;
    static int pP = 5, pA = 5, pB = 3, pOrient = 0, pAlc = 25;
    static int pColor = 0, pWidth = 1, pFill = 0;

    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "NUEVA CONICA");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(fullW * 0.44f);
    ImGui::Combo("##ctype", &pType,
                 "Elipse\0Circunferencia\0Parabola\0Hiperbola\0");
    ImGui::SameLine();
    colorPicker(200, &pColor);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetNextItemWidth(fullW * 0.30f);
    ImGui::DragInt(pType == 2 ? "Vertice X" : "Xc", &pXc, 1.0f, 0, 0, "%d");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fullW * 0.30f);
    ImGui::DragInt(pType == 2 ? "Vertice Y" : "Yc", &pYc, 1.0f, 0, 0, "%d");

    if (pType == 0) {
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::DragInt("Rx", &pRx, 1.0f, 0, 0, "%d");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::DragInt("Ry", &pRy, 1.0f, 0, 0, "%d");
    } else if (pType == 1) {
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::DragInt("R", &pR, 1.0f, 0, 0, "%d");
    } else if (pType == 2) {
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::DragInt("P (foco)", &pP, 1.0f, 0, 0, "%d");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::Combo("##por", &pOrient, "Arriba\0Abajo\0Derecha\0Izquierda\0");
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::DragInt("Alcance", &pAlc, 1.0f, 1, 100000, "%d");
    } else {
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::DragInt("A", &pA, 1.0f, 0, 0, "%d");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::DragInt("B", &pB, 1.0f, 0, 0, "%d");
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::Combo("##hor", &pOrient, "Horizontal\0Vertical\0");
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::DragInt("Alcance", &pAlc, 1.0f, 1, 100000, "%d");
    }
    ImGui::PopStyleColor();

    ImGui::SetNextItemWidth(fullW * 0.48f);
    ImGui::Combo("##fill", &pFill, "Contorno\0Lineas (centro)\0Solido\0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fullW * 0.34f);
    ImGui::SliderInt("grosor", &pWidth, 1, 4);

    if (primaryButton("Agregar conica", ImVec2(-1.0f, 0.0f))) {
        ConicRow c;
        c.type = pType;
        c.xc = pXc;
        c.yc = pYc;
        c.rx = pRx;
        c.ry = pRy;
        c.r = pR;
        c.p = pP;
        c.a = pA;
        c.b = pB;
        c.orient = pOrient;
        c.alcance = pAlc;
        c.color = pColor;
        c.width = pWidth;
        c.fillMode = pFill;
        g_gui.conics.push_back(c);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "CONICAS (%d)",
                       static_cast<int>(g_gui.conics.size()));
    ImGui::Spacing();

    for (int i = 0; i < static_cast<int>(g_gui.conics.size()); ++i) {
        ConicRow& c = g_gui.conics[i];
        ImGui::PushID(2000 + i);
        ImGui::SeparatorText(conicName(c.type));

        colorPicker(0, &c.color);
        ImGui::SameLine();
        ImGui::Checkbox("ver", &c.visible);
        ImGui::SameLine(fullW - 30.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.92f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
        if (ImGui::Button("\xC3\x97", ImVec2(24.0f, 22.0f))) {
            g_gui.conics.erase(g_gui.conics.begin() + i);
            ImGui::PopStyleColor(2);
            ImGui::PopID();
            --i;
            continue;
        }
        ImGui::PopStyleColor(2);

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::DragInt(c.type == 2 ? "Vertice X" : "Xc", &c.xc, 1.0f, 0, 0, "%d");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.30f);
        ImGui::DragInt(c.type == 2 ? "Vertice Y" : "Yc", &c.yc, 1.0f, 0, 0, "%d");
        if (c.type == 0) {
            ImGui::SetNextItemWidth(fullW * 0.30f);
            ImGui::DragInt("Rx", &c.rx, 1.0f, 0, 0, "%d");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fullW * 0.30f);
            ImGui::DragInt("Ry", &c.ry, 1.0f, 0, 0, "%d");
        } else if (c.type == 1) {
            ImGui::SetNextItemWidth(fullW * 0.30f);
            ImGui::DragInt("R", &c.r, 1.0f, 0, 0, "%d");
        } else if (c.type == 2) {
            ImGui::SetNextItemWidth(fullW * 0.30f);
            ImGui::DragInt("P (foco)", &c.p, 1.0f, 0, 0, "%d");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fullW * 0.30f);
            ImGui::Combo("##por", &c.orient, "Arriba\0Abajo\0Derecha\0Izquierda\0");
            ImGui::SetNextItemWidth(fullW * 0.30f);
            ImGui::DragInt("Alcance", &c.alcance, 1.0f, 1, 100000, "%d");
        } else {
            ImGui::SetNextItemWidth(fullW * 0.30f);
            ImGui::DragInt("A", &c.a, 1.0f, 0, 0, "%d");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fullW * 0.30f);
            ImGui::DragInt("B", &c.b, 1.0f, 0, 0, "%d");
            ImGui::SetNextItemWidth(fullW * 0.30f);
            ImGui::Combo("##hor", &c.orient, "Horizontal\0Vertical\0");
            ImGui::SetNextItemWidth(fullW * 0.30f);
            ImGui::DragInt("Alcance", &c.alcance, 1.0f, 1, 100000, "%d");
        }
        ImGui::PopStyleColor();

        ImGui::SetNextItemWidth(fullW * 0.48f);
        ImGui::Combo("##fm", &c.fillMode, "Contorno\0Lineas (centro)\0Solido\0");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fullW * 0.34f);
        ImGui::SliderInt("grosor", &c.width, 1, 4);

        drawConicInfo(c);

        if (primaryButton("Enviar puntos al arreglo", ImVec2(-1.0f, 0.0f))) {
            sendConicToArray(c);
        }

        if (ImGui::CollapsingHeader("Tabla del algoritmo (Pk, Xk, Yk)")) {
            MidResult m = conicMid(c);
            if (ImGui::BeginTable("##tab", 5,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("K");
                ImGui::TableSetupColumn("Pk");
                ImGui::TableSetupColumn("Xk");
                ImGui::TableSetupColumn("Yk");
                ImGui::TableSetupColumn("Region");
                ImGui::TableHeadersRow();
                for (const MidStep& s : m.pasos) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%d", s.k);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%ld", s.p);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", s.x);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d", s.y);
                    ImGui::TableSetColumnIndex(4);
                    if (c.type == 1) {
                        ImGui::TextUnformatted("Octante");
                    } else {
                        ImGui::Text("Region %d", s.region);
                    }
                }
                ImGui::EndTable();
            }
            std::string pts = "Cuadrante I: ";
            for (const ElPt& e : m.cuarto) {
                char tmp[32];
                std::snprintf(tmp, sizeof(tmp), "(%d,%d) ", e.x, e.y);
                pts += tmp;
            }
            ImGui::TextWrapped("%s", pts.c_str());
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

}  // namespace

const unsigned char* guiPalette(int idx) {
    if (idx < 0) idx = 0;
    return PAL[idx % PAL_N];
}

int guiPaletteCount() {
    return PAL_N;
}

void guiRecomputeSegs() {
    recomputeSegs();
}

void guiPointLabel(int idx, char* out, std::size_t sz) {
    if (sz == 0) return;
    if (idx >= 0 && idx < 26) {
        std::snprintf(out, sz, "%c", 'A' + idx);
    } else if (idx >= 26) {
        std::snprintf(out, sz, "%c%d", 'A' + (idx % 26), (idx / 26));
    } else {
        std::snprintf(out, sz, "?");
    }
}

bool guiInit(void* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImVector<ImWchar> ranges;
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddChar(0x0394);  // Delta
        builder.AddChar(0x03B8);  // theta
        builder.AddChar(0x221E);  // infinito
        builder.AddChar(0x00B1);  // plus-minus
        builder.AddChar(0x2192);  // flecha
        builder.AddChar(0x00B0);  // grado
        builder.AddChar(0x00D7);  // multiplicacion
        builder.AddChar(0x2302);  // casa
        builder.AddChar(0x2194);  // flecha doble
        builder.AddChar(0x251C);  // T izquierda
        builder.AddChar(0x2500);  // linea horizontal
        builder.BuildRanges(&ranges);
    }

    static const char* candidates[] = {
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    const char* sel = nullptr;
    for (const char* path : candidates) {
        if (fileExists(path)) {
            sel = path;
            break;
        }
    }
    if (sel) {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        io.Fonts->AddFontFromFileTTF(sel, 13.0f, &cfg, ranges.Data);
        ImFont* big = io.Fonts->AddFontFromFileTTF(sel, 16.0f, &cfg, ranges.Data);
        io.FontDefault = big;
        g_gui.unicode = true;
    } else {
        io.Fonts->AddFontDefault();
        g_gui.unicode = false;
    }

    ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(window), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    applyGeoGebraStyle();

    return true;
}

void guiBeginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void guiBuild() {
    // Siempre dibujar toolbar
    drawToolbar();

    if (!g_gui.showPanel) return;

    ImGuiIO& io = ImGui::GetIO();
    const float pw = 420.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - pw, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(pw, io.DisplaySize.y), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.98f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoTitleBar;
    if (!ImGui::Begin("##panel", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Graficador Doker");
    ImGui::SameLine(pw - 60.0f);
    ImGui::TextDisabled("H: ocultar");

    ImGui::Spacing();

    // --- Archivo: exportar / importar proyecto ---
    ImGui::TextColored(ImVec4(0.3f, 0.3f, 0.35f, 1.0f), "ARCHIVO (.graf / .csv / .json)");
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::SetNextItemWidth(pw - 190.0f);
    ImGui::InputText("##path", g_gui.filePath, sizeof(g_gui.filePath));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.52f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("Guardar", ImVec2(74.0f, 0.0f))) {
        std::string err;
        if (projectSave(g_gui.filePath, err)) {
            std::snprintf(g_gui.fileMsg, sizeof(g_gui.fileMsg),
                          "Guardado: %s", g_gui.filePath);
        } else {
            std::snprintf(g_gui.fileMsg, sizeof(g_gui.fileMsg),
                          "Error al guardar: %s", err.c_str());
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.56f, 0.68f, 0.36f, 1.0f));
    if (ImGui::Button("Abrir", ImVec2(70.0f, 0.0f))) {
        std::string err;
        if (projectLoad(g_gui.filePath, err)) {
            std::snprintf(g_gui.fileMsg, sizeof(g_gui.fileMsg),
                          "Abierto: %s", g_gui.filePath);
            g_gui.wantResample = true;
            g_gui.wantFitAll = true;
        } else {
            std::snprintf(g_gui.fileMsg, sizeof(g_gui.fileMsg),
                          "Error al abrir: %s", err.c_str());
        }
    }
    ImGui::PopStyleColor(3);
    if (g_gui.fileMsg[0]) {
        ImGui::TextWrapped("%s", g_gui.fileMsg);
    }
    ImGui::Separator();
    ImGui::Spacing();

    // Tabs
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 4.0f);
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        if (ImGui::BeginTabItem("  Puntos  ")) {
            pointsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();

    ImGui::End();
}

void guiEndFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool guiWantsMouse() {
    return ImGui::GetIO().WantCaptureMouse;
}

bool guiWantsKeyboard() {
    return ImGui::GetIO().WantCaptureKeyboard;
}
