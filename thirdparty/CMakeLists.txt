# 最小化的第三方库集成。
#
# 这里只把 header-only 库暴露为 INTERFACE target。rotor
# 仅保留源码：本项目的 actor 运行时基于独立版 asio
# 构建，不链接 rotor。

find_package(Threads REQUIRED)

add_library(libapp_asio INTERFACE)
target_include_directories(libapp_asio INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/asio/include")
target_compile_definitions(libapp_asio INTERFACE ASIO_STANDALONE)
target_link_libraries(libapp_asio INTERFACE Threads::Threads)

add_library(libapp_spdlog INTERFACE)
target_include_directories(libapp_spdlog INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/spdlog/include")
target_compile_definitions(libapp_spdlog INTERFACE SPDLOG_HEADER_ONLY)
