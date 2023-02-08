#include "client.h"

#include "buffer.h"

namespace rdss {

Task<void> Client::Echo() {
    Buffer buffer(1024);
    while (true) {
        const auto bytes_read = co_await conn_->Recv(buffer.Sink());
        LOG(INFO) << "read " << bytes_read;
        if (bytes_read == 0) {
            break;
        }
        buffer.Produce(bytes_read);
        auto bytes_written = co_await conn_->Send(std::string(buffer.Source()));
        LOG(INFO) << "written " << bytes_written;
        if (bytes_written == 0) {
            break;
        }
        buffer.Consume(bytes_written);
    }
    OnConnectionClose();
}

} // namespace rdss
