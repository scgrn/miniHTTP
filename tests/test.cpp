#define MINI_HTTP_TEST_BUILD
#define MINI_HTTP_IMPLEMENTATION
#include "../minihttp.h"

#include <cassert>
#include <cstring>

void testResonseToString() {
    Response res;
    res.status = 200;
    res.contentType = "text/plain";
    res.body = "hello";

    std::string str = res.toString();

    assert(str.find("HTTP/1.1 200 OK") != std::string::npos);
    assert(str.find("Content-Type: text/plain") != std::string::npos);
    assert(str.find("Content-Length: 5") != std::string::npos);
    assert(str.find("\r\n\r\nhello") != std::string::npos);
}

void testRouterBasic() {
    Router router;

    router.add(HttpMethod::GET, "/test", [](const Request&, void*) {
        return Response{200, "text/plain", "ok"};
    });

    Request req;
    req.method = HttpMethod::GET;
    req.path = "/test";

    Response res = router.route(req, nullptr);

    assert(res.status == 200);
    assert(res.body == "ok");
}

void testRouter404() {
    Router router;

    Request req;
    req.method = HttpMethod::GET;
    req.path = "/does_not_exist";

    Response res = router.route(req, nullptr);

    assert(res.status == 404);
}

void testBeforeMiddlewareBlocks() {
    Server server("127.0.0.1", 0);

    server.useBefore([](const Request&, Response& res, void*) {
        res.status = 403;
        res.body = "blocked";
        return false;
    });

    Request req;
    req.method = HttpMethod::GET;
    req.path = "/";

    Response res;

    bool allowed = true;
    for (auto& mw : server.beforeMiddleware) {
        if (!mw(req, res, nullptr)) {
            allowed = false;
            break;
        }
    }

    assert(!allowed);
    assert(res.status == 403);
    assert(res.body == "blocked");
}

void testAfterMiddlewareModifiesResponse() {
    Server server("127.0.0.1", 0);

    server.useAfter([](const Request&, Response& res, void*) {
        res.headers["X-Test"] = "123";
    });

    Request req;
    Response res;

    for (auto& mw : server.afterMiddleware) {
        mw(req, res, nullptr);
    }

    assert(res.headers["X-Test"] == "123");
}

void testContextFactory() {
    Server server("127.0.0.1", 0);

    bool created = false;
    bool destroyed = false;

    server.setContextFactory(
        [&]() -> void* {
            created = true;
            return new int(42);
        },
        [&](void* ctx) {
            destroyed = true;
            delete static_cast<int*>(ctx);
        }
    );

    // simulate worker lifecycle
    void* ctx = server.createContext();
    assert(created);
    assert(*(int*)ctx == 42);

    server.destroyContext(ctx);
    assert(destroyed);
}

void testEnableCors() {
    Server server("127.0.0.1", 0);
    server.enableCORS();

    Request req;
    Response res;

    for (auto& mw : server.afterMiddleware) {
        mw(req, res, nullptr);
    }

    assert(res.headers["Access-Control-Allow-Origin"] == "*");
    assert(res.headers["Access-Control-Allow-Methods"].find("GET") != std::string::npos);
}

void testSubscribeResponse() {
    Server server("127.0.0.1", 0);

    Request req;
    req.method = HttpMethod::GET;
    req.path = "/subscribe";

    Response res = server.router.route(req, nullptr);

    assert(res.status == 200);
    assert(res.keepAlive == true);
    assert(res.contentType == "text/event-stream");
}

void testParseHttpRequest() {
    const char* raw =
        "GET /hello HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "abcde";

    Request req;
    bool ok = parseHttpRequest(raw, strlen(raw), req);

    assert(ok);
    assert(req.method == HttpMethod::GET);
    assert(req.path == "/hello");
    assert(req.body == "abcde");
    assert(req.headers["Host"] == "localhost");
}

void testPathParamSingle() {
    Router router;

    router.add(HttpMethod::GET, "user/:id", [](Request& req, void*) {
        return Response{200, "text/plain", req.pathParams["id"]};
    });

    Request req;
    req.method = HttpMethod::GET;
    req.path = "/user/412";

    Response res = router.route(req, nullptr);

    assert(res.status == 200);
    assert(res.body == "412");
}

void testPathParamMultiple() {
    Router router;

    router.add(HttpMethod::GET, "user/:userId/post/:postId", [](Request& req, void*) {
        return Response{200, "text/plain", req.pathParams["userId"] + "," + req.pathParams["postId"]};
    });

    Request req;
    req.method = HttpMethod::GET;
    req.path = "/user/132/post/213";

    Response res = router.route(req, nullptr);

    assert(res.status == 200);
    assert(res.body == "132,213");
}

void testQueryParamSingle() {
    const char* raw =
        "GET /api?name=Alice HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    Request req;

    assert(parseHttpRequest(raw, strlen(raw), req));
    assert(req.path == "/api");
    assert(req.queryParams["name"] == "Alice");
}

void testQueryParamMultiple() {
    const char* raw =
        "GET /api?user=Ringo&submarine=YELLOW HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n";

    Request req;

    assert(parseHttpRequest(raw, strlen(raw), req));
    assert(req.path == "/api");
    assert(req.queryParams["user"] == "Ringo");
    assert(req.queryParams["submarine"] == "YELLOW");
}

int main(int argc, char* argv[]) {
    testResonseToString();
    testRouterBasic();
    testRouter404();
    testBeforeMiddlewareBlocks();
    testAfterMiddlewareModifiesResponse();
    testContextFactory();
    testEnableCors();
    testSubscribeResponse();
    testParseHttpRequest();
    testPathParamSingle();
    testPathParamMultiple();
    testQueryParamSingle();
    testQueryParamMultiple();

    std::cout << "All tests passed.\n";
}

