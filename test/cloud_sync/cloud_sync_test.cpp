#include <gtest/gtest.h>

#include "src/network/CloudSyncPolicy.h"

TEST(CloudSyncPolicy, RequiresHttpsHostWithoutCredentialsOrPath) {
  EXPECT_TRUE(CloudSyncPolicy::validServerUrl("https://homemenu.pl"));
  EXPECT_TRUE(CloudSyncPolicy::validServerUrl("https://homemenu.pl:443/"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl("http://homemenu.pl"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl("https://user:pass@homemenu.pl"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl("https://homemenu.pl/other"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl("https://homemenu.pl?redirect=elsewhere"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl("https://"));
}

TEST(CloudSyncPolicy, RequiresServerIssuedToken) {
  EXPECT_TRUE(CloudSyncPolicy::validToken("lb_reader_0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  EXPECT_FALSE(CloudSyncPolicy::validToken("lb_reader_0123"));
  EXPECT_FALSE(CloudSyncPolicy::validToken("lb_reader_0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"));
  EXPECT_FALSE(CloudSyncPolicy::validToken("lb_reader_0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\r"));
}

TEST(CloudSyncPolicy, AttemptsImmediatelyOnWakeAndAfterNewSession) {
  EXPECT_TRUE(CloudSyncPolicy::due(1000, 0, false, false));
  EXPECT_FALSE(CloudSyncPolicy::due(1000, 1000, false, false));
  EXPECT_TRUE(CloudSyncPolicy::due(1001, 1000, true, false));
  EXPECT_FALSE(CloudSyncPolicy::due(1001, 1000, false, true));
  EXPECT_TRUE(CloudSyncPolicy::due(1000 + CloudSyncPolicy::RETRY_MS, 1000, false, true));
  EXPECT_TRUE(CloudSyncPolicy::due(1000 + CloudSyncPolicy::INTERVAL_MS, 1000, false, false));
  EXPECT_TRUE(CloudSyncPolicy::due(0x00040000U, 0xffff0000U, false, true)); // wraparound
}
