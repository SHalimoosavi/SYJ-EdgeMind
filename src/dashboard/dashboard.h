#ifndef SYJ_EDGEMIND_DASHBOARD_DASHBOARD_H
#define SYJ_EDGEMIND_DASHBOARD_DASHBOARD_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "api/edge_mind_api.h"

namespace syj::edgemind::dashboard {

struct DashboardConfig {
    std::string bind_address = "127.0.0.1";
    int port = 8787;
    std::string model_registry_path;
    std::string token;
};

class DashboardServer {
public:
    explicit DashboardServer(DashboardConfig config);
    ~DashboardServer();

    DashboardServer(const DashboardServer&) = delete;
    DashboardServer& operator=(const DashboardServer&) = delete;

    bool start();
    void run();
    void stop();
    const std::string& token() const { return config_.token; }
    int port() const { return config_.port; }

private:
    struct RuntimeConfigState {
        std::string model_path;
        std::string model_id;
        std::string expected_checksum;
        std::string registry_path;
        std::string usage_state_path;
        int32_t context_size = 1024;
        int32_t threads = 4;
        float temperature = 0.7f;
        float top_p = 0.9f;
        int32_t top_k = 40;
        int32_t max_tokens = 256;
        int64_t memory_budget_mb = 3000;
        int64_t safety_reserve_mb = 300;
        int64_t session_time_limit_seconds = 0;
        int64_t daily_message_limit = 0;
        int64_t daily_token_limit = 0;
        int64_t reset_period_seconds = 86400;
    };

    DashboardConfig config_;
    RuntimeConfigState state_;
    syj_edgemind_runtime* runtime_ = nullptr;
    syj_edgemind_status last_status_ = SYJ_EDGEMIND_OK;
    mutable std::mutex operation_mutex_;
    void* server_ = nullptr;

    void install_routes();
    bool authenticate(const std::string& authorization) const;
    std::string health_json() const;
    std::string version_json() const;
    std::string system_json() const;
    std::string models_json() const;
    std::string runtime_json() const;
    std::string memory_json() const;
    std::string usage_json() const;

    bool load_from_request(const std::string& body, std::string* error_json);
    bool reload_current(std::string* error_json);
    void unload_current();
    static std::string make_json_error(const std::string& message, int status = 400);
};

bool generate_dashboard_token(std::string* out_token);

} // namespace syj::edgemind::dashboard

#endif // SYJ_EDGEMIND_DASHBOARD_DASHBOARD_H
