#include <cassert>
#include <iostream>
#include <string>
#include "MqttTransport.h"

int main() {
    using namespace xuna::transport;

    const char* devId = "12345678-1234-4234-8234-123456789abc";
    char topicBuf[kMaxTopicLen];
    char payloadBuf[kMaxPayloadLen];

    // 1. Status topic
    assert(buildStatusTopic(topicBuf, sizeof(topicBuf), devId));
    assert(std::string(topicBuf) == "xuna/v1/12345678-1234-4234-8234-123456789abc/status");

    // 2. Events topic
    assert(buildEventsTopic(topicBuf, sizeof(topicBuf), devId));
    assert(std::string(topicBuf) == "xuna/v1/12345678-1234-4234-8234-123456789abc/up/events");

    // 3. State topic
    assert(buildStateTopic(topicBuf, sizeof(topicBuf), devId));
    assert(std::string(topicBuf) == "xuna/v1/12345678-1234-4234-8234-123456789abc/up/state");

    // 4. Cmd topic
    assert(buildCmdTopic(topicBuf, sizeof(topicBuf), devId));
    assert(std::string(topicBuf) == "xuna/v1/12345678-1234-4234-8234-123456789abc/down/cmd");

    // 5. Suggest topic
    assert(buildSuggestTopic(topicBuf, sizeof(topicBuf), devId));
    assert(std::string(topicBuf) == "xuna/v1/12345678-1234-4234-8234-123456789abc/down/suggest");

    // 6. Status payload
    assert(buildStatusPayload(payloadBuf, sizeof(payloadBuf), "online"));
    assert(std::string(payloadBuf) == "{\"status\":\"online\"}");

    // 7. Event payload
    assert(buildEventPayload(payloadBuf, sizeof(payloadBuf), "button_press", 42000));
    assert(std::string(payloadBuf) == "{\"eventType\":\"button_press\",\"timestampMs\":42000}");

    // 8. State payload
    assert(buildStatePayload(payloadBuf, sizeof(payloadBuf), 5, 3600, 12));
    assert(std::string(payloadBuf) == "{\"snapshotGeneration\":5,\"life\":{\"activeSeconds\":3600,\"feedbackCount\":12}}");

    // 9. Boundary checks: buffer too small fails safely
    char tiny[10];
    assert(!buildStatusTopic(tiny, sizeof(tiny), devId));
    assert(!buildStatusPayload(tiny, sizeof(tiny), "online"));

    std::cout << "All MqttTransport C++11 tests passed successfully!" << std::endl;
    return 0;
}
