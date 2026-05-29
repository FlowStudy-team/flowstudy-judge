set(SOURCES
    main.cpp
    config/config.cpp
    mq/rabbitmq_client.cpp
    db/mysql_client.cpp
    judge/status.cpp
    judge/sandbox.cpp
    judge/judge_engine.cpp
    worker/worker.cpp
)

add_executable(judge_worker ${SOURCES})

target_include_directories(judge_worker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(judge_worker PRIVATE
    nlohmann_json::nlohmann_json
    ${RABBITMQ_LIBRARIES}
    ${MYSQL_LIBRARIES}
    ${SPDLOG_LIBRARIES}
)

target_compile_options(judge_worker PRIVATE
    -Wall -Wextra -Wpedantic
    ${RABBITMQ_CFLAGS_OTHER}
    ${MYSQL_CFLAGS_OTHER}
    ${SPDLOG_CFLAGS_OTHER}
)

target_compile_definitions(judge_worker PRIVATE
    ${SPDLOG_CFLAGS_OTHER}
)
