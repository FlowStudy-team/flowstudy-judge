set(TEST_SOURCES
    test_runner.cpp
    integration_test.cpp
    ../src/config/config.cpp
    ../src/judge/status.cpp
    ../src/judge/sandbox.cpp
    ../src/judge/judge_engine.cpp
)

add_executable(test_runner ${TEST_SOURCES})

target_include_directories(test_runner PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(test_runner PRIVATE
    nlohmann_json::nlohmann_json
    ${SPDLOG_LIBRARIES}
)

target_compile_options(test_runner PRIVATE
    -Wall -Wextra
    ${SPDLOG_CFLAGS_OTHER}
)

target_compile_definitions(test_runner PRIVATE
    ${SPDLOG_CFLAGS_OTHER}
)

add_executable(test_publisher test_publisher.cpp)

target_link_libraries(test_publisher PRIVATE
    nlohmann_json::nlohmann_json
    ${RABBITMQ_LIBRARIES}
)

target_compile_options(test_publisher PRIVATE
    -Wall -Wextra
    -Wno-deprecated-declarations
    ${RABBITMQ_CFLAGS_OTHER}
)

add_executable(sandbox_test sandbox_test.cpp)
target_link_libraries(sandbox_test PRIVATE)
