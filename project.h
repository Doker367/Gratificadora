#pragma once

#include <string>

// Exporta / importa el proyecto completo (funciones, puntos, conexiones,
// conicas y vista) en un archivo de texto plano (.graf).
bool projectSave(const std::string& path, std::string& err);
bool projectLoad(const std::string& path, std::string& err);
