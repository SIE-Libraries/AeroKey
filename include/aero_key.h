#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace aerokey {

enum class AeroType {
    Redis,
    MongoDB,
    FireStore,
    AWSDynamoDB,
};

enum class AeroStatus {
    Ok,
    NotFound,
    InvalidArgument,
    BackendUnavailable,
    CallbackError,
};

using AeroData = std::map<std::string, std::string>;
using AeroPayload = AeroData;

struct AeroUrl {
    std::string endpoint;
    std::string key;

    AeroUrl(std::string url, std::string key_value)
        : endpoint(std::move(url)), key(std::move(key_value)) {}
};

struct AeroError {
    AeroStatus code{AeroStatus::Ok};
    std::string message{};

    [[nodiscard]] bool ok() const { return code == AeroStatus::Ok; }
};

struct AeroWriteResult {
    std::size_t written{0};
    AeroError error{};
};

struct AeroResponse {
    AeroType backend{AeroType::Redis};
    std::vector<AeroData> records{};
    AeroError error{};
};

class IAeroProvider {
   public:
    virtual ~IAeroProvider() = default;

    virtual AeroWriteResult Push(const AeroUrl& url, const AeroPayload& payload) = 0;
    virtual AeroResponse Fetch(const AeroUrl& url) = 0;
    virtual AeroError Stream(const AeroUrl& url, const std::function<void(AeroData)>& func) = 0;
};

AeroWriteResult AeroPush(AeroType type, const AeroUrl& url, const AeroPayload& payload);
AeroResponse AeroFetch(AeroType type, const AeroUrl& url);
AeroError AeroStream(AeroUrl url, AeroType type, const std::function<void(AeroData)>& func);

void RegisterProvider(AeroType type, std::shared_ptr<IAeroProvider> provider);
void ResetProviders();

}  // namespace aerokey
