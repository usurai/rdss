#include "server.h"

int main([[maybe_unused]] int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_alsologtostderr = 1;
    FLAGS_v = 1;

    rdss::Server s;
    s.Run();
    return 0;
}
