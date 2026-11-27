#include <fcntl.h>
#include <unistd.h>
#include "../core/network.h"
#include "web_console.h"

namespace dm {

void WebConsole::register_extra_routes() {
    auto& S = *server_;

    // ------------------------------------------------------------ transfers (chunked, resumable uploads)
    S.Post("/api/v1/transfers/upload", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        bool inbox = b.value("inbox", false);
        std::string vol = b.value("volume", ""), dir = b.value("dir", "");
        if (!inbox) {
            VolumeInfo v; RelPath d;
            if (!volume_or_error(vol, res, v) || !rel_or_error(dir, res, d)) return;
            Status st = PathAuthorizer::check_write(v, d.child("x")); if (!st.ok()) { send_status(res, st); return; }
            if (!b.contains("size") || !b["size"].is_number_unsigned()) { send_status(res, Status::fail(Err::invalid_argument, "size required")); return; }
            if (b["size"].get<std::uint64_t>() > ops_->usable_free(v)) { send_status(res, Status::fail(Err::no_space, "not enough free space on " + v.name)); return; }
        } else if (!b.contains("size") || !b["size"].is_number_unsigned()) { send_status(res, Status::fail(Err::invalid_argument, "size required")); return; }
        std::string id;
        Status st = transfers_->begin(c.session.id, vol, dir, b.value("name", ""), b["size"].get<std::uint64_t>(), b.value("sha256", ""), inbox, id);
        if (!st.ok()) { send_status(res, st); return; }
        send_json(res, 201, json{{"ok", true}, {"id", id}, {"chunk_max", cfg_.upload_chunk_max}});
    });

    S.Put("/api/v1/transfers/upload/:id", [this](const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        std::string id = req.path_params.at("id");
        std::uint64_t offset = std::strtoull(req.get_param_value("offset").c_str(), nullptr, 10);
        std::string body; body.reserve(1 << 20);
        bool too_big = false;
        reader([&](const char* data, std::size_t len) {
            if (body.size() + len > cfg_.upload_chunk_max) { too_big = true; return false; }
            body.append(data, len); return true;
        });
        if (too_big) { send_status(res, Status::fail(Err::invalid_argument, "chunk larger than chunk_max")); return; }
        Status st = transfers_->append(id, c.session.id, offset, body.data(), body.size());
        if (!st.ok()) { send_status(res, st, json{{"upload", transfers_->get(id, c.session.id)}}); return; }
        send_json(res, 200, json{{"ok", true}, {"upload", transfers_->get(id, c.session.id)}});
    });

