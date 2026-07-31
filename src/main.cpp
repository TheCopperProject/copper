#define TOML_EXCEPTIONS 0
#include <toml.hpp>
#include <filesystem>
#include <format>
#include <string>
#include <iostream>
#include <fstream>
#include <optional>
#include <vector>

namespace fs = std::filesystem;

struct CommandInfo {
    std::string name;
    std::string description;
};

struct ProjectConfig {
    std::string name;
    std::string version;
    std::string type;
    std::string sourceDir;
    std::string buildDir;
    std::string entryRoot;
};

static std::string main_template =
    R"(func main(argc: i32, argv: char**) -> i32 {
    return 0;
})";

static std::string path;

static std::vector<CommandInfo> commands = {
    {"help", "Prints information about available commands."},
    {"init", "Initializes a new project in the current directory."},
    {"build", "Compiles the project and generates executables or libraries."},
    {"run", "Executes the project."},
    {"check", "Checks the code for errors without compiling binary artifacts."},
    {"clean", "Clears build artifacts and temporary files."},
    {"fmt", "Formats the source code according to standard rules."}};

static void printDefault() {
    std::cerr << "Usage: " << path << " <command> [arguments]\n";
    std::cerr << "Type '" << path << " help' for a list of commands.\n";
}

static std::optional<ProjectConfig> loadConfig(
    const std::string& configPath = "copper.toml") {
    if (!fs::exists(configPath)) {
        std::cerr << "Error: The configuration file \"" << configPath
                  << "\" was not found.\n";
        return std::nullopt;
    }

    toml::parse_result result = toml::parse_file(configPath);
    if (!result) {
        std::cerr << "Error: Parsing " << configPath << " failed:\n"
                  << result.error() << "\n";
        return std::nullopt;
    }

    toml::table& config = result.table();
    ProjectConfig cfg;
    bool valid = true;

    if (auto v = config["project"]["name"].value<std::string>())
        cfg.name = *v;
    else {
        std::cerr
            << "Error: Missing or invalid required field \"project.name\" in "
            << configPath << ".\n";
        valid = false;
    }

    if (auto v = config["project"]["version"].value<std::string>())
        cfg.version = *v;
    else {
        std::cerr << "Error: Missing or invalid required field "
                     "\"project.version\" in "
                  << configPath << ".\n";
        valid = false;
    }

    if (auto v = config["project"]["type"].value<std::string>()) {
        if (*v != "bin" && *v != "static_lib" && *v != "dynamic_lib") {
            std::cerr << "Error: \"project.type\" must be one of \"bin\", "
                         "\"static_lib\", \"dynamic_lib\" (got \""
                      << *v << "\") in " << configPath << ".\n";
            valid = false;
        } else {
            cfg.type = *v;
        }
    } else {
        std::cerr
            << "Error: Missing or invalid required field \"project.type\" in "
            << configPath << ".\n";
        valid = false;
    }

    if (auto v = config["directories"]["source"].value<std::string>())
        cfg.sourceDir = *v;
    else {
        std::cerr << "Error: Missing or invalid required field "
                     "\"directories.source\" in "
                  << configPath << ".\n";
        valid = false;
    }

    if (auto v = config["directories"]["build"].value<std::string>())
        cfg.buildDir = *v;
    else {
        std::cerr << "Error: Missing or invalid required field "
                     "\"directories.build\" in "
                  << configPath << ".\n";
        valid = false;
    }

    if (auto v = config["entry"]["root"].value<std::string>())
        cfg.entryRoot = *v;
    else {
        std::cerr
            << "Error: Missing or invalid required field \"entry.root\" in "
            << configPath << ".\n";
        valid = false;
    }

    if (!valid)
        return std::nullopt;
    return cfg;
}

int main(int argc, char** argv) {
    if (argc <= 0)
        return 1;
    path = argv[0];

    if (argc <= 1) {
        printDefault();
        return 0;
    }

    std::string command = argv[1];

    if (command == "help") {
        std::cout << "Available commands:\n\n";
        for (const auto& cmd : commands) {
            std::cout << "  " << cmd.name;
            if (cmd.name.length() < 8)
                std::cout << "\t\t";
            else
                std::cout << "\t";
            std::cout << cmd.description << "\n";
        }
    } else if (command == "init") {
        if (argc <= 2) {
            std::cerr << "Expected " << path << " init {name}";
            return 0;
        }
        std::string name = argv[2];
        static std::string default_toml = std::format(R"([project]
name = "{}"
version = "0.1.0"
type = "bin" # options are bin, static_lib, dynamic_lib
[directories]
source = "src"
build = "build"
[entry]
root = "main"
[compiler]
# flags = ["-Wall", "-Wextra"]
# defines = ["ENABLE_LOGGING=1"]
[link]
# libraries = ["sdl2", "openal"] # example linking, links -lsdl2, -lopenal
# Package dependencies (git modules)
[dependencies]
# format = "[www.github.com/JonkIsKindaCool/format](https://www.github.com/JonkIsKindaCool/format)",
                                                      name);

        fs::create_directories(name + "/src");

        std::ofstream configFile(name + "/copper.toml");
        if (!configFile.is_open()) {
            std::cerr << "Error: Could not create the file copper.toml!"
                      << std::endl;
            return 1;
        }
        configFile << default_toml << std::endl;
        configFile.close();

        std::ofstream mainFile(name + "/src/main.cop");
        if (!mainFile.is_open()) {
            std::cerr << "Error: Could not create the file main.cop!"
                      << std::endl;
            return 1;
        }
        mainFile << main_template << std::endl;
        mainFile.close();

        std::cout << "Succesfully created project " << name << "!";
    } else if (command == "build") {
        std::optional<ProjectConfig> config = loadConfig();
        if (!config)
            return 1;

    } else if (command == "run") {
        std::optional<ProjectConfig> config = loadConfig();
        if (!config)
            return 1;

    } else if (command == "check") {
        std::optional<ProjectConfig> config = loadConfig();
        if (!config)
            return 1;

    } else if (command == "clean") {
        std::optional<ProjectConfig> config = loadConfig();
        if (!config)
            return 1;

    } else if (command == "fmt") {
        std::optional<ProjectConfig> config = loadConfig();
        if (!config)
            return 1;

    } else {
        std::cout << "Use the help command for more info" << "\n";
    }

    return 0;
}
