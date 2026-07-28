cmake_minimum_required(VERSION 3.10.0)

set(CMAKE_CXX_STANDARD 17)
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