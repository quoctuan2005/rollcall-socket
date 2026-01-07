#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct RosterRow
{
    std::string student_id;
    std::string full_name;
    bool present = false;
    std::int64_t at_ms = 0;
};

class RollcallDb
{
public:
    explicit RollcallDb(std::string path);
    ~RollcallDb();

    RollcallDb(const RollcallDb &) = delete;
    RollcallDb &operator=(const RollcallDb &) = delete;

    bool ok() const;
    const std::string &path() const;

    void ensure_schema();

    // Roster
    int upsert_student(const std::string &student_id, const std::string &full_name);

    // Sessions + attendance
    void ensure_session(const std::string &session_id, std::int64_t started_at_ms);
    void record_attendance(const std::string &session_id,
                           const std::string &student_id,
                           std::int64_t at_ms,
                           std::int64_t counter,
                           const std::string &fingerprint_status,
                           int fingerprint_score);

    std::vector<RosterRow> roster_with_status(const std::string &session_id);

private:
    struct Impl;
    Impl *impl_;
};
