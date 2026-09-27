#include <gtest/gtest.h>

#include "src/network/CloudSyncPolicy.h"

TEST(CloudSyncPolicy, RequiresHttpsHostWithoutCredentialsOrPath) {
  // httpx:// and bare hostnames are both valid (upload() normalizes the bare
  // form by prepending https://).
  EXPECT_TRUE(CloudSyncPolicy::validServerUrl("https://homemenu.pl"));
  EXPECT_TRUE(CloudSyncPolicy::validServerUrl("https://homemenu.pl:443/"));
  EXPECT_TRUE(CloudSyncPolicy::validServerUrl("dev.lebiblioteq.homemenu.pl"));
  EXPECT_TRUE(CloudSyncPolicy::validServerUrl("homemenu.pl/"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl("http://homemenu.pl"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl("https://user:pass@homemenu.pl"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl("https://homemenu.pl/other"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl("https://homemenu.pl?redirect=elsewhere"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl("https://"));
  EXPECT_FALSE(CloudSyncPolicy::validServerUrl(""));
}

TEST(CloudSyncPolicy, RequiresServerIssuedToken) {
  EXPECT_TRUE(
      CloudSyncPolicy::validToken("lb_reader_0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
  EXPECT_FALSE(CloudSyncPolicy::validToken("lb_reader_0123"));
  EXPECT_FALSE(
      CloudSyncPolicy::validToken("lb_reader_0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"));
  EXPECT_FALSE(
      CloudSyncPolicy::validToken("lb_reader_0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde\r"));
}
