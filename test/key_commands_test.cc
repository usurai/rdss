#include "commands/key_commands.h"
#include "commands/string_commands.h"
#include "commands_test_base.h"

namespace rdss::test {

class KeyCommandsTest : public CommandsTestBase {
protected:
    void SetUp() override {
        CommandsTestBase::SetUp();
        RegisterStringCommands(&service_);
        RegisterKeyCommands(&service_);
    }
};

TEST_F(KeyCommandsTest, DelTest) {
    // DEL multiple keys
    Invoke("MSET k0 v0 k1 v1 k2 v2");
    ExpectInt(Invoke("DEL k0 k1 k2"), 3);

    // DEL the same key
    Invoke("SET k0 v0");
    ExpectInt(Invoke("DEL k0 k0"), 1);

    // DEL expired key returns 0
    Invoke("SET k0 v0 EX 1");
    AdvanceTime(std::chrono::seconds{1});
    ExpectInt(Invoke("DEL k0"), 0);
}

} // namespace rdss::test
