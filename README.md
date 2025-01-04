# coro25

`coro25` is a simple promise library to enable C++ 20 coroutines.

The library adopts a straightforward design:
1. The promise contains result memory storage and a pointer to the callback.
2. On the other hand, the callback contains a pointer to the promise.
3. When the promise or the callback is moved, the pointers are simply updated.

The design is depicted in the following diagram:

```
+-----------------+        +-----------------+
|   Promise       |        |   Callback      |
|-----------------|        |-----------------|
| - result memory |<------>| - pointer to    |
| - pointer to    |        |   promise       |
|   callback      |        |                 |
+-----------------+        +-----------------+
```

This approach is simple, efficient, and flexible.
It induces no overhead of virtual functions or vtables.

Since callbacks are just pointers to the promise objects, they can be stored in user-void-pointers like being used in C-libraries.
This works as long as the C-library does not move the memory around.

To store a callback, you can use the `store` method provided by the callback object, passing a pointer to the location where you want to store it.
Similarly, to load a callback, you can use the `load` method, passing a pointer to the location where the callback was stored.

Here is a simple usage example demonstrating how to use promises with `libuv`:

```cpp
#include "promise/Promise.hpp"
#include "uv.h"
#include <iostream>

using namespace promise;

static uv_timer_t tim;

static auto mysleep(int timeout) -> Promise<void> {
  return Promise<void>([timeout](auto callback) {
    callback.store(&tim.data);
    uv_timer_start(&tim, [](uv_timer_t* hndl) { Callback<void>::load(&hndl->data)(); }, timeout, 0);
  });
}

auto main() -> int {
  uv_loop_init(uv_default_loop());
  uv_timer_init(uv_default_loop(), &tim);

  ([]() -> Promise<void> {
    std::cout << "Starting coroutine...\n";
    co_await mysleep(1000);
    std::cout << "Coroutine resumed after 1 second.\n";
  })();

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  return 0;
}
```
