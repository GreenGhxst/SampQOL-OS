// Web Console server: wires the shared services to a versioned HTTP API and
// serves the static frontend. Networking is cpp-httplib (single header), TLS
// through OpenSSL when the build enables it.
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#ifdef DM_WITH_TLS
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#define CPPHTTPLIB_THREAD_POOL_COUNT 4
#include "../../third_party/httplib/httplib.h"
#include "../core/file_ops.h"
#include "../core/format_guard.h"
#include "../core/inbox.h"
#include "../core/intelligence.h"
#include "../core/job_queue.h"
#include "../core/journal.h"
#include "../core/screen.h"
#include "../core/transfer_manager.h"
#include "../core/volume_registry.h"
#include "auth.h"
#include "config.h"

namespace dm {

extern const char* kVersion;

class WebConsole {
public:
    WebConsole(Config cfg, std::unique_ptr<SystemProbe> probe, std::unique_ptr<BlockDeviceInventory> inventory,
               std::unique_ptr<FrameSource> screen);
    ~WebConsole();
    // Binds and starts serving on a background thread. Returns false on bind failure.
    bool start();
    void stop();
    int port() const { return port_; }
    bool tls() const { return tls_; }
    AuthService& auth() { return *auth_; }
    VolumeRegistry& volumes() { return *registry_; }
    json status_json();

private:
    struct Ctx {                      // per-request authenticated context
        Session session;
        bool trusted = false;
        std::string client;
    };
    void build_server();
    void register_core_routes();      // auth, session, volumes, files
    void register_extra_routes();     // transfers, jobs, search, collections, inbox, storage, screen, admin
    bool host_ok(const httplib::Request& req) const;
    bool transport_trusted(const httplib::Request& req) const;
    std::string cookie(const httplib::Request& req, const std::string& name) const;
    // Authenticates; writes an error response and returns false when refused.
    bool require(const httplib::Request& req, httplib::Response& res, Role need, bool mutating, Ctx& ctx);
    bool volume_or_error(const std::string& id, httplib::Response& res, VolumeInfo& out);
    bool rel_or_error(const std::string& p, httplib::Response& res, RelPath& out);
    json body_json(const httplib::Request& req, httplib::Response& res, bool& ok);
    void set_session_cookies(httplib::Response& res, const Session& s);
    void clear_session_cookies(httplib::Response& res);
    void refresh_volumes_throttled();
    void start_background();
    std::string submit_batch(const std::string& type, const Ctx& ctx, const json& items, const json& dest,
                             Conflict conflict, bool permanent, httplib::Response& res);

    Config cfg_;
    bool tls_ = false;
    int port_ = 0;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    std::thread housekeeping_;
    std::atomic<bool> stopping_{false};
    std::int64_t started_ms_ = 0;

    std::unique_ptr<Journal> journal_;
    std::unique_ptr<FileOps> ops_;
    std::unique_ptr<VolumeRegistry> registry_;
    std::unique_ptr<JobQueue> jobs_;
    std::unique_ptr<TransferManager> transfers_;
    std::unique_ptr<Intelligence> intel_;
    std::unique_ptr<Inbox> inbox_;
    std::unique_ptr<FormatGuard> format_;
    std::unique_ptr<AuthService> auth_;
    std::unique_ptr<FrameSource> screen_;
    std::mutex screen_mu_;
    std::map<std::string, std::int64_t> screen_last_frame_;   // session -> ts
    std::mutex refresh_mu_;
    std::int64_t last_refresh_ms_ = 0;
};

// HTTP helpers shared by the route files.
int http_status_for(Err e);
void send_json(httplib::Response& res, int code, const json& j);
void send_status(httplib::Response& res, const Status& st, const json& extra = json::object());
std::string content_disposition(const std::string& filename);
std::string mime_for(const std::string& name);

} // namespace dm
