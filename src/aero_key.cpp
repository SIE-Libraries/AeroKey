#include "aero_key.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace aerokey {
namespace {

struct ParsedEndpoint {
    std::string host;
    std::string port;
};

struct CommandResult {
    int exit_code{1};
    std::string output;
};

std::string shell_escape_single_quotes(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

CommandResult run_shell_command(const std::string& command) {
    CommandResult result;
    std::string full = command + " 2>&1";

    FILE* pipe = ::popen(full.c_str(), "r");
    if (!pipe) {
        result.output = "failed to start command";
        return result;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }

    int status = ::pclose(pipe);
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = 1;
    }

    return result;
}

AeroData parse_json_string_pairs(const std::string& input) {
    AeroData data;
    std::regex pair_regex("\"([^\"]+)\"\\s*:\\s*\"([^\"]*)\"");
    auto begin = std::sregex_iterator(input.begin(), input.end(), pair_regex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        data[(*it)[1].str()] = (*it)[2].str();
    }
    return data;
}

std::string payload_to_json_object(const AeroPayload& payload) {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [k, v] : payload) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << '"' << json_escape(k) << "\":\"" << json_escape(v) << '"';
    }
    oss << "}";
    return oss.str();
}

std::string payload_to_firestore_fields(const AeroPayload& payload) {
    std::ostringstream oss;
    oss << "{\"fields\":{";
    bool first = true;
    for (const auto& [k, v] : payload) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << '"' << json_escape(k) << "\":{\"stringValue\":\"" << json_escape(v) << "\"}";
    }
    oss << "}}";
    return oss.str();
}

ParsedEndpoint parse_redis_endpoint(const std::string& endpoint) {
    if (endpoint.empty()) {
        throw std::invalid_argument("endpoint must not be empty");
    }

    std::string value = endpoint;
    const std::string scheme = "redis://";
    if (value.rfind(scheme, 0) == 0) {
        value = value.substr(scheme.size());
    }

    auto slash = value.find('/');
    if (slash != std::string::npos) {
        value = value.substr(0, slash);
    }

    auto colon = value.find(':');
    if (colon == std::string::npos) {
        return {value, "6379"};
    }

    return {value.substr(0, colon), value.substr(colon + 1)};
}

std::string to_resp_command(const std::vector<std::string>& args) {
    std::ostringstream out;
    out << '*' << args.size() << "\r\n";
    for (const auto& arg : args) {
        out << '$' << arg.size() << "\r\n" << arg << "\r\n";
    }
    return out.str();
}

bool read_exact(int fd, std::string& out, std::size_t n) {
    out.clear();
    out.reserve(n);
    while (out.size() < n) {
        char buf[1024];
        const auto need = std::min<std::size_t>(sizeof(buf), n - out.size());
        auto readn = ::recv(fd, buf, need, 0);
        if (readn <= 0) {
            return false;
        }
        out.append(buf, static_cast<std::size_t>(readn));
    }
    return true;
}

bool read_line(int fd, std::string& line) {
    line.clear();
    char c = 0;
    while (true) {
        auto n = ::recv(fd, &c, 1, 0);
        if (n <= 0) {
            return false;
        }
        if (c == '\r') {
            auto n2 = ::recv(fd, &c, 1, 0);
            if (n2 <= 0 || c != '\n') {
                return false;
            }
            return true;
        }
        line.push_back(c);
    }
}

struct RedisValue {
    char type{'-'};
    std::string text;
    std::vector<RedisValue> array;
};

bool read_resp_value(int fd, RedisValue& out) {
    char type = 0;
    if (::recv(fd, &type, 1, 0) <= 0) {
        return false;
    }
    out = RedisValue{};
    out.type = type;

    std::string line;
    if (!read_line(fd, line)) {
        return false;
    }

    if (type == '+' || type == '-' || type == ':') {
        out.text = line;
        return true;
    }

    if (type == '$') {
        const int len = std::stoi(line);
        if (len < 0) {
            out.text.clear();
            return true;
        }
        if (!read_exact(fd, out.text, static_cast<std::size_t>(len))) {
            return false;
        }
        std::string crlf;
        return read_exact(fd, crlf, 2);
    }

    if (type == '*') {
        const int count = std::stoi(line);
        if (count < 0) {
            return true;
        }
        out.array.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            RedisValue item;
            if (!read_resp_value(fd, item)) {
                return false;
            }
            out.array.push_back(std::move(item));
        }
        return true;
    }

    return false;
}

