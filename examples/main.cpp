#include <iostream>

#include "aero_key.h"

int main() {
    using namespace aerokey;

    AeroUrl myDb("https://project-id.firebaseio.com", "AIzaSy-example-auth-key");
    AeroType currentStack = AeroType::AWSDynamoDB;

    AeroPayload userRecord = {{"id", "001"}, {"name", "AeroUser"}};
    auto write = AeroPush(currentStack, myDb, userRecord);
    if (!write.error.ok()) {
        std::cerr << "Write failed: " << write.error.message << "\n";
        return 1;
    }

    auto result = AeroFetch(currentStack, myDb);
    if (!result.error.ok()) {
        std::cerr << "Fetch failed: " << result.error.message << "\n";
        return 1;
    }

    std::cout << "Fetched records: " << result.records.size() << "\n";

    auto stream_error = AeroStream(myDb, currentStack, [](AeroData data) {
        std::cout << "Streaming Record: " << data["name"] << "\n";
    });

    if (!stream_error.ok()) {
        std::cerr << "Stream failed: " << stream_error.message << "\n";
        return 1;
    }

    return 0;
}
