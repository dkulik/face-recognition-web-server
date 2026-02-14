cmake_minimum_required(VERSION 3.16)
project(basic_c_web_server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(
  web_server_core
  src/http.c
  src/static_assets.c
  src/router.c
)

target_include_directories(web_server_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(web_server_core PUBLIC WEB_ROOT_DIR="${CMAKE_SOURCE_DIR}/web")

add_executable(web_server src/main.c)
target_link_libraries(web_server PRIVATE web_server_core)
add_executable(load_test src/load_test.c)

find_package(Threads REQUIRED)
target_link_libraries(load_test PRIVATE Threads::Threads)

target_compile_options(web_server_core PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

target_compile_options(web_server PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

target_compile_options(load_test PRIVATE
  -Wall
  -Wextra
  -Wpedantic
)

include(CTest)
if(BUILD_TESTING)
  add_executable(test_http tests/test_http.c)
  target_link_libraries(test_http PRIVATE web_server_core)
  target_compile_options(test_http PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME test_http COMMAND test_http)

  add_executable(test_static_assets tests/test_static_assets.c)
  target_link_libraries(test_static_assets PRIVATE web_server_core)
  target_compile_options(test_static_assets PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME test_static_assets COMMAND test_static_assets)

  add_executable(test_router tests/test_router.c)
  target_link_libraries(test_router PRIVATE web_server_core)
  target_compile_options(test_router PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME test_router COMMAND test_router)
endif()
