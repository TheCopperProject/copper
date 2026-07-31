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

bool linkExecutable(const std::vector<std::string> &objPaths, const std::string &exePath,
                     const char *argv0, TargetOS os, std::string &errMsg);