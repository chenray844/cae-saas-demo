cmake_minimum_required(VERSION 3.22)

project(OccApp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog REQUIRED)
find_package(freetype REQUIRED)
find_package(OpenCASCADE REQUIRED)

add_executable(${PROJECT_NAME}
    src/viewer/WasmOcctView.cpp
    main.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        src/viewer
        ${OpenCASCADE_INCLUDE_DIR}
)

target_link_directories(${PROJECT_NAME}
    PRIVATE
        ${OpenCASCADE_LIBRARY_DIR}
)

set(OpenCASCADE_LIBS)
list(APPEND OpenCASCADE_LIBS
    TKRWMesh 
    TKBinXCAF 
    TKBin 
    TKBinL 
    TKOpenGles 
    TKXCAF 
    TKVCAF 
    TKCAF 
    TKV3d 
    TKHLR 
    TKMesh 
    TKService 
    TKShHealing 
    TKPrim 
    TKTopAlgo 
    TKGeomAlgo 
    TKBRep 
    TKGeomBase 
    TKG3d 
    TKG2d 
    TKMath 
    TKLCAF 
    TKCDF 
    TKernel
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${OpenCASCADE_LIBS}
        freetype 
        spdlog::spdlog
)




set(emscripten_link_options)
set(emscripten_compile_options)

list(APPEND emscripten_link_options
    "-sWASM=1"
    "-sMODULARIZE=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXPORT_NAME=OccApp"
    "-sSINGLE_FILE=1"
    "-lembind"
    "-sUSE_ZLIB=1"
    "-sMAXIMUM_MEMORY=4GB"
    # "-sEXPORTED_RUNTIME_METHODS=['ENV']"
    "-s MAX_WEBGL_VERSION=2"
)

set(emscripten_debug_options)
set(DEBUGINFO "PROFILE" CACHE STRING "Type of debug info")
set_property(CACHE DEBUGINFO PROPERTY 
    STRINGS
        NONE 
        READABLE_JS
        PROFILE
        DEBUG_NATIVE 
)

if (DEBUGINFO STREQUAL "NONE")
    list(APPEND emscripten_debug_options
        "-g0"
    )
elseif(DEBUGINFO STREQUAL "READABLE_JS")
    list(APPEND emscripten_debug_options
        "-g1"
    )
    list(APPEND emscripten_link_options
        "-sDEMANGLE_SUPPORT=1"
    )
elseif(DEBUGINFO STREQUAL "PROFILE")
    list(APPEND emscripten_debug_options
        "-g2"
    )
    list(APPEND emscripten_link_options
        "-sDEMANGLE_SUPPORT=1"
    )
elseif(DEBUGINFO STREQUAL "DEBUG_NATIVE")
    list(APPEND emscripten_debug_options
        "-g3"
    )
    list(APPEND emscripten_link_options
        "-sASSERTIONS=1"
        "-sDEMANGLE_SUPPORT=1"
        "-sNO_DISABLE_EXCEPTION_CATCHING=1"
    )
endif()

set(emscripten_optimizations)
set(OPTIMIZE "SMALLEST_WITH_CLOSURE" CACHE STRING "Emscripten optimization")
set_property(CACHE OPTIMIZE PROPERTY 
    STRINGS
        NO_OPTIMIZATION
        LITTLE 
        MORE 
        BEST 
        SMALL 
        SMALLEST 
        SMALLEST_WITH_CLOSURE
)

if(OPTIMIZE STREQUAL "NO_OPTIMIZATION")
    list(APPEND emscripten_optimizations
        "-O0"
    )
elseif(OPTIMIZE STREQUAL "LITTLE")
    list(APPEND emscripten_optimizations
        "-O1"
    )
elseif(OPTIMIZE STREQUAL "MORE")
    list(APPEND emscripten_optimizations
        "-O2"
    )
elseif(OPTIMIZE STREQUAL "BEST")
    list(APPEND emscripten_optimizations
        "-O3"
    )
elseif(OPTIMIZE STREQUAL "SMALL")
    list(APPEND emscripten_optimizations
        "-Os"
    )
elseif(OPTIMIZE STREQUAL "SMALLEST")
    list(APPEND emscripten_optimizations
        "-Oz"
    )
elseif(OPTIMIZE STREQUAL "SMALLEST_WITH_CLOSURE")
    list(APPEND emscripten_optimizations
        "-Oz"
    )
    list(APPEND emscripten_link_options
        "--closure 1"
    )
endif()

target_compile_options(${PROJECT_NAME}
    PUBLIC 
        ${emscripten_compile_options}
        ${emscripten_optimizations}
        ${emscripten_debug_options}
)

target_link_options(${PROJECT_NAME}
    PUBLIC 
        ${emscripten_link_options}
        ${emscripten_optimizations}
        ${emscripten_debug_options}
)



add_custom_command(
    TARGET OccApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OccApp> 
    ${CMAKE_CURRENT_SOURCE_DIR}/../assets/wasm/
)