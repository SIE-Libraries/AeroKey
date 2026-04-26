# 🗝️ Aero Key

The Universal NoSQL Standard Connector for C++.

Aero Key provides a backend-agnostic API for key/value-style NoSQL operations and now includes:

- provider abstraction (`IAeroProvider`) for pluggable backends,
- structured error/status model (`AeroStatus`, `AeroError`),
- **real Redis provider** (`AeroType::Redis`) via TCP + RESP,
- **real Mongo provider** (`AeroType::MongoDB`) via `mongosh`,
- **real Firestore provider** (`AeroType::FireStore`) via REST (`curl`),
- in-memory default provider for `AeroType::AWSDynamoDB` in local/dev mode,
- pybind11 bindings for Python usage.

## 🛠️ Core API

- `AeroPush(AeroType, AeroUrl, AeroPayload) -> AeroWriteResult`
- `AeroFetch(AeroType, AeroUrl) -> AeroResponse`
- `AeroStream(AeroUrl, AeroType, callback) -> AeroError`
- `RegisterProvider(AeroType, std::shared_ptr<IAeroProvider>)`
- `ResetProviders()`

## Provider notes

### Redis (`AeroType::Redis`)
- Endpoint: `redis://host:port` (port defaults to `6379`).
- Push: `HSET` (requires `url.key` or payload `"id"`).
- Fetch: `HGETALL` (requires `url.key`).

### MongoDB (`AeroType::MongoDB`)
- Uses `mongosh` executable.
- Endpoint should be a Mongo connection string, e.g. `mongodb://localhost:27017`.
- Uses database `aerokey`, collection `records`, and document `_id = url.key`.

### Firestore (`AeroType::FireStore`)
- Uses `curl` with Firestore REST API.
- Endpoint should be a document collection URL, e.g.  
  `https://firestore.googleapis.com/v1/projects/<project>/databases/(default)/documents/aerokey`
- Document ID is `url.key`.

## 💻 C++ Example

```cpp
#include "aero_key.h"

using namespace aerokey;

// Uses in-memory default provider for local run.
AeroUrl url("memory://demo", "user:1");
auto write = AeroPush(AeroType::AWSDynamoDB, url, {{"id", "1"}, {"name", "AeroUser"}});
if (!write.error.ok()) {
    // handle error
}

auto response = AeroFetch(AeroType::AWSDynamoDB, url);
if (response.error.ok()) {
    // use response.records
}
```

## 🐍 Python (pybind11)

```python
from aerokey import AeroType, AeroUrl, AeroPush, AeroFetch

url = AeroUrl("redis://127.0.0.1:6379", "user:1")
write = AeroPush(AeroType.Redis, url, {"id": "1", "name": "AeroUser"})
print(write.error.code)

resp = AeroFetch(AeroType.Redis, url)
print(resp.records)
```

## 🔧 Local Build & Test

```bash
# C++ demo
g++ -std=c++17 -Wall -Wextra -pedantic -Iinclude src/aero_key.cpp examples/main.cpp -o aerokey_demo
./aerokey_demo

# C++ core tests
g++ -std=c++17 -Wall -Wextra -pedantic -Iinclude src/aero_key.cpp tests/test_core.cpp -o aerokey_tests
./aerokey_tests

# Python package (builds pybind11 extension)
python -m pip install .
```

## 🤖 GitHub Actions

CI workflow at `.github/workflows/ci.yml` runs:

- C++ demo compile + run
- C++ test compile + run
- `pip install .` + Python import smoke test

## 📜 License

Licensed under the MIT License.
