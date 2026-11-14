CMAKE_MINIMUM_REQUIRED(VERSION 3.22.1)
project(LIBRARY_OUTPUT_PATH)

message("CMAKE_VERSION: ${CMAKE_VERSION} ")
message("CMAKE_HOST_SYSTEM_NAME: ${CMAKE_HOST_SYSTEM_NAME} ")
message("CMAKE_HOST_SYSTEM: ${CMAKE_HOST_SYSTEM} ")
message("CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME} ")
message("CMAKE_SYSTEM: ${CMAKE_SYSTEM} ")
message("CMAKE_HOST_UNIX: ${CMAKE_HOST_UNIX} ")
message("CMAKE_HOST_WIN32: ${CMAKE_HOST_WIN32} ")
message("CMAKE_C_COMPILER: ${CMAKE_C_COMPILER} ")
message("CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER} ")
message("CMAKE_BUILD_TOOLS: ${CMAKE_BUILD_TOOLS} ")

#---------------------------------------------------------------------------------------
# Compiler config
#---------------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_CXX_STANDARD 20)

# 默认 Debug；若通过 -DCMAKE_BUILD_TYPE=Release/Debug 显式传入则以其为准
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif ()

message(STATUS "=== 设置编译输出目录和编译选项开始 ===")
if (WIN32)
    if (MSVC)
        add_compile_options(/wd4819)
        add_compile_options(/wd4828)
        add_compile_options(/wd4244)
        add_compile_options(/wd4267)
        add_compile_options(/wd4099)
        add_compile_options(/wd4005)
        add_compile_options(/wd4200)
        add_compile_options(/wd4065)
        add_compile_options(/wd4091)
        add_compile_options(/wd4251)
        add_compile_options(/wd4996)
        add_compile_options(/wd4275)
        add_compile_options(/wd4316)
        add_compile_options(/wd4244)
        add_compile_options(/wd4267)
        add_compile_options(/wd4244)
        add_compile_options(/wd4099)
        add_compile_options(/wd4018)
        add_compile_options(/wd4834)
        add_compile_options(/wd4065)
        add_compile_options(/wd4311)
        add_compile_options(/wd4309)
        add_compile_options(/wd4013)
        add_compile_options(/wd4024)
        add_compile_options(/wd4477)
        add_compile_options(/wd4133)
        add_compile_options(/wd4047)
        add_compile_options(/wd4099)
        add_compile_options(/wd4217)
        add_compile_options(/wd4286)
        add_compile_options(/wd4291)
        add_compile_options(/wd4099)
        add_compile_options(/wd4100)
    endif()
endif()

message("CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE} ")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_BUILD_TYPE "Debug")
    if (UNIX)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -Wno-deprecated-declarations")
        #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O0 -Wall -g3 -ggdb -fPIC -Wextra -Wno-unused-parameter -Wno-conversion-null -Wno-unused-but-set-variable -Wno-int-to-pointer-cast -Wno-array-bounds -static-libstdc++")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -O0 -Wall -g3 -ggdb -fPIC -static-libstdc++ -Wno-deprecated-declarations -Wno-volatile")
        add_definitions(
                -D_DEBUG
                -DCHECK_MEM_LEAK
                -Wcomment
                -fPIC
                -Wno-error
                -Wpointer-arith
                #-DCOMPILE_CHECK_STRING_FORMAT
        )
    elseif (WIN32)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWIN32_LEAN_AND_MEAN -D_WINSOCK_DEPRECATED_NO_WARNINGS -D_LIB -D_WIN32 -D_CONSOLE -DUNICODE -D_UNICODE -D_WINDOWS -DWIN32 -D_DEBUG -DLUA_COMPAT_5_1 -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -D_USRDLL -DDLL_EXPORTS -DMYSQLPP_MAKING_DLL -DHAVE_MYSQL_SSL_SET  -DBUILDING_LIBCURL -DCURL_STATICLIB ")
    endif ()
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_BUILD_TYPE "Release")
    if (UNIX)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -Wno-deprecated-declarations")
        #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -fPIC -std=c++11 -O3 -g -Wall")
        #大大增加编译失败的机会或不可预知的程序行为（包括错误）。 这样做将得不偿失，记住过犹不及。 在gcc 4.x.中使用-O3是不推荐的
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -fPIC -static-libstdc++ -Wno-deprecated-declarations -Wno-volatile")
        add_definitions(
                -DNDEBUG
                -Wcomment
                -fPIC
                -Wno-error
                -Wpointer-arith
        )
    elseif (WIN32)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWIN32_LEAN_AND_MEAN -D_WINSOCK_DEPRECATED_NO_WARNINGS -D_LIB -D_WIN32 -D_CONSOLE -DUNICODE -D_UNICODE -D_WINDOWS -DWIN32 -D_DEBUG -DLUA_COMPAT_5_1 -D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_DEPRECATE -D_CRT_NONSTDC_NO_DEPRECATE -D_USRDLL -DDLL_EXPORTS -DMYSQLPP_MAKING_DLL -DHAVE_MYSQL_SSL_SET  -DBUILDING_LIBCURL -DCURL_STATICLIB ")
    endif ()