class RedisConnection {
   public:
    explicit RedisConnection(const ParsedEndpoint& endpoint) {
        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo* result = nullptr;
        const int gai = ::getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &result);
        if (gai != 0) {
            throw std::runtime_error(std::string("getaddrinfo failed: ") + ::gai_strerror(gai));
        }

        for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
            int s = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (s < 0) {
                continue;
            }
            if (::connect(s, rp->ai_addr, rp->ai_addrlen) == 0) {
                fd_ = s;
                break;
            }
            ::close(s);
        }

        ::freeaddrinfo(result);

        if (fd_ < 0) {
            throw std::runtime_error(std::string("failed to connect to redis: ") + std::strerror(errno));
        }
    }

    ~RedisConnection() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;

    RedisValue command(const std::vector<std::string>& args) {
        auto payload = to_resp_command(args);

        std::size_t written = 0;
        while (written < payload.size()) {
            auto n = ::send(fd_, payload.data() + written, payload.size() - written, 0);
            if (n <= 0) {
                throw std::runtime_error(std::string("redis send failed: ") + std::strerror(errno));
            }
            written += static_cast<std::size_t>(n);
        }

        RedisValue value;
        if (!read_resp_value(fd_, value)) {
            throw std::runtime_error("failed to read redis response");
        }
        if (value.type == '-') {
            throw std::runtime_error("redis error: " + value.text);
        }
        return value;
    }

   private:
    int fd_{-1};
};

class RedisProvider final : public IAeroProvider {
   public:
    explicit RedisProvider(AeroType backend) : backend_(backend) {}

    AeroWriteResult Push(const AeroUrl& url, const AeroPayload& payload) override {
        auto id_it = payload.find("id");
        const std::string key = !url.key.empty() ? url.key : (id_it != payload.end() ? id_it->second : "");
        if (key.empty()) {
            return {0, {AeroStatus::InvalidArgument, "redis write requires url.key or payload['id']"}};
        }

        try {
            auto endpoint = parse_redis_endpoint(url.endpoint);
            RedisConnection conn(endpoint);

            std::vector<std::string> cmd{"HSET", key};
            for (const auto& [k, v] : payload) {
                cmd.push_back(k);
                cmd.push_back(v);
            }
            conn.command(cmd);
            return {1, {AeroStatus::Ok, ""}};
        } catch (const std::exception& ex) {
            return {0, {AeroStatus::BackendUnavailable, ex.what()}};
        }
    }

    AeroResponse Fetch(const AeroUrl& url) override {
        AeroResponse response;
        response.backend = backend_;

        if (url.key.empty()) {
            response.error = {AeroStatus::InvalidArgument, "redis fetch requires url.key"};
            return response;
        }

        try {
            auto endpoint = parse_redis_endpoint(url.endpoint);
            RedisConnection conn(endpoint);
            auto value = conn.command({"HGETALL", url.key});

            if (value.type != '*') {
                response.error = {AeroStatus::BackendUnavailable, "unexpected redis response type"};
                return response;
            }
            if (value.array.empty()) {
                response.error = {AeroStatus::NotFound, "record not found"};
                return response;
            }

            AeroData rec;
            for (std::size_t i = 0; i + 1 < value.array.size(); i += 2) {
                rec[value.array[i].text] = value.array[i + 1].text;
            }
            response.records.push_back(std::move(rec));
            response.error = {AeroStatus::Ok, ""};
            return response;
        } catch (const std::exception& ex) {
            response.error = {AeroStatus::BackendUnavailable, ex.what()};
            return response;
        }
    }

    AeroError Stream(const AeroUrl& url, const std::function<void(AeroData)>& func) override {
        if (!func) {
            return {AeroStatus::InvalidArgument, "callback must not be empty"};
        }

        auto response = Fetch(url);
        if (!response.error.ok()) {
            return response.error;
        }

        try {
            for (const auto& record : response.records) {
                func(record);
            }
        } catch (const std::exception& ex) {
            return {AeroStatus::CallbackError, ex.what()};
        } catch (...) {
            return {AeroStatus::CallbackError, "unknown callback exception"};
        }

        return {AeroStatus::Ok, ""};
    }

   private:
    AeroType backend_;
};

class MongoProvider final : public IAeroProvider {
   public:
    explicit MongoProvider(AeroType backend) : backend_(backend) {}

