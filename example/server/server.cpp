#define MINI_HTTP_IMPLEMENTATION
#include "../../minihttp.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

struct Context {
    std::mutex messageMutex;
    std::vector<std::string> messages;
};

static std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

static std::string formatMessage(const std::string& username, const std::string& text) {
    std::ostringstream oss;
    oss << "{"
        << "\"username\":\"" << username << "\","
        << "\"text\":\"" << text << "\","
        << "\"timestamp\":\"" << getCurrentTimestamp() << "\""
        << "}";
    return oss.str();
}

int main(int, char**) {
    std::cout << "Enter \"quit\" to shutdown server and exit.\n\n";

    Server server("0.0.0.0", 7815);
    server.enableCORS();

    server.setContextFactory(
        []() -> void* {
            return new Context();
        },
        [](void* ptr) {
            delete static_cast<Context*>(ptr);
        }
    );

    server.useBefore([](const Request& req, Response& res, void*) -> bool {
        std::cout << "Request: " << req.path << std::endl;
        return true;
    });

    server.router.add(HttpMethod::GET, "/subscribe", [&server](const Request& req, void* ptr) -> Response {
        Context* ctx = static_cast<Context*>(ptr);
        std::string history;
        {
            std::lock_guard<std::mutex> lock(ctx->messageMutex);
            if (ctx->messages.empty()) {
                history = "data: {\"type\":\"connected\"}\n\n";
            } else {
                for (const auto& msg : ctx->messages) {
                    history += "data: " + msg + "\n\n";
                }
            }
        }
        Response res;
        res.status = 200;
        res.contentType = "text/event-stream";
        res.body = history;
        res.keepAlive = true;

        return res;
    });

    server.router.add(HttpMethod::POST, "/message", [&server](const Request& req, void* ptr) -> Response {
        Context* ctx = static_cast<Context*>(ptr);

        std::string username = "Anonymous";
        std::string text;

        size_t uPos = req.body.find("\"username\":");
        if (uPos != std::string::npos) {
            size_t start = req.body.find('"', uPos + 10) + 1;
            size_t end = req.body.find('"', start);
            if (start > 0 && end != std::string::npos) {
                username = req.body.substr(start, end - start);
            }
        }

        size_t tPos = req.body.find("\"text\":");
        if (tPos != std::string::npos) {
            size_t start = req.body.find('"', tPos + 6) + 1;
            size_t end = req.body.find('"', start);
            if (start > 0 && end != std::string::npos) {
                text = req.body.substr(start, end - start);
            }
        }

        std::string formatted = formatMessage(username, text);

        {
            std::lock_guard<std::mutex> lock(ctx->messageMutex);
            ctx->messages.push_back(formatted);
        }

        server.broadcast(formatted);

        return {200, "application/json", "{\"ok\":true}"};
    });

    server.start();

    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        if (cmd == "quit") {
            break;
        }
    }
    server.stop();

    std::cout << "Goodbye." << std::endl;

    return 0;
}
