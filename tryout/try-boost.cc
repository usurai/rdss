#include <boost/asio.hpp>

#include <iostream>
#include <thread>

using boost::asio::ip::tcp;

class Proxy : public std::enable_shared_from_this<Proxy> {
public:
    Proxy(tcp::socket from, tcp::endpoint& to)
      : from_(std::move(from))
      , to_(from_.get_executor())
      , to_endpoint_(to) {}

    void transfer() {
        auto self = shared_from_this();
        to_.async_connect(to_endpoint_, [self](boost::system::error_code ec) {
            if (!ec) {
                self->read_from(self->from_, self->from_buffer_, self->to_);
                self->read_from(self->to_, self->to_buffer_, self->from_);
            }
        });
    }

    void read_from(tcp::socket& from, std::array<char, 1024>& buf, tcp::socket& to) {
        auto self = shared_from_this();
        from.async_read_some(
          boost::asio::buffer(buf),
          [self, &to, &from, &buf](boost::system::error_code ec, size_t bytes) {
              if (!ec) {
                  // std::cout << "read " << bytes << '\n';
                  self->write_to(to, buf, bytes, from);
              } else {
                  self->stop();
              }
          });
    }

    void write_to(tcp::socket& to, std::array<char, 1024>& buf, size_t bytes, tcp::socket& from) {
        auto self = shared_from_this();
        to.async_write_some(
          boost::asio::buffer(buf, bytes),
          [self, &from, &to, &buf](boost::system::error_code ec, size_t) {
              if (!ec) {
                  // std::cout << "written " << bytes << '\n';
                  self->read_from(from, buf, to);
              } else {
                  self->stop();
              }
          });
    }

    void stop() {
        from_.close();
        to_.close();
    }

private:
    tcp::socket from_;
    tcp::socket to_;
    tcp::endpoint& to_endpoint_;
    std::array<char, 1024> from_buffer_;
    std::array<char, 1024> to_buffer_;
};

void listen(tcp::acceptor& acceptor, tcp::endpoint& target_endpoint) {
    acceptor.async_accept([&](boost::system::error_code ec, tcp::socket client) {
        if (!ec) {
            std::cout << "accepted\n";
            auto proxy = std::make_shared<Proxy>(std::move(client), target_endpoint);
            proxy->transfer();
        }
        listen(acceptor, target_endpoint);
    });
}

int main() {
    try {
        boost::asio::io_context ctx;

        tcp::endpoint listen_endpoint = *tcp::resolver(ctx).resolve(
          "0.0.0.0", "7890", tcp::resolver::passive);
        tcp::acceptor acceptor(ctx, listen_endpoint);

        tcp::endpoint target_endpoint = *tcp::resolver(ctx).resolve("192.168.0.200", "7890");

        listen(acceptor, target_endpoint);

        ctx.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << '\n';
    }

    return 0;
}
