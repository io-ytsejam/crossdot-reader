#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "src/network/CloudSyncResponse.h"

TEST(CloudSyncResponse, RejectsOverflowEvenAfterValidPrefix) {
  CloudSyncResponse response;
  response.append("{\"ok\":true}", 11);
  const std::string excess(CloudSyncResponse::MAX_BODY_BYTES, ' ');
  response.append(excess.data(), excess.size());
  response.append(" ", 1);
  EXPECT_FALSE(response.confirmsIngest());
}

TEST(CloudSyncResponse, RejectsOversizedFirstChunkPermanently) {
  CloudSyncResponse response;
  const std::string excess(CloudSyncResponse::MAX_BODY_BYTES + 1, ' ');
  response.append(excess.data(), excess.size());
  response.append("{\"ok\":true}", 11);
  EXPECT_FALSE(response.confirmsIngest());
}

TEST(CloudSyncResponse, AcceptsExactCapacityAndEmptyChunk) {
  CloudSyncResponse response;
  const std::string body = "{\"ok\":true}" + std::string(CloudSyncResponse::MAX_BODY_BYTES - 11, ' ');
  response.append(nullptr, 0);
  response.append(body.data(), body.size());
  response.append(nullptr, 0);
  EXPECT_TRUE(response.confirmsIngest());
}

TEST(CloudSyncResponse, RejectsNullNonemptyChunk) {
  CloudSyncResponse response;
  response.append(nullptr, 1);
  response.append("{\"ok\":true}", 11);
  EXPECT_FALSE(response.confirmsIngest());
}

TEST(CloudSyncResponse, RejectsNonJsonSyntax) {
  const std::string_view malformed[] = {
      "{ok:true}",
      "{'ok':true}",
      "{\"ok\":true,\"extra\":+1}",
      "{\"ok\":true,\"extra\":01}",
      "{\"ok\":true,\"extra\":1.}",
      "{\"ok\":true,\"extra\":.1}",
      "{\"ok\":true,\"extra\":\"raw\nnewline\"}",
      "{\"ok\":true,\"extra\":\"bad\\xescape\"}",
      "{\"ok\":true,true:1}",
  };
  for (const auto body : malformed) {
    CloudSyncResponse response;
    response.append(body.data(), body.size());
    EXPECT_FALSE(response.confirmsIngest()) << body;
  }
}

TEST(CloudSyncResponse, ParsesBooleanTopLevelConfirmation) {
  struct Example {
    std::string_view body;
    bool accepted;
  };
  const Example examples[] = {
      {"{\"ok\": true}", true},
      {" \r\n{\"count\":2,\"ok\" : true,\"extra\":{\"x\":[1,null]}}\t ", true},
      {"{\"\\u006fk\":true}", true},
      {R"({"ok":true,"extra":[0,-1,0.5,-2.4e+3,1E-2,false,"quote\"slash\\\n"]})", true},
      {"", false},
      {"{}", false},
      {"{\"ok\":false}", false},
      {"{\"ok\":\"true\"}", false},
      {"{\"ok\":1}", false},
      {"{\"nested\":{\"ok\":true}}", false},
      {"[{\"ok\":true}]", false},
      {"{\"ok\":true", false},
      {"{\"ok\":true,}", false},
      {"{\"ok\":true}garbage", false},
      {"{\"ok\":true}{\"ok\":false}", false},
      {std::string_view("{\"ok\":true}\0junk", 16), false},
  };
  for (const auto& example : examples) {
    CloudSyncResponse response;
    response.append(example.body.data(), example.body.size());
    EXPECT_EQ(response.confirmsIngest(), example.accepted) << example.body;
  }
}

TEST(CloudSyncResponse, CollectsBodyDuringPerformAcrossChunks) {
  CloudSyncResponse response;
  // perform() delivers body chunks before returning; no subsequent read is needed.
  response.append("{\"o", 3);
  response.append("k\":tr", 5);
  response.append("ue}", 3);
  EXPECT_TRUE(response.confirmsIngest());
}

TEST(CloudSyncResponse, AcceptsEveryChunkBoundary) {
  constexpr std::string_view body = R"({"ok": true,"extra":"escaped\"value"})";
  for (size_t split = 0; split <= body.size(); ++split) {
    CloudSyncResponse response;
    response.append(body.data(), split);
    response.append(body.data() + split, body.size() - split);
    EXPECT_TRUE(response.confirmsIngest()) << split;
  }
  CloudSyncResponse bytewise;
  for (const char& c : body) bytewise.append(&c, 1);
  EXPECT_TRUE(bytewise.confirmsIngest());
}
