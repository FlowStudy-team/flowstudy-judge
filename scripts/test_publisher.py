#!/usr/bin/env python3
"""
Test publisher for FlowStudy-Judge.
Sends 5 test submissions to RabbitMQ covering: AC, WA, RE, TLE, CE.

Usage: python3 scripts/test_publisher.py [config_path]
Default config_path: config.json
"""

import json
import sys
import time
import os

try:
    import pika
except ImportError:
    print("Please install pika: pip install pika")
    sys.exit(1)

try:
    import mysql.connector
except ImportError:
    print("Please install mysql-connector-python: pip install mysql-connector-python")
    sys.exit(1)


def load_config(path):
    with open(path) as f:
        return json.load(f)


def read_test_code(filename):
    path = os.path.join(os.path.dirname(__file__), "..", "test", "test_codes", filename)
    with open(path) as f:
        return f.read()


def setup_database(mysql_cfg):
    """Insert pending submissions into the database."""
    conn = mysql.connector.connect(
        host=mysql_cfg["hostname"],
        port=mysql_cfg["port"],
        user=mysql_cfg["username"],
        password=mysql_cfg["password"],
        database=mysql_cfg["database"]
    )
    cursor = conn.cursor()

    # The submission_ids we will use
    insert_sql = """
        INSERT INTO submissions
            (submission_id, problem_id, language, code, time_limit_ms, memory_limit_kb, status)
        VALUES (%s, %s, 'cpp', '', %s, %s, 'Pending')
        ON DUPLICATE KEY UPDATE status='Pending', judged_at=NULL
    """

    configs = [
        (1, 1, 1000, 262144),   # AC
        (2, 1, 1000, 262144),   # WA
        (3, 1, 1000, 262144),   # RE
        (4, 1, 500, 262144),    # TLE (shorter time limit)
        (5, 1, 1000, 262144),   # CE
    ]

    for sub_id, prob_id, time_lim, mem_lim in configs:
        cursor.execute(insert_sql, (sub_id, prob_id, time_lim, mem_lim))

    conn.commit()
    cursor.close()
    conn.close()
    print("Database prepared: 5 pending submissions inserted.")


def check_results(mysql_cfg):
    """Query and display results from the database."""
    conn = mysql.connector.connect(
        host=mysql_cfg["hostname"],
        port=mysql_cfg["port"],
        user=mysql_cfg["username"],
        password=mysql_cfg["password"],
        database=mysql_cfg["database"]
    )
    cursor = conn.cursor(dictionary=True)

    cursor.execute("""
        SELECT submission_id, status, time_used_ms, memory_used_kb, error_message, compiler_output
        FROM submissions
        WHERE submission_id BETWEEN 1 AND 5
        ORDER BY submission_id
    """)

    print("\n" + "=" * 70)
    print("TEST RESULTS")
    print("=" * 70)

    expected = {
        1: "Accepted",
        2: "WrongAnswer",
        3: "RuntimeError",
        4: "TimeLimitExceeded",
        5: "CompilationError",
    }

    all_pass = True
    for row in cursor:
        sub_id = row["submission_id"]
        status = row["status"]
        exp = expected.get(sub_id, "?")
        passed = (status == exp)
        if not passed:
            all_pass = False
        flag = "PASS" if passed else "FAIL"
        print(f"[{flag}] submission_id={sub_id}: expected={exp:20s} got={status}")
        if not passed and row["error_message"]:
            print(f"     error: {row['error_message'][:100]}")
        if not passed and row["compiler_output"]:
            print(f"     compiler: {row['compiler_output'][:100]}")

    print("=" * 70)
    if all_pass:
        print("All tests PASSED!")
    else:
        print("Some tests FAILED!")

    cursor.close()
    conn.close()
    return all_pass


def main():
    config_path = sys.argv[1] if len(sys.argv) > 1 else "config.json"
    cfg = load_config(config_path)
    mq_cfg = cfg["rabbitmq"]
    mysql_cfg = cfg["mysql"]

    # Setup database records
    setup_database(mysql_cfg)

    # Connect to RabbitMQ
    credentials = pika.PlainCredentials(mq_cfg["username"], mq_cfg["password"])
    connection = pika.BlockingConnection(
        pika.ConnectionParameters(
            host=mq_cfg["hostname"],
            port=mq_cfg["port"],
            virtual_host=mq_cfg["vhost"],
            credentials=credentials
        )
    )
    channel = connection.channel()
    channel.queue_declare(queue=mq_cfg["queue_name"], durable=True)

    # Test cases: (submission_id, code_file, time_limit, expected)
    tests = [
        (1, "ac.cpp",  1000),
        (2, "wa.cpp",  1000),
        (3, "re.cpp",  1000),
        (4, "tle.cpp", 500),
        (5, "ce.cpp",  1000),
    ]

    testcases = [
        {"input": "1 2", "expected_output": "3"},
        {"input": "5 7", "expected_output": "12"},
        {"input": "-3 8", "expected_output": "5"},
    ]

    for sub_id, code_file, time_lim in tests:
        code = read_test_code(code_file)
        message = {
            "submission_id": sub_id,
            "problem_id": 1,
            "language": "cpp",
            "code": code,
            "time_limit": time_lim,
            "memory_limit": 262144,
            "testcases": testcases
        }

        body = json.dumps(message)
        channel.basic_publish(
            exchange="",
            routing_key=mq_cfg["queue_name"],
            body=body,
            properties=pika.BasicProperties(
                delivery_mode=2,  # persistent
            )
        )
        print(f"Published submission_id={sub_id} ({code_file}) time_limit={time_lim}ms")

    connection.close()
    print(f"\nPublished 5 test messages. Waiting for worker to process...")

    # Wait and poll for results
    max_wait = 60
    start = time.time()
    while time.time() - start < max_wait:
        time.sleep(3)
        conn = mysql.connector.connect(
            host=mysql_cfg["hostname"],
            port=mysql_cfg["port"],
            user=mysql_cfg["username"],
            password=mysql_cfg["password"],
            database=mysql_cfg["database"]
        )
        cursor = conn.cursor()
        cursor.execute(
            "SELECT COUNT(*) FROM submissions "
            "WHERE submission_id BETWEEN 1 AND 5 AND status != 'Pending'"
        )
        done = cursor.fetchone()[0]
        cursor.close()
        conn.close()

        print(f"  Processed: {done}/5")
        if done >= 5:
            break

    check_results(mysql_cfg)


if __name__ == "__main__":
    main()
