#include "TestFramework.h"
#include "tfcore/util/Json.h"

using namespace tfcore::json;

TEST_CASE("Json round-trips primitive types") {
    Value obj = Value::makeObject();
    obj.set("name", "Cliffs of Dover");
    obj.set("gain", 7.5);
    obj.set("bypassed", false);
    obj.set("nothing", nullptr);

    std::string text = obj.dump(0);
    Value parsed = Value::parse(text);

    REQUIRE(parsed.find("name")->asString() == "Cliffs of Dover");
    REQUIRE(tf_approx(parsed.find("gain")->asNumber(), 7.5));
    REQUIRE(parsed.find("bypassed")->asBool() == false);
    REQUIRE(parsed.find("nothing")->isNull());
}

TEST_CASE("Json round-trips nested arrays and objects") {
    Value root = Value::makeObject();
    Value chain = Value::makeArray();

    Value module1 = Value::makeObject();
    module1.set("moduleId", "noise_gate.v1");
    module1.set("bypassed", false);
    chain.push(module1);

    Value module2 = Value::makeObject();
    module2.set("moduleId", "drive.v1");
    module2.set("bypassed", true);
    chain.push(module2);

    root.set("chain", chain);

    Value parsed = Value::parse(root.dump(2));
    const auto& parsedChain = parsed.find("chain")->asArray();
    REQUIRE(parsedChain.size() == 2);
    REQUIRE(parsedChain[0].find("moduleId")->asString() == "noise_gate.v1");
    REQUIRE(parsedChain[1].find("bypassed")->asBool() == true);
}

TEST_CASE("Json escapes and unescapes special characters in strings") {
    Value obj = Value::makeObject();
    obj.set("description", "Bright \"lead\" tone\nwith a tab\there");

    Value parsed = Value::parse(obj.dump(0));
    REQUIRE(parsed.find("description")->asString() == "Bright \"lead\" tone\nwith a tab\there");
}

TEST_CASE("Json parse throws on malformed input") {
    bool threw = false;
    try {
        Value::parse("{ this is not json ");
    } catch (const std::exception&) {
        threw = true;
    }
    REQUIRE(threw);
}

TEST_CASE("Json handles empty objects and arrays") {
    Value root = Value::makeObject();
    root.set("emptyObj", Value::makeObject());
    root.set("emptyArr", Value::makeArray());

    Value parsed = Value::parse(root.dump(2));
    REQUIRE(parsed.find("emptyObj")->asObject().empty());
    REQUIRE(parsed.find("emptyArr")->asArray().empty());
}
