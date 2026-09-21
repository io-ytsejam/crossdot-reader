#include <gtest/gtest.h>

#include "StatisticsSessionAuth.h"

namespace {
constexpr const char* OTP = "12345678";
constexpr const char* TOKEN = "00112233445566778899aabbccddeeff";
}  // namespace

TEST(StatisticsSessionAuth, PairsOnceAndAuthorizesBearerToken) {
  StatisticsSessionAuth auth;
  auth.start(OTP, TOKEN);

  EXPECT_EQ(auth.pair(OTP, 1000), StatisticsSessionAuth::PairResult::Success);
  EXPECT_TRUE(auth.isPaired());
  EXPECT_TRUE(auth.authorize("Bearer 00112233445566778899aabbccddeeff"));
  EXPECT_FALSE(auth.authorize("Bearer 00112233445566778899aabbccddeefe"));
  EXPECT_FALSE(auth.authorize("00112233445566778899aabbccddeeff"));
  EXPECT_EQ(auth.pair(OTP, 2000), StatisticsSessionAuth::PairResult::AlreadyPaired);
}

TEST(StatisticsSessionAuth, RateLimitsAttemptsWithoutConsumingCode) {
  StatisticsSessionAuth auth;
  auth.start(OTP, TOKEN);

  EXPECT_EQ(auth.pair("00000000", 1000), StatisticsSessionAuth::PairResult::InvalidCode);
  EXPECT_EQ(auth.pair(OTP, 1500), StatisticsSessionAuth::PairResult::RateLimited);
  EXPECT_EQ(auth.pair(OTP, 2000), StatisticsSessionAuth::PairResult::Success);
}

TEST(StatisticsSessionAuth, ResetInvalidatesPriorSession) {
  StatisticsSessionAuth auth;
  auth.start(OTP, TOKEN);
  ASSERT_EQ(auth.pair(OTP, UINT32_MAX - 500), StatisticsSessionAuth::PairResult::Success);

  auth.start("87654321", "ffeeddccbbaa99887766554433221100");
  EXPECT_FALSE(auth.authorize("Bearer 00112233445566778899aabbccddeeff"));
  EXPECT_EQ(auth.pair("87654321", 10), StatisticsSessionAuth::PairResult::Success);
  EXPECT_TRUE(auth.authorize("Bearer ffeeddccbbaa99887766554433221100"));
}

TEST(StatisticsSessionAuth, RateLimitHandlesMillisRollover) {
  StatisticsSessionAuth auth;
  auth.start(OTP, TOKEN);

  EXPECT_EQ(auth.pair("00000000", UINT32_MAX - 500), StatisticsSessionAuth::PairResult::InvalidCode);
  EXPECT_EQ(auth.pair(OTP, 100), StatisticsSessionAuth::PairResult::RateLimited);
  EXPECT_EQ(auth.pair(OTP, 500), StatisticsSessionAuth::PairResult::Success);
}