    AeroWriteResult Push(const AeroUrl& url, const AeroPayload& payload) override {
        if (url.endpoint.empty() || url.key.empty()) {
            return {0, {AeroStatus::InvalidArgument, "mongodb push requires endpoint and url.key"}};
        }

        const std::string doc_json = payload_to_json_object(payload);
        const std::string script =
            "db.getSiblingDB('aerokey').records.updateOne({_id: '" + shell_escape_single_quotes(url.key) +
            "'}, {$set: " + doc_json + "}, {upsert: true});";

        const std::string cmd = "mongosh '" + shell_escape_single_quotes(url.endpoint) +
                                "' --quiet --eval '" + shell_escape_single_quotes(script) + "'";

        auto result = run_shell_command(cmd);
        if (result.exit_code != 0) {
            return {0, {AeroStatus::BackendUnavailable, "mongosh push failed: " + result.output}};
        }

        return {1, {AeroStatus::Ok, ""}};
    }

    AeroResponse Fetch(const AeroUrl& url) override {
        AeroResponse response;
        response.backend = backend_;

        if (url.endpoint.empty() || url.key.empty()) {
            response.error = {AeroStatus::InvalidArgument, "mongodb fetch requires endpoint and url.key"};
            return response;
        }

        const std::string script =
            "const d=db.getSiblingDB('aerokey').records.findOne({_id: '" + shell_escape_single_quotes(url.key) +
            "'}); if (d) print(EJSON.stringify(d));";

        const std::string cmd = "mongosh '" + shell_escape_single_quotes(url.endpoint) +
                                "' --quiet --eval '" + shell_escape_single_quotes(script) + "'";

        auto result = run_shell_command(cmd);
        if (result.exit_code != 0) {
            response.error = {AeroStatus::BackendUnavailable, "mongosh fetch failed: " + result.output};
            return response;
        }

        auto parsed = parse_json_string_pairs(result.output);
        if (parsed.empty()) {
            response.error = {AeroStatus::NotFound, "record not found"};
            return response;
        }

        response.records.push_back(std::move(parsed));
        response.error = {AeroStatus::Ok, ""};
        return response;
    }

    AeroError Stream(const AeroUrl& url, const std::function<void(AeroData)>& func) override {
        if (!func) {
            return {AeroStatus::InvalidArgument, "callback must not be empty"};
        }

        auto response = Fetch(url);
        if (!response.error.ok()) {
            return response.error;
        }

        for (const auto& rec : response.records) {
            func(rec);
        }
        return {AeroStatus::Ok, ""};
    }

   private:
    AeroType backend_;
};

class FirestoreProvider final : public IAeroProvider {
   public:
    explicit FirestoreProvider(AeroType backend) : backend_(backend) {}

    AeroWriteResult Push(const AeroUrl& url, const AeroPayload& payload) override {
        if (url.endpoint.empty() || url.key.empty()) {
            return {0, {AeroStatus::InvalidArgument, "firestore push requires endpoint and url.key"}};
        }

        const std::string body = payload_to_firestore_fields(payload);
        std::string doc_url = url.endpoint;
        if (!doc_url.empty() && doc_url.back() != '/') {
            doc_url += '/';
        }
        doc_url += url.key;

        const std::string cmd = "curl -sS -X PATCH '" + shell_escape_single_quotes(doc_url) +
                                "' -H 'Content-Type: application/json' -d '" +
                                shell_escape_single_quotes(body) + "'";

        auto result = run_shell_command(cmd);
        if (result.exit_code != 0) {
            return {0, {AeroStatus::BackendUnavailable, "firestore push failed: " + result.output}};
        }

        return {1, {AeroStatus::Ok, ""}};
    }

    AeroResponse Fetch(const AeroUrl& url) override {
        AeroResponse response;
        response.backend = backend_;

        if (url.endpoint.empty() || url.key.empty()) {
            response.error = {AeroStatus::InvalidArgument, "firestore fetch requires endpoint and url.key"};
            return response;
        }

        std::string doc_url = url.endpoint;
        if (!doc_url.empty() && doc_url.back() != '/') {
            doc_url += '/';
        }
        doc_url += url.key;

        const std::string cmd = "curl -sS '" + shell_escape_single_quotes(doc_url) + "'";
        auto result = run_shell_command(cmd);
        if (result.exit_code != 0) {
            response.error = {AeroStatus::BackendUnavailable, "firestore fetch failed: " + result.output};
            return response;
        }

        auto parsed = parse_json_string_pairs(result.output);
        if (parsed.empty()) {
            response.error = {AeroStatus::NotFound, "record not found"};
            return response;
        }

        response.records.push_back(std::move(parsed));
        response.error = {AeroStatus::Ok, ""};
        return response;
    }

