cmake_minimum_required(VERSION 3.10.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PROJECT_NAME "Copper")
project(${PROJECT_NAME})

find_package(LLVM REQUIRED CONFIG)
find_package(LLD REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM_LINK_LLVM_DYLIB = ${LLVM_LINK_LLVM_DYLIB}")
message(STATUS "LLVM PATH: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLD PATH: ${LLD_INCLUDE_DIRS}")

include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${LLD_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})

add_definitions(-D__STDC_CONSTANT_MACROS)
add_definitions(-D__STDC_FORMAT_MACROS)
add_definitions(-D__STDC_LIMIT_MACROS)
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)

file(GLOB_RECURSE SOURCE_FILES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
    ${LLD_INCLUDE_DIRS}
)

if(LLVM_LINK_LLVM_DYLIB)
    set(llvm_libs LLVM)
else()
    llvm_map_components_to_libnames(
        llvm_libs
        support
        core
        native
        mc
        object
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${llvm_libs}
    lldCOFF
    lldELF
    lldCommon
)

# Bundled runtime objects/libs
#
# link.cpp expects a "runtime/<windows|linux>" folder next to the produced
# executable, containing the crt objects and libs it feeds to lld-link /
# ld.lld. Rather than hardcoding paths, we detect whatever compilers are
# available and ask them directly (via -dumpmachine / -print-file-name)
# where those files live, then copy them next to the binary.
#
# You can override the auto-detection with:
#   -DENABLE_WINDOWS_RUNTIME=ON/OFF
#   -DENABLE_LINUX_RUNTIME=ON/OFF
# Leaving them at AUTO just uses whatever toolchains are found.

set(ENABLE_WINDOWS_RUNTIME "AUTO" CACHE STRING "Bundle the Windows/MinGW runtime (ON/OFF/AUTO)")
set(ENABLE_LINUX_RUNTIME   "AUTO" CACHE STRING "Bundle the Linux runtime (ON/OFF/AUTO)")
set_property(CACHE ENABLE_WINDOWS_RUNTIME PROPERTY STRINGS AUTO ON OFF)
set_property(CACHE ENABLE_LINUX_RUNTIME   PROPERTY STRINGS AUTO ON OFF)

set(_RUNTIME_COMPILER_CANDIDATES
    gcc cc clang
    x86_64-w64-mingw32-gcc x86_64-w64-mingw32-clang
    i686-w64-mingw32-gcc
    x86_64-linux-gnu-gcc
    x86_64-pc-linux-gnu-gcc
)

set(WINDOWS_RUNTIME_CC "")
set(LINUX_RUNTIME_CC "")

foreach(_cand ${_RUNTIME_COMPILER_CANDIDATES})
    find_program(_runtime_cc_${_cand} NAMES ${_cand})
    if(_runtime_cc_${_cand})
        execute_process(
            COMMAND ${_runtime_cc_${_cand}} -dumpmachine
            OUTPUT_VARIABLE _cc_triple
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        string(TOLOWER "${_cc_triple}" _cc_triple)

        if(_cc_triple MATCHES "mingw|windows" AND NOT WINDOWS_RUNTIME_CC)
            set(WINDOWS_RUNTIME_CC "${_runtime_cc_${_cand}}")
            set(WINDOWS_RUNTIME_CC_TRIPLE "${_cc_triple}")
        elseif(_cc_triple MATCHES "linux" AND NOT LINUX_RUNTIME_CC)
            set(LINUX_RUNTIME_CC "${_runtime_cc_${_cand}}")
            set(LINUX_RUNTIME_CC_TRIPLE "${_cc_triple}")
        endif()
    endif()
endforeach()

set(WINDOWS_RUNTIME_CC_ARGS "")
if(WINDOWS_RUNTIME_CC MATCHES "clang(\\.exe)?$")
    set(WINDOWS_RUNTIME_CC_ARGS --target=x86_64-w64-mingw32)
endif()

if(ENABLE_WINDOWS_RUNTIME STREQUAL "AUTO")
    if(WINDOWS_RUNTIME_CC)
        set(_BUNDLE_WINDOWS_RUNTIME TRUE)
    else()
        set(_BUNDLE_WINDOWS_RUNTIME FALSE)
    endif()
elseif(ENABLE_WINDOWS_RUNTIME)
    if(NOT WINDOWS_RUNTIME_CC)
        message(FATAL_ERROR "ENABLE_WINDOWS_RUNTIME=ON but no mingw/windows-targeting compiler was found")
    endif()
    set(_BUNDLE_WINDOWS_RUNTIME TRUE)
else()
    set(_BUNDLE_WINDOWS_RUNTIME FALSE)
endif()

if(ENABLE_LINUX_RUNTIME STREQUAL "AUTO")
    if(LINUX_RUNTIME_CC)
        set(_BUNDLE_LINUX_RUNTIME TRUE)
    else()
        set(_BUNDLE_LINUX_RUNTIME FALSE)
    endif()
elseif(ENABLE_LINUX_RUNTIME)
    if(NOT LINUX_RUNTIME_CC)
        message(FATAL_ERROR "ENABLE_LINUX_RUNTIME=ON but no linux-targeting compiler was found")
    endif()
    set(_BUNDLE_LINUX_RUNTIME TRUE)
else()
    set(_BUNDLE_LINUX_RUNTIME FALSE)
endif()

set(WINDOWS_RUNTIME_FILES
    crt2.o crtbegin.o
    libmingw32.a libgcc.a libgcc_eh.a libmoldname.a
    libmingwex.a libmsvcrt.a libadvapi32.a libshell32.a
    libuser32.a libkernel32.a crtend.o
)

set(LINUX_RUNTIME_FILES
    crt1.o crti.o crtbeginS.o crtendS.o crtn.o
    libc.a libgcc.a
)

# Asks a compiler where each file lives (-print-file-name) and copies it
# next to the built executable. Missing files just get a warning instead
# of failing the build outright, since not every distro ships everything
# under the same name.
function(bundle_runtime_files compiler compiler_args files dest_dir)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dest_dir}"
    )

    foreach(_file ${files})
        execute_process(
            COMMAND ${compiler} ${compiler_args} -print-file-name=${_file}
            OUTPUT_VARIABLE _resolved_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(NOT _resolved_path STREQUAL "${_file}" AND EXISTS "${_resolved_path}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_resolved_path}" "${dest_dir}/${_file}"
            )
        else()
            message(WARNING "[${compiler}] couldn't locate '${_file}', skipping it")
        endif()
    endforeach()
endfunction()

if(_BUNDLE_WINDOWS_RUNTIME)
    bundle_runtime_files(
        "${WINDOWS_RUNTIME_CC}" "${WINDOWS_RUNTIME_CC_ARGS}"
        "${WINDOWS_RUNTIME_FILES}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/runtime/windows"
    )
endif()

if(_BUNDLE_LINUX_RUNTIME)
    bundle_runtime_files(
        "${LINUX_RUNTIME_CC}" ""
        "${LINUX_RUNTIME_FILES}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/runtime/linux"
    )
endif()

message(STATUS "Bundled runtime - Windows: ${_BUNDLE_WINDOWS_RUNTIME} (${WINDOWS_RUNTIME_CC})")
message(STATUS "Bundled runtime - Linux:   ${_BUNDLE_LINUX_RUNTIME} (${LINUX_RUNTIME_CC})")