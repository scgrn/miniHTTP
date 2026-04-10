/**

zlib License

(C) 2026 Andrew Krause

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

**/

/*

MiniHTTP

Minimal, cross-platform, header-only HTTP/1.1 server for prototyping. Not for production use!

Features:
    Thread-local worker context (for database connections, etc)
    Broadcast to clients via SSE

Usage:
Define MINI_HTTP_IMPLEMENTATION in exactly one translation unit before
including minihttp.h.

Link against ws2_32 on Windows
Link against pthread and dl on Linux

*/

#ifndef MINI_HTTP_H
#define MINI_HTTP_H

#include <iostream>
#include <sstream>
#include <filesystem>
#include <unistd.h>
#include <queue>
#include <unordered_map>
#include <functional>
#include <vector>

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <future>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#endif

#if defined(_MSC_VER)
#define RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define RESTRICT __restrict__
#else
#define RESTRICT
#endif

#ifdef _WIN32
typedef SOCKET Socket;
typedef int socklen_t;
#else
typedef int Socket;
#endif

using ContextFactory = std::function<void*()>;
using ContextDestructor = std::function<void(void*)>;

enum class HttpMethod {
    GET,
    POST,
    PUT,
    DEL,
    PATCH,
    OPTIONS,
    UNKNOWN
};

//  hash function for HttpMethod so we can use it as a map key
namespace std {
    template <>
    struct hash<HttpMethod> {
        std::size_t operator()(const HttpMethod& m) const {
            return static_cast<std::size_t>(m);
        }
    };
}

