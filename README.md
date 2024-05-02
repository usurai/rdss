# rdss

rdss is an experimental in-memory key-value server on Linux, it offers the following features:

- It supports a subset of Redis' `String` commands, and utilizes the RESP wire protocol, making it compatible with some of Redis clients.
- It's able to thread-scale the network I/O and protocol parsing with the help of the asynchronous library(TODO: link to io readme) that leverages io_uring and c++ coroutine efficiently.
- Supports Redis' `maxmemory` directive by implementing key eviction, employing approximated LRU and randomized eviction policies to manage memory usage effectively.

## Limitations

rdss is an experimental project and not production-ready. Its current limitations include:

- Lack of Persistence: RDSS operates purely in-memory, lacking any persistence mechanism. Consequently, data loss occurs if the program crashes or the node fails.
- Single-Node Limitation: RDSS functions solely as a single-node program, devoid of replication support or a cluster mode.
- Limited Multi-Threading Benefits: Although RDSS supports multi-threaded I/O, its core, the hash table, remains single-threaded. As a result, scaling beyond 13 threads yields diminishing benefits.

## API Coverage

Refer to [rdss Commands](./src/service/commands/README.md) for detailed information on supported commands.

## Architecture

rdss utilizes a simple single-threaded data structure service design for its core functionality. However, it optimizes network I/O and stateless computation tasks, such as protocol parsing and serialization, by employing multi-threading. Since the majority of query time is spent on I/O operations, offloading this work to additional threads yields significant benefits.

![overview](./doc/images/overview.svg)

### Threading Model

rdss operates within a single process, orchestrated by one service thread and N I/O threads.

At its core, rdss is just a straightforward hash table. The service thread manages operations involving the hash table, such as serving queries, resizing the table when it gets fully loaded, purging expired keys, and evicting data when maxmemory limits are reached.

The I/O threads handle network I/O and computation that doesn't need the serializability, such as protocal parsing and serialization.

The one that weavens the service thread and I/O threads together is the coroutine. Upon each connection, a coroutine is spawned to handle client interactions, following this sequence:

1. Initialize the client state.
2. co_await `recv()` from the connection to receive data, then parse the command and arguments.
3. Execute the command via service API and retrieve the result.
4. Serialize the result, co_await to send the serialized data.
5. Reset state, return to step 2.

Most steps above are executed by certain I/O thread, except the command execution, handled by the service thread. Thanks to the coroutine's ability to suspend / resume, the code of the above logic appears sequential, avoiding the callback-style complexity.

Underneath, implicit coroutine suspensions and resumptions occur, transitioning execution as follows:

1. Spawn the execution on the I/O thread.
2. Initialize the client state.
3. Initiate a `recv()` operation and suspend, awaiting the data arrival.
4. Upon data reception, resume and parse the command and arguments.
5. Suspend to transfer the execution.
6. Resume on the service thread, execute the command via service API and retrieve the result.
7. Suspend to transfer the execution.
8. Resume on the I/O thread, serialize the result, initiates the send operation and suspend.
9. Resume upon data transmission
10. Reset state, return to step 2.

The chracteristic of the coroutine offsers several benefits to the above work pattern:

- Suspend during I/O operations to free the thread for other tasks, multiplexing the thread for multiple coroutines, hence multiple connections.
- Transfer the execution between threads seamlessly by suspending on one thread and resuming on other thread without additional synchronization, enabling the service APIs to operate with no locks.

A simplified view of threading model is as follow:
![A simplified threading model](./doc/images/threading-model.svg)

## Benchmark

[TODO]

## Getting Started

Follow the steps to build rdss from source and run:

### Prerequisites (TODO)

- Linux 5.19 or later
- clang-17 or later
- liburing
- xxhash

### Build From Source

Clone the project and build with cmake presets, there are two presets {release, debug} available. The following builds the 'release' version.

```
git clone https://github.com/usurai/rdss
cd rdss
cmake --preset release
cmake --build --preset release
```

### Run

Run rdss either with or without provided configuration.

```
build/release/src/rdss 
# or
build/release/src/rdss rdss.ini
```

### Configuration

rdss provides configurations to tweak the program, see [rdss.ini](./rdss.ini) for the detailed information.

## License

TODO
