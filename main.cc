#include "config.h"
#include "server.h"

#include <glog/logging.h>

int main([[maybe_unused]] int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_alsologtostderr = 1;
    FLAGS_v = 1;

    if (argc > 2) {
        LOG(ERROR) << "Too many arguments";
        return 1;
    }
    rdss::Config server_config;
    if (argc == 2) {
        server_config.ReadFromFile(argv[1]);
        LOG(INFO) << server_config.ToString();
    }

    rdss::Server s(std::move(server_config));
    s.Run();
    return 0;
}
