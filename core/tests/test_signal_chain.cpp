#include <memory>
#include <vector>

#include "TestFramework.h"
#include "tfcore/chain/SignalChain.h"
#include "tfcore/dsp/modules/Drive.h"
#include "tfcore/dsp/modules/NoiseGate.h"

using namespace tfcore;

TEST_CASE("SignalChain processes modules in order") {
    std::vector<std::shared_ptr<IAudioModule>> modules;
    auto gate = std::make_shared<NoiseGate>();
    auto drive = std::make_shared<Drive>();

    gate->prepare(ProcessSpec{44100.0, 32, 1});
    gate->parameters().find("threshold_db")->set(-80.0f); // stays open
    drive->prepare(ProcessSpec{44100.0, 32, 1});
    drive->parameters().find("gain_db")->set(0.0f);
    drive->parameters().find("tone")->set(1.0f);
    drive->parameters().find("level_db")->set(-6.0f);

    modules.push_back(std::move(gate));
    modules.push_back(std::move(drive));

    auto chainState = std::make_shared<ChainState>(std::move(modules));
    REQUIRE(chainState->size() == 2);

    SignalChain chain;
    chain.setChain(chainState);

    AudioBuffer buf(1, 32);
    for (int i = 0; i < 32; ++i) buf.channel(0)[i] = 0.1f;
    chain.process(buf);

    // Drive's -6dB level stage should have attenuated the signal.
    REQUIRE(buf.channel(0)[31] < 0.1f);
    REQUIRE(buf.channel(0)[31] > 0.0f);
}

TEST_CASE("SignalChain with no chain set is a safe no-op") {
    SignalChain chain;
    AudioBuffer buf(1, 16);
    for (int i = 0; i < 16; ++i) buf.channel(0)[i] = 0.5f;
    chain.process(buf); // must not crash despite no setChain() call
    REQUIRE_APPROX(buf.channel(0)[0], 0.5f);
}

TEST_CASE("SignalChain swapping to a new ChainState takes effect on the next process call") {
    SignalChain chain;

    std::vector<std::shared_ptr<IAudioModule>> emptyModules;
    chain.setChain(std::make_shared<ChainState>(std::move(emptyModules)));

    AudioBuffer buf(1, 8);
    for (int i = 0; i < 8; ++i) buf.channel(0)[i] = 0.3f;
    chain.process(buf);
    REQUIRE_APPROX(buf.channel(0)[0], 0.3f); // empty chain: untouched

    std::vector<std::shared_ptr<IAudioModule>> withGate;
    auto gate = std::make_shared<NoiseGate>();
    gate->prepare(ProcessSpec{44100.0, 8, 1});
    gate->setBypassed(true); // bypassed so signal still passes through untouched
    withGate.push_back(std::move(gate));
    chain.setChain(std::make_shared<ChainState>(std::move(withGate)));

    chain.process(buf);
    REQUIRE(chain.currentChain()->size() == 1);
}
