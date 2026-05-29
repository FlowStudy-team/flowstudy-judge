#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    std::string host = "127.0.0.1";
    int port = 5672;
    std::string vhost = "/";
    std::string user = "judge";
    std::string pass = "judge_pass";
    std::string queue = "submission_queue";

    // Connect
    amqp_connection_state_t conn = amqp_new_connection();
    amqp_socket_t* socket = amqp_tcp_socket_new(conn);
    amqp_socket_open(socket, host.c_str(), port);

    amqp_login(conn, vhost.c_str(), 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
               user.c_str(), pass.c_str());

    amqp_channel_open(conn, 1);
    amqp_get_rpc_reply(conn);

    // Build test messages
    nlohmann::json testcases = {
        {{"input", "1 2"}, {"expected_output", "3"}},
        {{"input", "5 7"}, {"expected_output", "12"}},
        {{"input", "-3 8"}, {"expected_output", "5"}}
    };

    struct TestConfig {
        int id;
        std::string file;
        int time_limit;
    };

    std::string base = "/home/dhc/FlowStudy-Judge/";

    TestConfig tests[] = {
        {1, base + "test/test_codes/ac.cpp", 1000},
        {2, base + "test/test_codes/wa.cpp", 1000},
        {3, base + "test/test_codes/re.cpp", 1000},
        {4, base + "test/test_codes/tle.cpp", 500},
        {5, base + "test/test_codes/ce.cpp", 1000},
    };

    for (const auto& tc : tests) {
        std::string code = read_file(tc.file);
        if (code.empty()) {
            std::cerr << "Failed to read " << tc.file << std::endl;
            continue;
        }

        nlohmann::json msg = {
            {"submission_id", tc.id},
            {"problem_id", 1},
            {"language", "cpp"},
            {"code", code},
            {"time_limit", tc.time_limit},
            {"memory_limit", 262144},
            {"testcases", testcases}
        };

        std::string body = msg.dump();
        amqp_basic_properties_t props;
        std::memset(&props, 0, sizeof(props));
        props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
        props.content_type = amqp_cstring_bytes("application/json");
        props.delivery_mode = 2; // persistent

        int status = amqp_basic_publish(
            conn, 1,
            amqp_empty_bytes,                     // exchange
            amqp_cstring_bytes(queue.c_str()),    // routing_key
            0, 0,
            &props,
            amqp_cstring_bytes(body.c_str()));

        if (status == AMQP_STATUS_OK) {
            std::cout << "Published submission_id=" << tc.id
                      << " (" << tc.file << ") time_limit=" << tc.time_limit
                      << "ms" << std::endl;
        } else {
            std::cerr << "Failed to publish " << tc.id << std::endl;
        }
    }

    amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS);
    amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn);

    std::cout << "\nPublished 5 test messages." << std::endl;
    return 0;
}
