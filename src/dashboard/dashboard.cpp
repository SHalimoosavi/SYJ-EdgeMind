#include "dashboard/dashboard.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include <httplib.h>

#include "dashboard/dashboard_static.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace syj::edgemind::dashboard {
namespace {

constexpr size_t kMaxRequestBody = 16 * 1024;

std::string json_escape(const std::string& input) {
    std::ostringstream out;
    for (unsigned char c : input) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
                        << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::string json_string(const std::string& s) { return "\"" + json_escape(s) + "\""; }

bool parse_json_string(const std::string& body, const std::string& key, std::string* out) {
    const std::string needle = "\"" + key + "\"";
    const size_t key_pos = body.find(needle);
    if (key_pos == std::string::npos) return false;
    size_t p = body.find(':', key_pos + needle.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p]))) ++p;
    if (p >= body.size() || body[p] != '"') return false;
    ++p;
    std::string value;
    value.reserve(64);
    while (p < body.size()) {
        const char c = body[p++];
        if (c == '"') { *out = value; return true; }
        if (c != '\\') { value.push_back(c); continue; }
        if (p >= body.size()) return false;
        const char e = body[p++];
        switch (e) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default: return false; // keep the parser deliberately bounded and strict
        }
        if (value.size() > kMaxRequestBody) return false;
    }
    return false;
}

bool parse_json_int(const std::string& body, const std::string& key, int64_t* out) {
    const std::string needle = "\"" + key + "\"";
    const size_t key_pos = body.find(needle);
    if (key_pos == std::string::npos) return false;
    size_t p = body.find(':', key_pos + needle.size());
    if (p == std::string::npos) return false;
    ++p;
    while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p]))) ++p;
    if (p >= body.size()) return false;
    const char* begin = body.c_str() + p;
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(begin, &end, 10);
    if (end == begin || errno == ERANGE) return false;
    if (value < 0 || value > std::numeric_limits<int32_t>::max()) return false;
    *out = static_cast<int64_t>(value);
    return true;
}

std::string report_from(size_t (*fn)(const syj_edgemind_runtime*, char*, size_t), const syj_edgemind_runtime* rt) {
    if (!rt) return {};
    const size_t n = fn(rt, nullptr, 0);
    if (n == 0) return {};
    std::string out(n + 1, '\0');
    fn(rt, out.data(), out.size());
    out.resize(std::strlen(out.c_str()));
    return out;
}

std::string registry_report(const std::string& path) {
    const size_t n = syj_edgemind_list_models(path.c_str(), nullptr, 0);
    if (n == 0) return {};
    std::string out(n + 1, '\0');
    syj_edgemind_list_models(path.c_str(), out.data(), out.size());
    return std::string(out.c_str());
}

// The C API intentionally exposes the registry as a stable text report. The
// dashboard turns each line into a presentation object without introducing a
// JSON dependency into the core. The parser is strict enough to ignore lines
// that do not match the documented registry listing shape.
std::string models_as_json(const std::string& text) {
    std::ostringstream out;
    out << "[";
    bool first = true;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream row(line);
        std::string id, name, rest;
        if (!(row >> id)) continue;
        const size_t sep = line.find("  ");
        if (sep == std::string::npos) continue;
        const size_t bracket = line.find("  [", sep + 2);
        if (bracket == std::string::npos) continue;
        name = line.substr(sep + 2, bracket - (sep + 2));
        const size_t close = line.rfind("]  ");
        if (close == std::string::npos || close <= bracket + 3) continue;
        const std::string attrs = line.substr(bracket + 3, close - (bracket + 3));
        const size_t mid = attrs.find("  ");
        if (mid == std::string::npos) continue;
        const std::string arch = attrs.substr(0, mid);
        const std::string quant = attrs.substr(mid + 2);
        const std::string verification = line.substr(close + 3);
        if (!first) out << ",";
        first = false;
        out << "{\"model_id\":" << json_string(id) << ",\"name\":" << json_string(name)
            << ",\"architecture\":" << json_string(arch) << ",\"quantization\":" << json_string(quant)
            << ",\"verification\":" << json_string(verification) << "}";
    }
    out << "]";
    return out.str();
}

std::string status_name(syj_edgemind_status status) {
    return syj_edgemind_status_message(status);
}

} // namespace

bool generate_dashboard_token(std::string* out_token) {
    if (!out_token) return false;
    std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return false;
    }