endif ()
message("CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")
message("CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")

# 设置编译输出目录
set(CMAKE_ROOT_DIR ${PROJECT_SOURCE_DIR})
set(CMAKE_BIN_DIR "${CMAKE_ROOT_DIR}/Bin/${CMAKE_BUILD_TYPE}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BIN_DIR}")
set(LIBRARY_OUTPUT_PATH "${CMAKE_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BIN_DIR}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${CMAKE_BIN_DIR}")
# RPATH 统一为 $ORIGIN（与本产物同级目录）：不再把构建机的绝对路径写进 RUNPATH，产物连同依赖 .so 一起拷到任意目录都能按同级目录解析；LD_LIBRARY_PATH 仅作可选兜底。
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
set(CMAKE_INSTALL_RPATH "$ORIGIN")
message(STATUS "PROJECT_SOURCE_DIR = ${PROJECT_SOURCE_DIR}")
message(STATUS "CMAKE_ROOT_DIR = ${CMAKE_ROOT_DIR}")
message(STATUS "CMAKE_BIN_DIR = ${CMAKE_BIN_DIR}")
message(STATUS "CMAKE_RUNTIME_OUTPUT_DIRECTORY = ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

if (UNIX)
    SET(INCLUDE_DIR "/usr/include"
            "/usr/local/include"
            "${CMAKE_ROOT_DIR}/thirdparty"
            )
elseif (WIN32)
    SET(INCLUDE_DIR "/usr/include"
            "/usr/local/include"
            "${CMAKE_ROOT_DIR}/thirdparty"
            )
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    if (UNIX)
        # CMAKE_BIN_DIR 在前：确保优先命中 BIN_DIR 里动态库
        SET(LIB_DIR "${CMAKE_BIN_DIR}"
                "/usr/lib"
        )
        SET(LIB_LINK_DEP resolv dl rt pthread)
        message(STATUS "库目录: ${LIB_DIR}")
    endif ()
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (UNIX)
        SET(LIB_DIR "${CMAKE_BIN_DIR}"
                "/usr/lib"
        )
        SET(LIB_LINK_DEP resolv dl rt pthread tirpc)
        message(STATUS "库目录: ${LIB_DIR}")
    endif ()
endif ()

include_directories(${INCLUDE_DIR})
link_directories(${LIB_DIR})

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    ADD_SUBDIRECTORY("thirdparty")
    ADD_SUBDIRECTORY("test")
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
    ADD_SUBDIRECTORY("thirdparty")
    ADD_SUBDIRECTORY("test")
endif ()

message(STATUS "=== 设置编译输出目录和编译选项结束 ===")
