#include "db.h"

#include <sqlite3.h>

#include <iostream>
#include <stdexcept>

struct RollcallDb::Impl
{
    std::string path;
    sqlite3 *db = nullptr;
    bool opened = false;
};

static void exec_sql(sqlite3 *db, const char *sql)
{
    char *errmsg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK)
    {
        std::string msg = errmsg ? errmsg : "sqlite error";
        sqlite3_free(errmsg);
        throw std::runtime_error(msg);
    }
}

RollcallDb::RollcallDb(std::string path)
{
    impl_ = new Impl();
    impl_->path = std::move(path);

    const int rc = sqlite3_open(impl_->path.c_str(), &impl_->db);
    if (rc != SQLITE_OK)
    {
        std::cerr << "[db] Failed to open: " << impl_->path << " rc=" << rc << "\n";
        return;
    }
    impl_->opened = true;

    try
    {
        ensure_schema();
    }
    catch (const std::exception &e)
    {
        std::cerr << "[db] Schema init failed: " << e.what() << "\n";
    }
}

RollcallDb::~RollcallDb()
{
    if (impl_)
    {
        if (impl_->db)
            sqlite3_close(impl_->db);
        delete impl_;
        impl_ = nullptr;
    }
}

bool RollcallDb::ok() const
{
    return impl_ && impl_->opened && impl_->db;
}

const std::string &RollcallDb::path() const
{
    return impl_->path;
}

void RollcallDb::ensure_schema()
{
    if (!ok())
        return;

    // Keep schema minimal and append-only safe.
    // Foreign keys are intentionally avoided to allow attendance even when roster is empty.
    exec_sql(impl_->db,
             "PRAGMA journal_mode=WAL;"
             "CREATE TABLE IF NOT EXISTS students("
             "  student_id TEXT PRIMARY KEY,"
             "  full_name  TEXT NOT NULL DEFAULT ''"
             ");"
             "CREATE TABLE IF NOT EXISTS sessions("
             "  session_id TEXT PRIMARY KEY,"
             "  started_at_ms INTEGER NOT NULL"
             ");"
             "CREATE TABLE IF NOT EXISTS attendance("
             "  session_id TEXT NOT NULL,"
             "  student_id TEXT NOT NULL,"
             "  at_ms INTEGER NOT NULL,"
             "  counter INTEGER NOT NULL,"
             "  fingerprint_status TEXT NOT NULL,"
             "  fingerprint_score INTEGER NOT NULL,"
             "  PRIMARY KEY(session_id, student_id)"
             ");"
             "CREATE INDEX IF NOT EXISTS idx_attendance_session ON attendance(session_id);"
             "CREATE INDEX IF NOT EXISTS idx_attendance_student ON attendance(student_id);");
}

int RollcallDb::upsert_student(const std::string &student_id, const std::string &full_name)
{
    if (!ok())
        return 0;

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO students(student_id, full_name) VALUES(?, ?) "
                      "ON CONFLICT(student_id) DO UPDATE SET full_name=excluded.full_name";

    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return 0;

    sqlite3_bind_text(stmt, 1, student_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, full_name.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

void RollcallDb::ensure_session(const std::string &session_id, std::int64_t started_at_ms)
{
    if (!ok())
        return;

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO sessions(session_id, started_at_ms) VALUES(?, ?) "
                      "ON CONFLICT(session_id) DO NOTHING";

    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, started_at_ms);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void RollcallDb::record_attendance(const std::string &session_id,
                                   const std::string &student_id,
                                   std::int64_t at_ms,
                                   std::int64_t counter,
                                   const std::string &fingerprint_status,
                                   int fingerprint_score)
{
    if (!ok())
        return;

    // Ensure a roster row exists even if not imported yet.
    upsert_student(student_id, "");

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "INSERT INTO attendance(session_id, student_id, at_ms, counter, fingerprint_status, fingerprint_score) "
                      "VALUES(?, ?, ?, ?, ?, ?) "
                      "ON CONFLICT(session_id, student_id) DO UPDATE SET at_ms=excluded.at_ms, counter=excluded.counter, "
                      "fingerprint_status=excluded.fingerprint_status, fingerprint_score=excluded.fingerprint_score";

    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, student_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, at_ms);
    sqlite3_bind_int64(stmt, 4, counter);
    sqlite3_bind_text(stmt, 5, fingerprint_status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, fingerprint_score);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<RosterRow> RollcallDb::roster_with_status(const std::string &session_id)
{
    std::vector<RosterRow> out;
    if (!ok())
        return out;

    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT s.student_id, s.full_name, COALESCE(a.at_ms, 0) AS at_ms "
        "FROM students s "
        "LEFT JOIN attendance a ON a.student_id=s.student_id AND a.session_id=? "
        "ORDER BY s.student_id";

    if (sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return out;

    sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);

    while (true)
    {
        const int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW)
        {
            RosterRow r;
            const unsigned char *sid = sqlite3_column_text(stmt, 0);
            const unsigned char *name = sqlite3_column_text(stmt, 1);
            r.student_id = sid ? reinterpret_cast<const char *>(sid) : "";
            r.full_name = name ? reinterpret_cast<const char *>(name) : "";
            r.at_ms = sqlite3_column_int64(stmt, 2);
            r.present = r.at_ms > 0;
            out.push_back(std::move(r));
            continue;
        }
        break;
    }

    sqlite3_finalize(stmt);
    return out;
}