//  https://www.iana.org/assignments/http-status-codes/http-status-codes.xhtml
inline std::unordered_map<int, std::string> responseCodes {
    //  informational responses
    {100, "Continue"},
    {101, "Switching Protocols"},
    {102, "Processing"},
    {103, "Early Hints"},

    //  success
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {203, "Non-Authoritative Information"},
    {204, "No Content"},
    {205, "Reset Content"},
    {206, "Partial Content"},
    {207, "Multi-Status"},
    {208, "Already Reported"},
    {226, "IM Used"},

    //  redirection
    {300, "Multiple Choices"},
    {301, "Moved Permanently"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {305, "Use Proxy"},
    {307, "Temporary Redirect"},
    {308, "Permanent Redirect"},

    //  client error
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {402, "Payment Required"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {406, "Not Acceptable"},
    {407, "Proxy Authentication Required"},
    {408, "Request Timeout"},
    {409, "Conflict"},
    {410, "Gone"},
    {411, "Length Required"},
    {412, "Precondition Failed"},
    {413, "Content Too Large"},
    {414, "URI Too Long"},
    {415, "Unsupported Media Type"},
    {416, "Range Not Satisfiable"},
    {417, "Expectation Failed"},
    {418, "I'm a teapot"},
    {421, "Misdirected Request"},
    {422, "Unprocessable Content"},
    {423, "Locked"},
    {424, "Failed Dependency"},
    {425, "Too Early"},
    {426, "Upgrade Required"},
    {428, "Precondition Required"},
    {429, "Too Many Requests"},
    {430, "Unassigned"},
    {431, "Request Header Fields Too Large"},
    {451, "Unavailable For Legal Reasons"},

    //  server error
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Timeout"},
    {505, "HTTP Version Not Supported"},
    {506, "Variant Also Negotiates"},
    {507, "Insufficient Storage"},
    {508, "Loop Detected"},
    {509, "Unassigned"},
    {510, "Not Extended (OBSOLETED)"},
    {511, "Network Authentication Required"},
};

struct Request {
    HttpMethod method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct Response {
    int status = 200;
    std::string contentType = "text/plain";
    std::string body;
    bool keepAlive = false;

    std::unordered_map<std::string, std::string> headers = {};

    std::string toString() const;
};

//  BeforeMiddleware can short circuit a request by returning false
using BeforeMiddleware = std::function<bool(const Request&, Response&, void*)>;
using AfterMiddleware = std::function<void(const Request&, Response&, void*)>;

using Handler = std::function<Response(const Request&, void*)>;
using ErrorHandler = std::function<void(const std::string& message, bool fatal)>;

class Router {
    public:
        void add(HttpMethod method, const std::string& path, Handler handler) {
            routes[method][path] = handler;
        }
    
        Response route(const Request& req, void* ctx) const {
            auto m = routes.find(req.method);
            if (m != routes.end()) {
                auto p = m->second.find(req.path);
                if (p != m->second.end()) {
                    return p->second(req, ctx);
                }
            }
    
            return {404, "text/plain", "Not Found"};
        }
    
    private:
        std::unordered_map<HttpMethod, std::unordered_map<std::string, Handler> > routes;
};

class Server {
    public:
        Server(const std::string& ipAddress, int port);
        void setContextFactory(ContextFactory create, ContextDestructor destroy);
        void setErrorHandler(ErrorHandler errorHandler);

        bool start();
        void stop();
        void broadcast(const std::string& message);
        void useBefore(BeforeMiddleware middleware);
        void useAfter(AfterMiddleware middleware);
        void enableCORS();

        Router router;

    private:
        void error(const std::string& message, bool fatal = false);
        void acceptLoop();
        void worker();
        void handleClient(Socket clientSocket, void* ctx);

        Socket serverSocket;
        struct sockaddr_in socketAddress;
        socklen_t socketAddressLen = sizeof(socketAddress);
        
        std::string ipAddress;
        int port;
        int threadCount;
    
        std::atomic<bool> running{true};

        ContextFactory createContext;
        ContextDestructor destroyContext;

        ErrorHandler errorHandler;

        std::vector<Socket> subscribers;
        std::mutex subscriberMutex;

        std::vector<BeforeMiddleware> beforeMiddleware;
        std::vector<AfterMiddleware> afterMiddleware;

        //  connection pool
        std::queue<Socket> jobQueue;
        std::mutex queueMutex;
        std::condition_variable cv;
        
        std::vector<std::thread> workers;
        std::thread acceptThread;
    
};

#endif  //  MINI_HTTP_H

//  ----------------------------------------- END OF HEADER -----------------------------------------

#ifdef MINI_HTTP_IMPLEMENTATION

const int BUFFER_SIZE = 30720;

static bool socketValid(Socket socket) {
#ifdef _WIN32
    return socket != INVALID_SOCKET;
#else
    return socket >= 0;
#endif
}

static void closeSocket(Socket socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

void Server::error(const std::string& message, bool fatal) {
#ifdef _WIN32
    std::cout << WSAGetLastError() << std::endl;
#endif
    errorHandler("ERROR: " + message + "\n", fatal);
}

std::string Response::toString() const {
    std::ostringstream ss;

    ss << "HTTP/1.1 " << status << " " << responseCodes[status] << "\r\n";
    ss << "Content-Type: " << contentType << "\r\n";
    ss << "Cache-Control: no-cache\r\n";
    
    if (keepAlive) {
        ss << "Connection: keep-alive\r\n";
    } else {
        ss << "Content-Length: " << body.size() << "\r\n";
        ss << "Connection: close\r\n";
    }

    for (const auto& [key, value] : headers) {
        ss << key << ": " << value << "\r\n";
    }

    ss << "\r\n";
    ss << body;

    return ss.str();
}

Server::Server(const std::string& ipAddress, int port) {
    this->ipAddress = ipAddress;
    this->port = port;

    threadCount = std::thread::hardware_concurrency();
    if (threadCount > 0) {
        std::cout << "Number of logical cores available: " << threadCount << std::endl;
    } else {
        std::cout << "Could not determine core count, defaulting to one thread." << std::endl;
        threadCount = 1;
    }
    
    //  default error handler   
    setErrorHandler([](const std::string& message, bool fatal) {
        std::cerr << message;
        if (fatal) {
            std::exit(1);
        }
    });

    //  preflight
    useBefore([](const Request& req, Response& res, void*) -> bool {
        if (req.method == HttpMethod::OPTIONS) {
            res.status = 204;
            res.body = "";
            return false;
        }
        return true;
    });

    //  add default routes
    router.add(HttpMethod::GET, "/subscribe", [](const Request& req, void* ctx) -> Response {
        std::cout << ctx << std::endl;
        std::cout << req.body << std::endl;

        return {200, "text/event-stream", "", true};
    });
}

void Server::setContextFactory(ContextFactory create, ContextDestructor destroy) {
    createContext = create;
    destroyContext = destroy;
}

void Server::setErrorHandler(ErrorHandler errorHandler) {
    this->errorHandler = errorHandler;
}

bool Server::start() {
    socketAddress.sin_family = AF_INET;
    socketAddress.sin_port = htons(port);
    socketAddress.sin_addr.s_addr = inet_addr(ipAddress.c_str());

#ifdef _WIN32
    //  initalize winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) {
        error("WSAStartup failed", true);
        return false;
    }
#endif

    //  start server
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (!socketValid(serverSocket)) {
        error("Cannot create socket", true);
        return false;
    }
    
    int opt = 1;
#ifdef _WIN32
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    if (bind(serverSocket, (sockaddr*)&socketAddress, socketAddressLen) < 0) {
        error("Cannot connect socket to address");
        return false;
    }
    
    //  start listening
    if (listen(serverSocket, 20) < 0) {
        error("Socket listen failed", true);
        return false;
    }
    

#ifdef WIN_32
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct hostent* host = gethostbyname(hostname);
        if (host != nullptr) {
            for (int i = 0; host->h_addr_list[i] != 0; i++) {
                struct in_addr addr;
                memcpy(&addr, host->h_addr_list[i], sizeof(struct in_addr));
                std::cout << "Local IP: " << i << ": " << inet_ntoa(addr) << std::endl;
            }
        }
    }
#else
    char buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &socketAddress.sin_addr, buffer, sizeof(buffer));
    std::string address = buffer;

    struct ifaddrs *interfaces = nullptr;
    if (getifaddrs(&interfaces) == 0) {
        for (struct ifaddrs *ifa = interfaces; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr != nullptr && ifa->ifa_addr->sa_family == AF_INET) {
                char ip[INET_ADDRSTRLEN];
                void* addr = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
                inet_ntop(AF_INET, addr, ip, INET_ADDRSTRLEN);
                std::cout << ifa->ifa_name << ": " << ip << std::endl;
            }
        }
        freeifaddrs(interfaces);
    }
#endif

    std::cout << "Listening on port " << ntohs(socketAddress.sin_port) << "\n";

    //    initialize worker threads
    workers.clear();
    for (int i = 0; i < threadCount; i++) {
        workers.emplace_back(&Server::worker, this);
    }    
    acceptThread = std::thread(&Server::acceptLoop, this);

    return true;
}