#else
    std::ifstream in("/dev/urandom", std::ios::binary);
    if (!in.is_open()) return false;
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (in.gcount() != static_cast<std::streamsize>(bytes.size())) return false;
#endif
    static constexpr char hex[] = "0123456789abcdef";
    out_token->clear();
    out_token->reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        out_token->push_back(hex[b >> 4]);
        out_token->push_back(hex[b & 0x0f]);
    }
    return true;
}

DashboardServer::DashboardServer(DashboardConfig config) : config_(std::move(config)) {
    state_.registry_path = config_.model_registry_path.empty() ? ".syj_edgemind_model_registry" : config_.model_registry_path;
    // Keep dashboard-managed usage state alongside its model registry. This
    // prevents a custom dashboard registry from accidentally sharing or
    // mutating the process-wide default usage-state file.
    state_.usage_state_path = state_.registry_path + ".usage_state";
}

DashboardServer::~DashboardServer() {
    stop();
}

bool DashboardServer::authenticate(const std::string& authorization) const {
    const std::string expected = "Bearer " + config_.token;
    if (config_.token.empty() || authorization.size() != expected.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        diff = static_cast<unsigned char>(diff | static_cast<unsigned char>(authorization[i] ^ expected[i]));
    }
    return diff == 0;
}

void DashboardServer::install_routes() {
    auto* server = static_cast<httplib::Server*>(server_);
    server->set_payload_max_length(kMaxRequestBody);

    auto auth_guard = [this](const httplib::Request& req, httplib::Response& res) {
        if (!authenticate(req.get_header_value("Authorization"))) {
            res.status = 401;
            res.set_header("WWW-Authenticate", "Bearer");
            res.set_content("{\"error\":\"unauthorized\"}", "application/json");
            return false;
        }
        return true;
    };
    server->set_pre_routing_handler([auth_guard](const httplib::Request& req, httplib::Response& res) {
        if (req.path == "/" || req.path == "/index.html") return httplib::Server::HandlerResponse::Unhandled;
        return auth_guard(req, res) ? httplib::Server::HandlerResponse::Unhandled : httplib::Server::HandlerResponse::Handled;
    });

    server->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kDashboardHtml, "text/html; charset=UTF-8");
        res.set_header("Cache-Control", "no-store");
    });
    server->Get("/index.html", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kDashboardHtml, "text/html; charset=UTF-8");
        res.set_header("Cache-Control", "no-store");
    });

    server->Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(health_json(), "application/json");
    });
    server->Get("/api/version", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(version_json(), "application/json");
    });
    server->Get("/api/system", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(system_json(), "application/json");
    });
    server->Get("/api/models", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(models_json(), "application/json");
    });
    server->Get("/api/runtime", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(runtime_json(), "application/json");
    });
    server->Get("/api/memory", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(memory_json(), "application/json");
    });
    server->Get("/api/usage", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(usage_json(), "application/json");
    });

    server->Post("/api/runtime/load", [this](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(operation_mutex_);
        std::string error;
        if (!load_from_request(req.body, &error)) {
            res.status = 400;
            res.set_content(error, "application/json");
            return;
        }
        res.set_content("{\"ok\":true,\"status\":\"" + json_escape(status_name(last_status_)) + "\"}", "application/json");
    });

    server->Post("/api/runtime/unload", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(operation_mutex_);
        unload_current();
        res.set_content("{\"ok\":true}", "application/json");
    });

    server->Post("/api/runtime/reload", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(operation_mutex_);
        std::string error;
        if (!reload_current(&error)) {
            res.status = 409;
            res.set_content(error, "application/json");
            return;
        }
        res.set_content("{\"ok\":true}", "application/json");
    });

    server->Post("/api/runtime/reset", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(operation_mutex_);
        if (!runtime_) {
            res.status = 409;
            res.set_content("{\"error\":\"runtime is not loaded\"}", "application/json");
            return;
        }
        syj_edgemind_reset(runtime_);
        res.set_content("{\"ok\":true}", "application/json");
    });

    server->Post("/api/generate", [this](const httplib::Request& req, httplib::Response& res) {
        if (req.body.size() > kMaxRequestBody) {
            res.status = 413;
            res.set_content("{\"error\":\"request body too large\"}", "application/json");
            return;
        }
        std::string prompt;
        if (!parse_json_string(req.body, "prompt", &prompt) || prompt.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"prompt must be a non-empty JSON string\"}", "application/json");
            return;
        }
        res.set_chunked_content_provider("text/event-stream; charset=utf-8",
            [this, prompt](size_t, httplib::DataSink& sink) {
                std::lock_guard<std::mutex> lock(operation_mutex_);
                struct StreamContext { httplib::DataSink* sink; bool writable = true; } ctx{&sink, true};
                if (!runtime_) {
                    const std::string event = "data: {\"done\":true,\"status\":" + json_string("Runtime is not loaded") + "}\n\n";
                    sink.write(event.data(), event.size());
                    sink.done();
                    return false;
                }
                auto callback = [](const char* piece, void* opaque) -> int {
                    auto* c = static_cast<StreamContext*>(opaque);
                    if (!c->sink->is_writable()) { c->writable = false; return 0; }
                    const std::string event = "data: {\"token\":" + json_string(piece ? piece : "") + "}\n\n";
                    c->sink->write(event.data(), event.size());
                    return c->sink->is_writable() ? 1 : 0;
                };
                const syj_edgemind_status status = syj_edgemind_generate(runtime_, prompt.c_str(), callback, &ctx);
                const std::string event = "data: {\"done\":true,\"status\":" + json_string(status_name(status)) + "}\n\n";
                if (ctx.writable && sink.is_writable()) sink.write(event.data(), event.size());
                sink.done();
                return true;
            });
    });
}

