#include "base/config.h"
#include "server.h"
#include <cstring>

#include <glog/logging.h>

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    const auto non_flag_index = google::ParseCommandLineFlags(&argc, &argv, true);
    FLAGS_alsologtostderr = 1;

    rdss::Config server_config;
    if (argv[non_flag_index] != nullptr) {
        server_config.ReadFromFile(argv[non_flag_index]);
        LOG(INFO) << server_config.ToString();
    }

    rdss::Server s(std::move(server_config));
    s.Run();
    return 0;
}