void Server::stop() {
    running = false;

    //  close server. unblock accept()
#ifdef _WIN32
    shutdown(serverSocket, SD_BOTH);
    closesocket(serverSocket);
    WSACleanup();
#else
    shutdown(serverSocket, SHUT_RDWR);
    close(serverSocket);
#endif

    acceptThread.join();

    cv.notify_all();
    for (auto &t : workers) {
        t.join();
    }
}

void Server::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(subscriberMutex);
    std::string payload = "data: " + message + "\n\n";
    
    for (auto it = subscribers.begin(); it != subscribers.end();) {
        Socket socket = *it;
        
        int sent = send(socket, payload.c_str(), payload.size(), 0);

        if (sent <= 0) {
            //  client disconnected
            closeSocket(socket);
            it = subscribers.erase(it);
        } else {
            it++;
        }
    }
}
        
void Server::useBefore(BeforeMiddleware middleware) {
    beforeMiddleware.push_back(middleware);
}

void Server::useAfter(AfterMiddleware middleware) {
    afterMiddleware.push_back(middleware);
}

void Server::enableCORS() {
    useAfter([](const Request& req, Response& res, void*) {
        res.headers["Access-Control-Allow-Origin"] = "*";
        res.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, PATCH, OPTIONS";
        res.headers["Access-Control-Allow-Headers"] = "Content-Type";
    });
}

void Server::acceptLoop() {
    while (running) {
        std::cout << "Waiting for a new connection...\n";
        
        //  accept connection
        socketAddressLen = sizeof(socketAddress);
        Socket clientSocket = accept(serverSocket, (sockaddr *)&socketAddress, &socketAddressLen);
        if (!socketValid(clientSocket)) {
            if (!running) {
                //  expected during shutdown
                break;
            }
            
            char buffer[INET_ADDRSTRLEN];
            std::string address = inet_ntop(AF_INET, &socketAddress.sin_addr, buffer, sizeof(buffer));
        
            std::ostringstream ss;
            ss << "Server failed to accept incoming connection from ADDRESS: "
                << address << " PORT: " << ntohs(socketAddress.sin_port);
            error(ss.str());

            continue;
        }
        
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (jobQueue.size() > 100) {
                // job queue full, reject connection
                closeSocket(clientSocket);

                continue;
            }
            jobQueue.push(clientSocket);
        }

        cv.notify_one();
    }
}

void Server::worker() {
    void* ctx = nullptr;

    if (createContext) {
        ctx = createContext();  // user-defined
    }

    while (running || !jobQueue.empty()) {
        Socket clientSocket;

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            cv.wait(lock, [this]{
                return !jobQueue.empty() || !running;
            });

            if (!running && jobQueue.empty()) {
                break;
            }

            clientSocket = jobQueue.front();
            jobQueue.pop();
        }

        handleClient(clientSocket, ctx);
    }

    if (destroyContext && ctx) {
        destroyContext(ctx);
    }
}

