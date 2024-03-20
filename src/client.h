#pragma once

#include "io/connection.h"
#include "io/promise.h"
#include "resp/resp_parser.h"
#include "resp/result.h"

namespace rdss {

class ClientManager;
class DataStructureService;
class RingExecutor;

class Client {
public:
    explicit Client(Connection* conn, ClientManager* manager, DataStructureService* service);

    Task<void> Process(RingExecutor* dss_executor);

    void Close();

private:
    // If 'query_buffer_' is not virtual view, ensure it has enough space for the upcoming recv.
    // Also, if it gets expansion that changes its location, optionally updates existing
    // 'arguments_' to reflext the change.
    void EnsureBuffer();

    // Resets state between queries.
    void ResetState();

    void OnConnectionClose() { delete this; }

private:
    std::unique_ptr<Connection> conn_;

    ClientManager* const manager_;

    DataStructureService* const service_;

    // Might expand before each call to recv() to ensure at least 'kIOGenericBufferSize'(16KB)
    // available. Should be reset after each round of serving to reclaim buffer space.
    Buffer query_buffer_;

    Buffer output_buffer_;

    // View of parsed arguments over 'query_buffer_'. We don't clear it after round of serving to
    // avoid memory gets reclaim / allocate over the turns of serving.
    // TODO: Clear it if memory gets tight.
    StringViews arguments_;

    // Lazily created multi-bulk parser. If it's in error/done state, it will automatically reset
    // upon new call to Parse().
    std::unique_ptr<MultiBulkParser> mbulk_parser_{nullptr};

    Result query_result_;

    // For output string/string array, that is, when reply is like "$6\r\nFOOBAR\r\n", there are 3
    // iovecs_, first being view over "$6\r\n" in 'output_buffer_', second being view over value
    // string shared_ptr in 'Result', the last being "\r\n" that references the same CRLF in the
    // first iovec.
    std::vector<iovec> iovecs_;
};

} // namespace rdss
