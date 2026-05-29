CREATE DATABASE IF NOT EXISTS online_judge
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE online_judge;

CREATE TABLE IF NOT EXISTS submissions (
    submission_id   BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
    problem_id      BIGINT UNSIGNED NOT NULL,
    language        VARCHAR(20)     NOT NULL DEFAULT 'cpp',
    code            MEDIUMTEXT      NOT NULL,
    time_limit_ms   INT UNSIGNED    NOT NULL DEFAULT 1000,
    memory_limit_kb INT UNSIGNED    NOT NULL DEFAULT 262144,
    status          VARCHAR(30)     NOT NULL DEFAULT 'Pending',
    time_used_ms    INT UNSIGNED    NOT NULL DEFAULT 0,
    memory_used_kb  INT UNSIGNED    NOT NULL DEFAULT 0,
    error_message   TEXT,
    compiler_output TEXT,
    failed_testcase INT             NOT NULL DEFAULT -1,
    created_at      DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP,
    judged_at       DATETIME,

    INDEX idx_problem  (problem_id),
    INDEX idx_status   (status),
    INDEX idx_created  (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