static bool readRequest(Socket clientSocket, std::string& out) {
    char buffer[BUFFER_SIZE];
    out.clear();

    size_t headerEnd = std::string::npos;
    unsigned int contentLength = 0;

    while (true) {
        int bytes = recv(clientSocket, buffer, sizeof(buffer), 0);

        if (bytes <= 0) {
            return false;
        }

        out.append(buffer, bytes);

        //  check if headers complete
        if (headerEnd == std::string::npos) {
            headerEnd = out.find("\r\n\r\n");

            if (headerEnd != std::string::npos) {
                //  parse Content-Length if exists
                size_t pos = out.find("Content-Length:");

                if (pos != std::string::npos) {
                    size_t end = out.find("\r\n", pos);

                    std::string value = out.substr(pos + 15, end - (pos + 15));

                    contentLength = std::stoi(value);
                } else {
                    return true;
                }
            }
        }

        //  if headers read, check body completion
        if (headerEnd != std::string::npos) {
            size_t bodyStart = headerEnd + 4;
            size_t bodySize = out.size() - bodyStart;

            if (bodySize >= contentLength) {
                return true;
            }
        }
    }
}

static bool parseHttpRequest(const char* buffer, size_t length, Request& req) {
    std::string data(buffer, length);

    size_t pos = data.find("\r\n\r\n");
    if (pos == std::string::npos) {
        return false;
    }

    std::string headerPart = data.substr(0, pos);
    req.body = data.substr(pos + 4);

    std::istringstream stream(headerPart);
    std::string line;

    // parse start line
    if (!std::getline(stream, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    std::istringstream startLine(line);

    std::string methodStr;
    startLine >> methodStr;
    startLine >> req.path;
    startLine >> req.version;

    if (methodStr == "GET") {
        req.method = HttpMethod::GET;
    } else if (methodStr == "POST") {
        req.method = HttpMethod::POST;
    } else if (methodStr == "PUT") {
        req.method = HttpMethod::PUT;
    } else if (methodStr == "DELETE") {
        req.method = HttpMethod::DEL;
    } else if (methodStr == "PATCH") {
        req.method = HttpMethod::PATCH;
    } else {
        req.method = HttpMethod::UNKNOWN;
    }

    // parse headers
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        if (!value.empty() && value[0] == ' ') {
            value.erase(0, 1);
        }

        req.headers[key] = value;
    }

    return true;
}

void Server::handleClient(Socket clientSocket, void* ctx) {
    Request request;
    Response response;

    //  read request
    std::string rawRequest;
    if (!readRequest(clientSocket, rawRequest)) {
        error("Failed to read request");
        closeSocket(clientSocket);

        return;
    }

    //  parse request
    if (!parseHttpRequest(rawRequest.c_str(), rawRequest.size(), request)) {
        Response response = Response{400, "text/plain", "Bad Request"};
        std::string responseStr = response.toString();
        send(clientSocket, responseStr.c_str(), responseStr.size(), 0);
        closeSocket(clientSocket);

        return;
    }

    //  run before middleware
    for (auto& middleware : beforeMiddleware) {
        if (!middleware(request, response, ctx)) {
            std::string responseStr = response.toString();
            send(clientSocket, responseStr.c_str(), responseStr.size(), 0);            
            closeSocket(clientSocket);
            
            return;
        }
    }

    //  route request
    response = router.route(request, ctx);

    //  run after middleware
    for (auto& middleware : afterMiddleware) {
        middleware(request, response, ctx);
    }

    //  add subscriber
    if (response.keepAlive) {
        {
            std::lock_guard<std::mutex> lock(subscriberMutex);
            subscribers.push_back(clientSocket);
        }
    }
    
    // write HTTP response
    std::string responseStr = response.toString();
    int bytesSent;
    size_t totalBytesSent = 0;

    while (totalBytesSent < responseStr.size()) {
        bytesSent = send(clientSocket,
            responseStr.c_str() + totalBytesSent,
            responseStr.size() - totalBytesSent, 0);

        if (bytesSent < 0) {
            break;
        }
        totalBytesSent += bytesSent;
    }

    if (totalBytesSent != responseStr.size()) {
        std::cout << "Error sending response to client.";
    }

    if (!response.keepAlive) {
        closeSocket(clientSocket);
    }
}

#endif    //  MINI_HTTP_IMPLEMENTATION