    S.Get("/api/v1/transfers/upload/:id", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        json u = transfers_->get(req.path_params.at("id"), c.session.id);
        if (u.is_null()) { send_status(res, Status::fail(Err::not_found, "upload not found")); return; }
        send_json(res, 200, json{{"ok", true}, {"upload", u}});
    });

    S.Post("/api/v1/transfers/upload/:id/commit", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        std::string id = req.path_params.at("id");
        json u = transfers_->get(id, c.session.id);
        if (u.is_null()) { send_status(res, Status::fail(Err::not_found, "upload not found")); return; }
        std::string staged, sha;
        Status st = u.value("state", "") == "complete" ? transfers_->completed(id, c.session.id, staged, sha)
                                                        : transfers_->finish(id, c.session.id, staged, sha);
        if (!st.ok()) { send_status(res, st); return; }
        if (u.value("inbox", false)) {
            std::string item;
            st = inbox_->add(staged, u.value("name", ""), sha, item);
            if (st.ok()) transfers_->forget(id);
            send_status(res, st, json{{"inbox_item", item}, {"sha256", sha}});
            return;
        }
        VolumeInfo v; RelPath d;
        if (!volume_or_error(u.value("volume", ""), res, v) || !rel_or_error(u.value("dir", ""), res, d)) return;
        std::string fname; OpContext ctx;
        ctx.revoked = [this, id=v.id]() {
            try {
                registry_->refresh();
                auto current = registry_->get(id);
                return !current || !current->mounted || current->ownership != Ownership::device;
            } catch (...) { return true; }
        };
        st = ops_->place_file(staged, v, d, u.value("name", ""), conflict_from_string(b.value("conflict", "fail")), ctx, &fname);
        if (st.ok()) { transfers_->forget(id); intel_->invalidate(v.id, d.child(fname).str()); }
        send_status(res, st, json{{"path", st.ok() ? d.child(fname).str() : ""}, {"sha256", sha}, {"upload", transfers_->get(id, c.session.id)}});
    });

    S.Delete("/api/v1/transfers/upload/:id", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        send_status(res, transfers_->abort(req.path_params.at("id"), c.session.id));
    });

    S.Get("/api/v1/transfers", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        send_json(res, 200, json{{"ok", true}, {"uploads", transfers_->list(c.session.id)}, {"chunk_max", cfg_.upload_chunk_max}, {"max_upload_bytes", cfg_.max_upload_bytes}});
    });

    // ------------------------------------------------------------ jobs
    S.Get("/api/v1/jobs", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        send_json(res, 200, json{{"ok", true}, {"jobs", jobs_->list(c.session.id, c.session.role == Role::admin && req.get_param_value("all") == "1")}});
    });
    S.Get("/api/v1/jobs/:id", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        json j = jobs_->get(req.path_params.at("id"));
        if (j.is_null()) { send_status(res, Status::fail(Err::not_found, "job not found")); return; }
        j["ok"] = true; send_json(res, 200, j);
    });
    S.Post("/api/v1/jobs/:id/cancel", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        bool done = jobs_->cancel(req.path_params.at("id"), c.session.id, c.session.role == Role::admin);
        send_status(res, done ? Status::good() : Status::fail(Err::not_found, "job not found or already finished"));
    });

    // ------------------------------------------------------------ search / file intelligence
    S.Get("/api/v1/search", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        SearchQuery q;
        q.volume = req.get_param_value("volume"); q.text = req.get_param_value("q").substr(0, 100); q.kind = req.get_param_value("kind");
        q.ext = req.get_param_value("ext"); q.sort = req.has_param("sort") ? req.get_param_value("sort") : "name"; q.descending = req.get_param_value("desc") == "1";
        q.min_size = std::strtoull(req.get_param_value("min").c_str(), nullptr, 10); q.max_size = std::strtoull(req.get_param_value("max").c_str(), nullptr, 10);
        q.offset = std::strtoul(req.get_param_value("offset").c_str(), nullptr, 10);
        q.limit = req.has_param("limit") ? std::strtoul(req.get_param_value("limit").c_str(), nullptr, 10) : 200;
        json r = intel_->search(q); r["ok"] = true; send_json(res, 200, r);
    });
    S.Get("/api/v1/search/status", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        send_json(res, 200, json{{"ok", true}, {"indexes", intel_->index_status()}});
    });
    S.Post("/api/v1/search/index", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        VolumeInfo v; if (!volume_or_error(b.value("volume", ""), res, v)) return;
        Status st = PathAuthorizer::check_read(v); if (!st.ok()) { send_status(res, st); return; }
        std::string id;
        st = jobs_->submit("index", c.session.id, [this, vid = v.id](Job& j) {
            std::size_t n = 0; Status s = intel_->scan(vid, &j.cancel, n);
            j.detail = json{{"volume", vid}, {"indexed", n}};
            if (!s.ok()) { j.state = s.code == Err::cancelled ? "cancelled" : "failed"; j.error = s.message; }
        }, id, {JobItem{v.id + ":", "", "queued", ""}});
        if (!st.ok()) { send_status(res, st); return; }
        send_json(res, 202, json{{"ok", true}, {"job", id}});
    });
    S.Post("/api/v1/search/duplicates", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        VolumeInfo v; if (!volume_or_error(b.value("volume", ""), res, v)) return;
        std::string id;
        Status st = jobs_->submit("duplicates", c.session.id, [this, vid = v.id](Job& j) {
            j.detail = intel_->duplicates(vid, &j.cancel);
            if (j.detail.contains("error")) { j.state = "failed"; j.error = j.detail["error"]; }
        }, id, {JobItem{v.id + ":", "", "queued", ""}});
        if (!st.ok()) { send_status(res, st); return; }
        send_json(res, 202, json{{"ok", true}, {"job", id}});
    });

    S.Get("/api/v1/collections", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        send_json(res, 200, json{{"ok", true}, {"collections", intel_->collections()}});
    });
    S.Post("/api/v1/collections", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        std::string id; Status st = intel_->collection_create(b.value("name", ""), id);
        send_status(res, st, json{{"id", id}});
    });
    S.Post("/api/v1/collections/:id/rename", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        send_status(res, intel_->collection_rename(req.path_params.at("id"), b.value("name", "")));
    });
    S.Delete("/api/v1/collections/:id", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        send_status(res, intel_->collection_delete(req.path_params.at("id")));
    });
    S.Post("/api/v1/collections/:id/items", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        send_status(res, intel_->collection_add(req.path_params.at("id"), b.value("volume", ""), b.value("path", "")));
    });
    S.Post("/api/v1/collections/:id/remove", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        send_status(res, intel_->collection_remove(req.path_params.at("id"), b.value("volume", ""), b.value("path", "")));
    });
    S.Get("/api/v1/favorites", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        send_json(res, 200, json{{"ok", true}, {"favorites", intel_->favorites()}});
    });
    S.Post("/api/v1/favorites", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        send_status(res, intel_->favorite(b.value("volume", ""), b.value("path", ""), b.value("on", true)));
    });
    S.Get("/api/v1/recents", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        send_json(res, 200, json{{"ok", true}, {"recents", intel_->recents()}});
    });
    S.Get("/api/v1/tags", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        if (req.has_param("volume")) send_json(res, 200, json{{"ok", true}, {"tags", intel_->tags(req.get_param_value("volume"), req.get_param_value("path"))}});
        else send_json(res, 200, json{{"ok", true}, {"all", intel_->all_tags()}});
    });
    S.Post("/api/v1/tags", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        send_status(res, intel_->set_tags(b.value("volume", ""), b.value("path", ""), b.value("tags", std::vector<std::string>{})));
    });

    // ------------------------------------------------------------ inbox
    S.Get("/api/v1/inbox", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        send_json(res, 200, json{{"ok", true}, {"items", inbox_->list()}});
    });
    S.Get("/api/v1/inbox/:id", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        json it = inbox_->get(req.path_params.at("id"));
        if (it.is_null()) { send_status(res, Status::fail(Err::not_found, "inbox item not found")); return; }
        it["ok"] = true; send_json(res, 200, it);
    });
    S.Get("/api/v1/inbox/:id/peaks", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        std::vector<float> peaks;
        std::size_t buckets = req.has_param("buckets") ? std::strtoul(req.get_param_value("buckets").c_str(), nullptr, 10) : 400;
        Status st = inbox_->peaks(req.path_params.at("id"), buckets, peaks);
        if (!st.ok()) { send_status(res, st); return; }
        send_json(res, 200, json{{"ok", true}, {"peaks", peaks}});
    });
    S.Get("/api/v1/inbox/:id/audio", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        auto fd = std::make_shared<ScopedFd>(); std::uint64_t size = 0; std::string name;
        Status st = inbox_->open(req.path_params.at("id"), *fd, size, name);
        if (!st.ok()) { send_status(res, st); return; }
        res.set_header("Accept-Ranges", "bytes");
        res.set_content_provider(size, mime_for(name), [fd](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
            char buf[65536]; ssize_t r = ::pread(fd->get(), buf, std::min(length, sizeof buf), off_t(offset));
            return r > 0 && sink.write(buf, std::size_t(r));
        });
    });
    S.Post("/api/v1/inbox/:id/rename", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        send_status(res, inbox_->rename(req.path_params.at("id"), b.value("name", "")));
    });
    S.Post("/api/v1/inbox/:id/commit", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        VolumeInfo v; RelPath d;
        if (!volume_or_error(b.value("volume", ""), res, v) || !rel_or_error(b.value("path", ""), res, d)) return;
        std::string final_path;
        Status st = inbox_->commit(req.path_params.at("id"), v, d, conflict_from_string(b.value("conflict", "fail")), final_path);
        if (st.ok()) intel_->invalidate(v.id, final_path);
        send_status(res, st, json{{"path", final_path}, {"volume", v.id}});
    });
    S.Delete("/api/v1/inbox/:id", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        send_status(res, inbox_->discard(req.path_params.at("id")));
    });

    // ------------------------------------------------------------ storage dashboard
    S.Get("/api/v1/storage/status", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        refresh_volumes_throttled();
        json vols = json::array(); json warnings = json::array();
        for (const auto& v : registry_->list()) {
            json j = v.to_json();
            j["counts"] = intel_->dashboard_counts(v.id);
            if (v.mounted && v.capacity && v.free * 100 / v.capacity < 5) warnings.push_back(json{{"volume", v.id}, {"level", "warn"}, {"message", v.name + " has less than 5% free space"}});
            if (v.ownership == Ownership::usb_host) warnings.push_back(json{{"volume", v.id}, {"level", "info"}, {"message", v.name + " is exported to the USB host (SD Card Access); file operations are blocked"}});
            if (v.fs_read_only) warnings.push_back(json{{"volume", v.id}, {"level", "info"}, {"message", v.name + " is mounted read-only"}});
            vols.push_back(j);
        }
        json errors = json::array();
        for (const auto& e : journal_->recent(100)) if (e.state == "failed" || e.state == "interrupted" || e.state == "partial") { errors.push_back(e.to_json()); if (errors.size() >= 20) break; }
        send_json(res, 200, json{{"ok", true}, {"volumes", vols}, {"warnings", warnings}, {"recent_errors", errors},
                                 {"jobs", jobs_->list(c.session.id, false)}, {"uploads", transfers_->list(c.session.id)},
                                 {"media_health", json{{"supported", false}, {"note", "No verified device interface for flash media health; only filesystem capacity is reported."}}},
                                 {"server", status_json()}});
    });
    S.Get("/api/v1/storage/network", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        json n = network_status_json(cfg_.bind_host, port_, tls_); n["ok"] = true; n["client"] = req.remote_addr;
        n["pairing_hint"] = "Connect the MPC Sample over USB-C, open one of the URLs below on the connected computer or tablet and enter the pairing code shown by the device.";
        send_json(res, 200, n);
    });
    S.Get("/api/v1/storage/journal", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        json arr = json::array(); for (const auto& e : journal_->recent(200)) arr.push_back(e.to_json());
        send_json(res, 200, json{{"ok", true}, {"entries", arr}});
    });
    S.Get("/api/v1/storage/usage", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        VolumeInfo v; RelPath p;
        if (!volume_or_error(req.get_param_value("volume"), res, v) || !rel_or_error(req.get_param_value("path"), res, p)) return;
        std::uint64_t files = 0, dirs = 0, bytes = 0;
        Status st = ops_->usage(v, p, files, dirs, bytes);
        send_status(res, st, json{{"files", files}, {"folders", dirs}, {"bytes", bytes}});
    });

    // ------------------------------------------------------------ screen bridge
    S.Get("/api/v1/screen/status", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        json j{{"ok", true}, {"width", kScreenW}, {"height", kScreenH}, {"max_fps", cfg_.screen_max_fps}, {"format", "rgb565le"},
               {"remote_input", json{{"enabled", false}, {"reason", "native input integration is not verified; remote control stays disabled"}}}};
        if (screen_) { j["source"] = screen_->name(); j["available"] = screen_->available(); j["simulated"] = screen_->simulated(); j["reason"] = screen_->unavailable_reason(); }
        else { j["source"] = "none"; j["available"] = false; j["simulated"] = false; j["reason"] = "no frame source configured"; }
        send_json(res, 200, j);
    });
    S.Get("/api/v1/screen/frame", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::viewer, false, c)) return;
        if (!screen_ || !screen_->available()) { send_status(res, Status::fail(Err::unavailable, screen_ ? screen_->unavailable_reason() : "no frame source")); return; }
        Frame f;
        {
            std::lock_guard<std::mutex> lk(screen_mu_);
            std::int64_t now = now_ms(), min_gap = 1000 / std::max(1, cfg_.screen_max_fps);
            auto& last = screen_last_frame_[c.session.id];
            if (now - last < min_gap) { res.set_header("Retry-After", "1"); send_json(res, 429, json{{"ok", false}, {"error", "busy"}, {"message", "frame rate limit"}}); return; }
            last = now;
            Status st = screen_->capture(f);
            if (!st.ok()) { send_status(res, st); return; }
        }
        Status st = validate_frame(f);
        if (!st.ok()) { send_status(res, st); return; }
        res.set_header("X-DM-Frame-Seq", std::to_string(f.seq));
        res.set_header("X-DM-Frame-Source", f.source);
        res.set_header("X-DM-Frame-Simulated", f.simulated ? "1" : "0");
        res.set_header("X-DM-Frame-Size", std::to_string(f.width) + "x" + std::to_string(f.height));
        res.set_content(std::string(reinterpret_cast<const char*>(f.rgb565.data()), f.rgb565.size()), "application/octet-stream");
    });
    S.Post("/api/v1/screen/input", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::file_manager, true, c)) return;
        send_json(res, 501, json{{"ok", false}, {"error", "unsupported"}, {"message", "remote control is disabled until the native command-handling integration is verified on hardware"}});
    });

    // ------------------------------------------------------------ admin
    S.Get("/api/v1/admin/status", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::admin, false, c)) return;
        json j = status_json(); j["ok"] = true; j["config"] = cfg_.to_json(); j["jobs"] = jobs_->list("", true);
        send_json(res, 200, j);
    });
    S.Get("/api/v1/admin/format/devices", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::admin, false, c)) return;
        send_json(res, 200, json{{"ok", true}, {"devices", format_->devices()}, {"simulation", format_->simulation()}});
    });
    S.Post("/api/v1/admin/format/preflight", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::admin, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        std::string vol = b.value("volume", "");
        if (!VolumeRegistry::valid_id(vol)) { send_status(res, Status::fail(Err::invalid_argument, "bad volume id")); return; }
        json r = format_->preflight(vol, c.session.id); r["ok"] = true;
        log_warn("format preflight for " + vol + " by " + c.client + ": " + (r["eligible"].get<bool>() ? "eligible" : "refused"));
        send_json(res, 200, r);
    });
    S.Post("/api/v1/admin/format/confirm", [this](const httplib::Request& req, httplib::Response& res) {
        Ctx c; if (!require(req, res, Role::admin, true, c)) return;
        bool ok; json b = body_json(req, res, ok); if (!ok) return;
        if (!b.value("confirm_erase", false)) { send_status(res, Status::fail(Err::invalid_argument, "confirm_erase must be true")); return; }
        json r = format_->confirm(b.value("token", ""), b.value("volume", ""), b.value("device", ""), b.value("fs_type", "exfat"), b.value("label", ""), c.session.id);
        log_warn("format confirm for " + b.value("volume", "") + " by " + c.client + ": " + (r.value("ok", false) ? "executed (simulation)" : "refused: " + r.value("message", "")));
        send_json(res, r.value("ok", false) ? 200 : 403, r);
    });
}

} // namespace dm