    AeroError Stream(const AeroUrl& url, const std::function<void(AeroData)>& func) override {
        if (!func) {
            return {AeroStatus::InvalidArgument, "callback must not be empty"};
        }

        auto response = Fetch(url);
        if (!response.error.ok()) {
            return response.error;
        }

        for (const auto& rec : response.records) {
            func(rec);
        }
        return {AeroStatus::Ok, ""};
    }

   private:
    AeroType backend_;
};

class InMemoryProvider final : public IAeroProvider {
   public:
    explicit InMemoryProvider(AeroType backend) : backend_(backend) {}

    AeroWriteResult Push(const AeroUrl& url, const AeroPayload& payload) override {
        if (url.endpoint.empty()) {
            return {0, {AeroStatus::InvalidArgument, "endpoint must not be empty"}};
        }

        std::lock_guard<std::mutex> lock(mutex_);
        store_[storage_key(url)].push_back(payload);
        return {1, {AeroStatus::Ok, ""}};
    }

    AeroResponse Fetch(const AeroUrl& url) override {
        AeroResponse response;
        response.backend = backend_;

        if (url.endpoint.empty()) {
            response.error = {AeroStatus::InvalidArgument, "endpoint must not be empty"};
            return response;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = store_.find(storage_key(url));
        if (it == store_.end()) {
            response.error = {AeroStatus::NotFound, "no records for url"};
            return response;
        }

        response.records = it->second;
        response.error = {AeroStatus::Ok, ""};
        return response;
    }

    AeroError Stream(const AeroUrl& url, const std::function<void(AeroData)>& func) override {
        if (!func) {
            return {AeroStatus::InvalidArgument, "callback must not be empty"};
        }
        if (url.endpoint.empty()) {
            return {AeroStatus::InvalidArgument, "endpoint must not be empty"};
        }

        std::vector<AeroData> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = store_.find(storage_key(url));
            if (it == store_.end()) {
                return {AeroStatus::NotFound, "no records for url"};
            }
            snapshot = it->second;
        }

        try {
            for (const auto& record : snapshot) {
                func(record);
            }
        } catch (const std::exception& ex) {
            return {AeroStatus::CallbackError, ex.what()};
        } catch (...) {
            return {AeroStatus::CallbackError, "unknown callback exception"};
        }

        return {AeroStatus::Ok, ""};
    }

   private:
    static std::string storage_key(const AeroUrl& url) { return url.endpoint + "::" + url.key; }

    AeroType backend_;
    std::mutex mutex_;
    std::map<std::string, std::vector<AeroData>> store_;
};

std::mutex g_registry_mutex;
std::map<AeroType, std::shared_ptr<IAeroProvider>> g_registry;

std::shared_ptr<IAeroProvider> ensure_provider(AeroType type) {
    auto it = g_registry.find(type);
    if (it != g_registry.end()) {
        return it->second;
    }

    std::shared_ptr<IAeroProvider> provider;
    if (type == AeroType::Redis) {
        provider = std::make_shared<RedisProvider>(type);
    } else if (type == AeroType::MongoDB) {
        provider = std::make_shared<MongoProvider>(type);
    } else if (type == AeroType::FireStore) {
        provider = std::make_shared<FirestoreProvider>(type);
    } else {
        provider = std::make_shared<InMemoryProvider>(type);
    }

    g_registry[type] = provider;
    return provider;
}

}  // namespace

AeroWriteResult AeroPush(AeroType type, const AeroUrl& url, const AeroPayload& payload) {
    std::shared_ptr<IAeroProvider> provider;
    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        provider = ensure_provider(type);
    }
    return provider->Push(url, payload);
}

AeroResponse AeroFetch(AeroType type, const AeroUrl& url) {
    std::shared_ptr<IAeroProvider> provider;
    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        provider = ensure_provider(type);
    }
    return provider->Fetch(url);
}

AeroError AeroStream(AeroUrl url, AeroType type, const std::function<void(AeroData)>& func) {
    std::shared_ptr<IAeroProvider> provider;
    {
        std::lock_guard<std::mutex> lock(g_registry_mutex);
        provider = ensure_provider(type);
    }
    return provider->Stream(url, func);
}

void RegisterProvider(AeroType type, std::shared_ptr<IAeroProvider> provider) {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    if (!provider) {
        throw std::invalid_argument("provider must not be null");
    }
    g_registry[type] = std::move(provider);
}

void ResetProviders() {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    g_registry.clear();
}

}  // namespace aerokey
