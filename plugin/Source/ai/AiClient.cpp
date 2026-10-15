#include "AiClient.h"

#include <algorithm>

#include "tfcore/preset/PresetSerializer.h"
#include "tfcore/util/Json.h"

namespace toneforge {

namespace {

// A one-shot thread that just runs a std::function<void()> once. Uses
// only the base juce::Thread API (startThread()/run()/stopThread()) -
// deliberately not juce::Thread::launch() or a self-deleting thread
// pattern, both of which either may not exist in every JUCE version this
// project could end up built against, or have real thread-lifetime
// hazards (deleting a juce::Thread object from within its own run() is a
// known-tricky pattern - see AiClient.h's ownership comment for why
// AiClient itself owns these instead).
class RequestThread final : public juce::Thread {
public:
    explicit RequestThread(std::function<void()> task) : juce::Thread("ToneForge AI request"), task_(std::move(task)) {}

    void run() override { task_(); }

private:
    std::function<void()> task_;
};

// Off the message thread AND off the audio thread always - only ever
// called from inside a RequestThread's run(). Performs a blocking HTTP
// POST and returns the raw response body, or an empty optional if the
// request failed outright (backend not running, connection refused,
// timeout) - a genuinely common case (Ollama/the backend simply isn't
// started yet) that must degrade gracefully, not crash.
std::optional<juce::String> httpPost(const juce::String& url, const juce::String& jsonBody, juce::String& errorOut) {
    juce::URL target(url);
    target = target.withPOSTData(jsonBody);

    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                              .withConnectionTimeoutMs(15000)
                              .withExtraHeaders("Content-Type: application/json");

    auto stream = target.createInputStream(options);
    if (!stream) {
        errorOut = "Could not reach the AI backend at " + url +
                   " - is it running? (uvicorn app.main:app --port 8765, see docs/SETUP_WINDOWS.md)";
        return std::nullopt;
    }

    return stream->readEntireStreamAsString();
}

} // namespace

AiClient::AiClient(juce::String baseUrl) : baseUrl_(std::move(baseUrl)) {}

AiClient::~AiClient() {
    // Any request still in flight when the processor is destroyed (plugin
    // instance unloading) is abandoned rather than waited on indefinitely
    // - a local LLM response can legitimately take many seconds, and this
    // destructor must not hang plugin teardown that long. stopThread()
    // with a short timeout just bounds how long we wait before moving on;
    // completion callbacks that fire after this point reference a
    // juce::Component::SafePointer inside AiPanel (see AiPanel.cpp), not
    // this object or a raw pointer to it, so there's nothing left here
    // for a late callback to use unsafely - see docs/THREAD_MODEL.md.
    for (auto& t : threads_) t->stopThread(100);
}

void AiClient::runInBackground(std::function<void()> task) {
    pruneFinishedThreads();

    auto thread = std::make_unique<RequestThread>(std::move(task));
    thread->startThread();
    threads_.push_back(std::move(thread));
}

void AiClient::pruneFinishedThreads() {
    threads_.erase(std::remove_if(threads_.begin(), threads_.end(),
                                   [](const std::unique_ptr<juce::Thread>& t) { return !t->isThreadRunning(); }),
                    threads_.end());
    // erase() destroys each removed RequestThread here, on the message
    // thread - safe because isThreadRunning() == false means run() has
    // already returned, so there is nothing left to stop or join.
}

void AiClient::generate(juce::String prompt, std::function<void(GenerateResult)> onComplete) {
    const juce::String url = baseUrl_ + "/generate";

    runInBackground([url, prompt, onComplete] {
        GenerateResult result;
        try {
            tfcore::json::Value envelope = tfcore::json::Value::makeObject();
            envelope.set("prompt", prompt.toStdString());
            const juce::String body(envelope.dump());

            juce::String error;
            auto response = httpPost(url, body, error);
            if (!response) {
                result.errorMessage = error;
            } else {
                result.preset = tfcore::PresetSerializer::fromJson(response->toStdString());
                result.success = true;
            }
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = juce::String("AI generate request failed: ") + e.what();
        }

        juce::MessageManager::callAsync([onComplete, result] { onComplete(result); });
    });
}

void AiClient::songReference(juce::String query, std::function<void(GenerateResult)> onComplete) {
    const juce::String url = baseUrl_ + "/song-reference";

    runInBackground([url, query, onComplete] {
        GenerateResult result;
        try {
            tfcore::json::Value envelope = tfcore::json::Value::makeObject();
            envelope.set("query", query.toStdString());
            const juce::String body(envelope.dump());

            juce::String error;
            auto response = httpPost(url, body, error);
            if (!response) {
                result.errorMessage = error;
            } else {
                result.preset = tfcore::PresetSerializer::fromJson(response->toStdString());
                result.success = true;
            }
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = juce::String("AI song-reference request failed: ") + e.what();
        }

        juce::MessageManager::callAsync([onComplete, result] { onComplete(result); });
    });
}

void AiClient::modify(tfcore::Preset current, juce::String instruction, std::function<void(GenerateResult)> onComplete) {
    const juce::String url = baseUrl_ + "/modify";

    // tfcore::Preset/ModuleState are plain value types (strings, a map,
    // no atomics, no live IAudioModule/Parameter pointers) so capturing a
    // copy by value into this lambda and running it on another thread is
    // safe - see AiClient.h's class comment.
    runInBackground([url, current, instruction, onComplete] {
        GenerateResult result;
        try {
            tfcore::json::Value envelope = tfcore::json::Value::makeObject();
            envelope.set("preset", tfcore::json::Value::parse(tfcore::PresetSerializer::toJson(current)));
            envelope.set("instruction", instruction.toStdString());
            const juce::String body(envelope.dump());

            juce::String error;
            auto response = httpPost(url, body, error);
            if (!response) {
                result.errorMessage = error;
            } else {
                result.preset = tfcore::PresetSerializer::fromJson(response->toStdString());
                result.success = true;
            }
        } catch (const std::exception& e) {
            result.success = false;
            result.errorMessage = juce::String("AI modify request failed: ") + e.what();
        }

        juce::MessageManager::callAsync([onComplete, result] { onComplete(result); });
    });
}

void AiClient::explain(tfcore::Preset current, std::function<void(ExplainResult)> onComplete) {
    const juce::String url = baseUrl_ + "/explain";

    runInBackground([url, current, onComplete] {
        ExplainResult result;
        try {
            tfcore::json::Value envelope = tfcore::json::Value::makeObject();
            envelope.set("preset", tfcore::json::Value::parse(tfcore::PresetSerializer::toJson(current)));
            const juce::String body(envelope.dump());

            juce::String error;
            auto response = httpPost(url, body, error);
            if (!response) {
                result.text = error;
                result.success = false;
            } else {
                auto parsed = tfcore::json::Value::parse(response->toStdString());
                if (const auto* explanation = parsed.find("explanation")) {
                    result.text = juce::String(explanation->asString());
                    result.success = true;
                } else {
                    result.text = "AI backend response was missing an \"explanation\" field.";
                    result.success = false;
                }
            }
        } catch (const std::exception& e) {
            result.success = false;
            result.text = juce::String("AI explain request failed: ") + e.what();
        }

        juce::MessageManager::callAsync([onComplete, result] { onComplete(result); });
    });
}

void AiClient::checkHealth(std::function<void(HealthResult)> onComplete) {
    const juce::String url = baseUrl_ + "/health";

    runInBackground([url, onComplete] {
        HealthResult result;
        try {
            juce::URL target(url);
            const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                      .withConnectionTimeoutMs(4000);
            auto stream = target.createInputStream(options);

            if (!stream) {
                result.reachable = false;
                result.statusSummary = "AI backend offline";
            } else {
                const auto body = stream->readEntireStreamAsString();
                auto parsed = tfcore::json::Value::parse(body.toStdString());
                result.reachable = true;
                const auto* status = parsed.find("status");
                result.statusSummary = status ? ("AI backend: " + juce::String(status->asString())) : "AI backend: reachable";
            }
        } catch (const std::exception&) {
            // Reached the server but couldn't parse its response - still
            // meaningfully different from "offline" for the UI to show.
            result.reachable = true;
            result.statusSummary = "AI backend: reachable (unexpected response)";
        }

        juce::MessageManager::callAsync([onComplete, result] { onComplete(result); });
    });
}

} // namespace toneforge