bool DashboardServer::start() {
    if (config_.token.empty() && !generate_dashboard_token(&config_.token)) return false;
    auto* server = new httplib::Server();
    server_ = server;
    install_routes();
    if (!server->bind_to_port(config_.bind_address.c_str(), config_.port)) {
        delete server;
        server_ = nullptr;
        return false;
    }
    return true;
}

void DashboardServer::run() {
    if (!server_) return;
    static_cast<httplib::Server*>(server_)->listen_after_bind();
}

void DashboardServer::stop() {
    if (server_) {
        auto* server = static_cast<httplib::Server*>(server_);
        server->stop();
        delete server;
        server_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(operation_mutex_);
    unload_current();
}

void DashboardServer::unload_current() {
    if (runtime_) {
        syj_edgemind_destroy(runtime_);
        runtime_ = nullptr;
    }
    last_status_ = SYJ_EDGEMIND_OK;
}

bool DashboardServer::load_from_request(const std::string& body, std::string* error_json) {
    if (body.size() > kMaxRequestBody) {
        *error_json = "{\"error\":\"request body too large\"}";
        return false;
    }
    std::string model_id;
    std::string model_path;
    const bool have_id = parse_json_string(body, "model_id", &model_id) && !model_id.empty();
    const bool have_path = parse_json_string(body, "model_path", &model_path) && !model_path.empty();
    if (have_id == have_path) {
        *error_json = "{\"error\":\"provide exactly one of model_id or model_path\"}";
        return false;
    }

    RuntimeConfigState candidate = state_;
    candidate.model_id = have_id ? model_id : "";
    candidate.model_path = have_path ? model_path : "";
    int64_t value = 0;
    if (parse_json_int(body, "context_size", &value)) candidate.context_size = static_cast<int32_t>(value);
    if (parse_json_int(body, "threads", &value)) candidate.threads = static_cast<int32_t>(value);

    syj_edgemind_config config;
    syj_edgemind_default_config(&config);
    config.model_id = candidate.model_id.empty() ? nullptr : candidate.model_id.c_str();
    config.model_path = candidate.model_path.empty() ? nullptr : candidate.model_path.c_str();
    config.context_size = candidate.context_size;
    config.threads = candidate.threads;
    config.temperature = candidate.temperature;
    config.top_p = candidate.top_p;
    config.top_k = candidate.top_k;
    config.max_tokens = candidate.max_tokens;
    config.memory_budget_mb = candidate.memory_budget_mb;
    config.safety_reserve_mb = candidate.safety_reserve_mb;
    config.session_time_limit_seconds = candidate.session_time_limit_seconds;
    config.daily_message_limit = candidate.daily_message_limit;
    config.daily_token_limit = candidate.daily_token_limit;
    config.reset_period_seconds = candidate.reset_period_seconds;
    config.usage_state_path = candidate.usage_state_path.c_str();
    config.expected_model_checksum_sha256 = candidate.expected_checksum.empty() ? nullptr : candidate.expected_checksum.c_str();
    config.model_registry_path = candidate.registry_path.c_str();

    // The core runtime is deliberately single-instance here. Do not create
    // a second loaded model before releasing the current one: that would
    // defeat the memory-admission invariant on low-memory devices.
    unload_current();

    syj_edgemind_status status = SYJ_EDGEMIND_OK;
    syj_edgemind_runtime* fresh = syj_edgemind_create(&config, &status);
    if (!fresh) {
        last_status_ = status;
        *error_json = "{\"error\":" + json_string(status_name(status)) + "}";
        return false;
    }

    runtime_ = fresh;
    state_ = std::move(candidate);
    last_status_ = status;
    if (status != SYJ_EDGEMIND_OK) {
        *error_json = "{\"error\":" + json_string(status_name(status)) + "}";
        return false;
    }
    return true;
}

bool DashboardServer::reload_current(std::string* error_json) {
    if (state_.model_id.empty() && state_.model_path.empty()) {
        *error_json = "{\"error\":\"no previous model configuration to reload\"}";
        return false;
    }
    std::string body = "{\"" + std::string(state_.model_id.empty() ? "model_path" : "model_id") + "\":" +
                       json_string(state_.model_id.empty() ? state_.model_path : state_.model_id) + "}";
    return load_from_request(body, error_json);
}

std::string DashboardServer::health_json() const {
    return std::string("{\"status\":\"ok\",\"loaded\":") + (runtime_ ? "true" : "false") + "}";
}

std::string DashboardServer::version_json() const {
#ifndef SYJ_EDGEMIND_RELEASE_VERSION
#define SYJ_EDGEMIND_RELEASE_VERSION "v0.9.0-alpha"
#endif
    return std::string("{\"release\":") + json_string(SYJ_EDGEMIND_RELEASE_VERSION) +
           ",\"abi_version\":" + std::to_string(syj_edgemind_abi_version()) + "}";
}

std::string DashboardServer::system_json() const {
    // The Phase 7 C API bridge exposes the same platform-neutral struct used
    // by the memory observer. This call remains read-only and does not touch
    // the runtime/model lifecycle.
    syj_edgemind_system_memory_info info{};
    syj_edgemind_get_system_memory(&info);
    std::ostringstream out;
    out << "{\"available\":" << (info.available ? "true" : "false")
        << ",\"total_bytes\":" << info.total_bytes << ",\"available_bytes\":" << info.available_bytes
        << ",\"total_mb\":" << (static_cast<double>(info.total_bytes) / 1048576.0)
        << ",\"available_mb\":" << (static_cast<double>(info.available_bytes) / 1048576.0) << "}";
    return out.str();
}

std::string DashboardServer::models_json() const {
    std::lock_guard<std::mutex> lock(operation_mutex_);
    return std::string("{\"models\":") + models_as_json(registry_report(state_.registry_path)) + "}";
}

std::string DashboardServer::runtime_json() const {
    std::lock_guard<std::mutex> lock(operation_mutex_);
    std::ostringstream out;
    out << "{\"loaded\":" << (runtime_ && last_status_ == SYJ_EDGEMIND_OK ? "true" : "false")
        << ",\"last_status\":" << json_string(status_name(last_status_));
    if (runtime_ && last_status_ == SYJ_EDGEMIND_OK) {
        syj_edgemind_model_info info{};
        if (syj_edgemind_get_model_info(runtime_, &info) == 0) {
            out << ",\"model\":{\"description\":" << json_string(info.description)
                << ",\"n_params\":" << info.n_params << ",\"model_size_bytes\":" << info.model_size_bytes
                << ",\"n_ctx_train\":" << info.n_ctx_train << ",\"n_ctx\":" << info.n_ctx
                << ",\"threads\":" << info.n_threads << "}";
        }
    }
    out << "}";
    return out.str();
}

std::string DashboardServer::memory_json() const {
    std::lock_guard<std::mutex> lock(operation_mutex_);
    return std::string("{\"report\":") + json_string(report_from(syj_edgemind_get_memory_report, runtime_)) + "}";
}

std::string DashboardServer::usage_json() const {
    std::lock_guard<std::mutex> lock(operation_mutex_);
    return std::string("{\"report\":") + json_string(report_from(syj_edgemind_get_usage_report, runtime_)) + "}";
}

std::string DashboardServer::make_json_error(const std::string& message, int) {
    return "{\"error\":" + json_string(message) + "}";
}

} // namespace syj::edgemind::dashboard
