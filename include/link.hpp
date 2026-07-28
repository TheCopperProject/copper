#pragma once
#include <string>
#include <vector>

enum class TargetOS
{
    Windows,
    Linux,
};

TargetOS targetOSFromTriple(const std::string &triple);

std::string runtimeSubdir(TargetOS os);

// Linkea uno o más archivos objeto (uno por cada módulo/unidad de
// compilación emitida) en un único ejecutable. El orden de objPaths no
// importa para la resolución de símbolos (lld resuelve globalmente), salvo
// que dependas de inicialización estática con orden implícito.
bool linkExecutable(const std::vector<std::string> &objPaths, const std::string &exePath,
                     const char *argv0, TargetOS os, std::string &errMsg);