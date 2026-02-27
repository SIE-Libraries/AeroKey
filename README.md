🗝️ Aero KeyThe Universal NoSQL Standard Connector for C++Aero Key is a high-performance, standardized abstraction layer for NoSQL databases. It allows developers to write database-agnostic code, enabling seamless switching between Redis, MongoDB, Firestore, and AWS DynamoDB with zero refactoring.🚀 The "One-Line Swap" PhilosophyAero Key is built on the principle that your business logic shouldn't be held hostage by your database driver. By standardizing the connection and interaction patterns, you can migrate from a local Redis cache to a global Firestore instance by changing a single parameter.🛠️ Core API Reference1. AeroUrlThe configuration object that encapsulates the connection string and security credentials.Signature: AeroUrl(std::string url, std::string key)2. AeroFetchRetrieves data from the specified provider.Signature: AeroFetch(AeroType type, AeroUrl url)Returns: A standardized AeroResponse object.3. AeroPushInserts or updates data across any supported NoSQL backend.Signature: AeroPush(AeroType type, AeroUrl url, AeroPayload payload)Function: Translates the AeroPayload into the native format (BSON, JSON, or Key-Value pairs).4. AeroStreamAn asynchronous, functional interface for processing data as a continuous stream.Signature: AeroStream(AeroUrl url, AeroType type, std::functional<void(AeroData)> func)💻 Code Example#include "aero_key.h"

// Initialize connection details
AeroUrl myDb("[https://project-id.firebaseio.com](https://project-id.firebaseio.com)", "AIzaSy...auth-key");

// --- THE ONE-LINE SWAP ---
// Switch between AeroType::Redis, MongoDB, FireStore, or AWS
AeroType currentStack = AeroType::FireStore; 

// 1. Push data
AeroPayload userRecord = {{"id", "001"}, {"name", "AeroUser"}};
AeroPush(currentStack, myDb, userRecord);

// 2. Fetch data
auto result = AeroFetch(currentStack, myDb);

// 3. Stream data
AeroStream(myDb, currentStack, [](AeroData data) {
    std::cout << "Streaming Record: " << data["name"] << std::endl;
});
🏗️ Supported BackendsAeroTypeDatabaseProtocolRedisRedisIn-Memory / RESPMongoDBMongoDBDocument / BSONFireStoreGoogle FirestoreDocument / GRPCAWSDynamoDBKey-Value / HTTP🎯 Why Aero Key?Vendor Agnostic: Stop writing boilerplate for specific SDKs.Modern C++: Heavy use of std::functional and type-safe enums.Efficiency: Built-in AeroStream ensures memory-efficient handling of large datasets.Security: AeroUrl keeps keys and endpoints encapsulated and away from your logic.📜 LicenseLicensed under the MIT License.
