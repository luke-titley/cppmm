//------------------------------------------------------------------------------
// vfx-rs
//------------------------------------------------------------------------------
#include "cppmm_ast.hpp"
#include "cppmm_ast_add_c.hpp"
#include "cppmm_ast_read.hpp"
#include "cppmm_ast_write_c.hpp"
#include "cppmm_ast_write_cmake.hpp"
#include "cppmm_ast_write_rustsys.hpp"

#include "filesystem.hpp"
#include "pystring.h"

#include "fmt/format.h"

#include <fstream>
#include <iostream>

#include <llvm/Support/CommandLine.h> // TODO: consider https://github.com/jarro2783/cxxopts to remove clang dep

#define SPDLOG_ACTIVE_LEVEL TRACE

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace cl = llvm::cl;
namespace fs = ghc::filesystem;

static cl::opt<std::string> opt_in_dir(cl::Positional, cl::desc("<input dir>"),
                                       cl::Required);

static cl::opt<std::string> opt_out_dir(
    "o",
    cl::desc("Directory under which output C and Rust binding projects will be "
             "written. Defaults to current directory if not specified."));

static cl::opt<std::string> opt_project_name(
    "p", cl::desc("Name for the project. This will determine the name of the "
                  "generated C library and the rust crate"));

static cl::list<std::string>
    opt_lib("l", cl::desc("Library that bindings link to."), cl::ZeroOrMore);

static cl::list<std::string>
    opt_lib_dir("L", cl::desc("Directories you can find libraries in."),
                cl::ZeroOrMore);

static cl::opt<int> opt_verbosity(
    "v", cl::desc("Verbosity. 0=errors, 1=warnings, 2=info, 3=debug, 4=trace"),
    cl::init(1));

static cl::opt<int> opt_version_major("major", cl::desc("Major version"),
                                      cl::init(0));

static cl::opt<int> opt_version_minor("minor", cl::desc("Minor version"),
                                      cl::init(1));

static cl::opt<int> opt_version_patch("patch", cl::desc("Patch version"),
                                      cl::init(0));

template <typename T> std::vector<std::string> to_vector(const T& t) {
    std::vector<std::string> result;
    for (auto& i : t) {
        result.push_back(i);
    }

    return result;
}

void generate(const char* input, const char* project_name, const char* output,
              const char* rust_output, const cppmm::Libs& libs,
              const cppmm::LibDirs& lib_dirs, int version_major,
              int version_minor, int version_patch) {
    const std::string input_directory = input;
    const std::string output_directory = output;

    // Read the json ast
    auto cpp_ast = cppmm::read::json(input_directory);

    // Add the c translation units
    auto starting_point = cpp_ast.tus.size();
    cppmm::transform::add_c(output_directory, cpp_ast);

    // Save out only the c translation units
    std::string c_project_name = fmt::format("{}-c", project_name);
    cppmm::write::cerrors(output_directory.c_str(), cpp_ast, starting_point,
                          project_name);
    cppmm::write::c(c_project_name.c_str(), cpp_ast, starting_point);

    // Create a cmake file as well
    cppmm::write::cmake(c_project_name.c_str(), cpp_ast, starting_point, libs,
                        lib_dirs, version_major, version_minor, version_patch,
                        project_name);

    std::string cwd = fs::current_path().string();
    std::string c_dir = pystring::os::path::abspath(output_directory, cwd);

    cppmm::rust_sys::write(rust_output, project_name, c_dir.c_str(), cpp_ast,
                           starting_point, libs, lib_dirs, version_major,
                           version_minor, version_patch);
}

int main(int argc, char** argv) {
    auto _console = spdlog::stdout_color_mt("console");

    cl::ParseCommandLineOptions(
        argc, argv,
        " Generates a C binding project from input JSON AST output by astgen");

    // Set up logging
    switch (opt_verbosity) {
    case 0:
        spdlog::set_level(spdlog::level::err);
        break;
    case 1:
        spdlog::set_level(spdlog::level::warn);
        break;
    case 2:
        spdlog::set_level(spdlog::level::info);
        break;
    case 3:
        spdlog::set_level(spdlog::level::debug);
        break;
    case 4:
        spdlog::set_level(spdlog::level::trace);
        break;
    default:
        spdlog::set_level(spdlog::level::warn);
        break;
    }
    spdlog::set_pattern("%20s:%4# %^[%5l]%$ %v");

    // Grab the output directory from the options, defaulting to $CWD if not
    // specified.
    // Our C and Rust projects will be placed in directories under this,
    // named project_name-c and project_name-sys, respectively.
    fs::path cwd = fs::current_path();
    fs::path out_dir = cwd;
    if (opt_out_dir != "") {
        out_dir = fs::path(opt_out_dir.c_str());
    }

    // must supply a project name
    std::string project_name;
    if (opt_project_name != "") {
        project_name = opt_project_name;
    } else {
        SPDLOG_CRITICAL("Must supply a project name with the -p flag");
        return -3;
    }

    // create the c and rust directories
    fs::path c_dir = out_dir / fmt::format("{}-c", project_name);
    fs::path rust_dir = out_dir / fmt::format("{}-sys", project_name);

    // attempt to create the output directory if it doesn't exist
    if (!fs::is_directory(c_dir) && !fs::create_directories(c_dir)) {
        SPDLOG_CRITICAL("Could not create output directory \"{}\"",
                        c_dir.string());
        return -1;
    }

    if (!fs::is_directory(rust_dir) && !fs::create_directories(rust_dir)) {
        SPDLOG_CRITICAL("Could not create Rust output directory \"{}\"",
                        rust_dir.string());
        return -2;
    }

    fs::path rust_src_dir = rust_dir / "src";
    if (!fs::is_directory(rust_src_dir) &&
        !fs::create_directories(rust_src_dir)) {
        SPDLOG_CRITICAL("Could not create Rust src directory \"{}\"",
                        rust_src_dir.string());
        return -2;
    }

    auto libs = to_vector(opt_lib);
    auto lib_dirs = to_vector(opt_lib_dir);
    generate(opt_in_dir.c_str(), project_name.c_str(), c_dir.c_str(),
             rust_dir.c_str(), libs, lib_dirs, opt_version_major,
             opt_version_minor, opt_version_patch);

    return 0;
}
