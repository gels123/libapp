project(testmain)

add_executable(testmain testmain.cpp)

target_include_directories(testmain PRIVATE "${CMAKE_SOURCE_DIR}/libapp/include")
target_link_libraries(testmain PRIVATE libapp_asio)
target_compile_features(testmain PRIVATE cxx_std_20)
