#include <stdexcept>

#include "aero_key.h"

int main() {
    using namespace aerokey;

    ResetProviders();

    AeroUrl url("memory://demo", "test-key");

    auto write = AeroPush(AeroType::AWSDynamoDB, url, {{"id", "1"}, {"name", "alice"}});
    if (!write.error.ok() || write.written != 1) {
        throw std::runtime_error("expected successful write");
    }

    auto fetch = AeroFetch(AeroType::AWSDynamoDB, url);
    if (!fetch.error.ok() || fetch.records.size() != 1) {
        throw std::runtime_error("expected one fetched record");
    }

    std::size_t streamed = 0;
    auto stream_error = AeroStream(url, AeroType::AWSDynamoDB, [&](AeroData) { streamed++; });
    if (!stream_error.ok() || streamed != 1) {
        throw std::runtime_error("expected one streamed record");
    }

    auto missing = AeroFetch(AeroType::AWSDynamoDB, AeroUrl("memory://demo", "other-key"));
    if (missing.error.code != AeroStatus::NotFound) {
        throw std::runtime_error("expected not found for backend with no data");
    }

    auto bad = AeroPush(AeroType::AWSDynamoDB, AeroUrl("", "k"), {{"id", "1"}});
    if (bad.error.code != AeroStatus::InvalidArgument) {
        throw std::runtime_error("expected invalid argument for empty endpoint");
    }

    // Redis provider is now a real network provider. Without a running Redis instance,
    // operations should fail with BackendUnavailable instead of silently succeeding.
    auto redis = AeroFetch(AeroType::Redis, AeroUrl("redis://127.0.0.1:6390", "missing"));
    if (redis.error.code != AeroStatus::BackendUnavailable) {
        throw std::runtime_error("expected backend unavailable for unreachable redis");
    }


    auto mongo = AeroFetch(AeroType::MongoDB, AeroUrl("mongodb://127.0.0.1:27018", "missing"));
    if (mongo.error.code != AeroStatus::BackendUnavailable && mongo.error.code != AeroStatus::NotFound) {
        throw std::runtime_error("expected backend unavailable/not found for mongo provider in test environment");
    }

    auto firestore = AeroFetch(
        AeroType::FireStore,
        AeroUrl("https://firestore.googleapis.com/v1/projects/example/databases/(default)/documents/aerokey", "missing"));
    if (firestore.error.code != AeroStatus::BackendUnavailable && firestore.error.code != AeroStatus::NotFound) {
        throw std::runtime_error("expected backend unavailable/not found for firestore provider in test environment");
    }

    return 0;
}
