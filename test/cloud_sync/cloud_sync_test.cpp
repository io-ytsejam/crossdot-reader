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

TEST(CloudSyncPolicy, StopsWaitingForDefinitiveWifiJoinFailures) {
  EXPECT_TRUE(CloudSyncPolicy::terminalWifiJoinFailure(2));    // authentication expired
  EXPECT_TRUE(CloudSyncPolicy::terminalWifiJoinFailure(15));   // 4-way handshake timeout
  EXPECT_TRUE(CloudSyncPolicy::terminalWifiJoinFailure(201));  // no AP found
  EXPECT_TRUE(CloudSyncPolicy::terminalWifiJoinFailure(202));  // authentication failed
  EXPECT_TRUE(CloudSyncPolicy::terminalWifiJoinFailure(203));  // association failed
  EXPECT_TRUE(CloudSyncPolicy::terminalWifiJoinFailure(204));  // handshake timeout
  EXPECT_TRUE(CloudSyncPolicy::terminalWifiJoinFailure(205));  // connection failed
  EXPECT_TRUE(CloudSyncPolicy::terminalWifiJoinFailure(210));  // incompatible security
  EXPECT_TRUE(CloudSyncPolicy::terminalWifiJoinFailure(212));  // below RSSI threshold

  EXPECT_FALSE(CloudSyncPolicy::terminalWifiJoinFailure(8));    // deliberate disconnect
  EXPECT_FALSE(CloudSyncPolicy::terminalWifiJoinFailure(200));  // transient beacon timeout
}

TEST(CloudSyncPolicy, BoundsTheEntireBootWifiJoin) { EXPECT_LE(CloudSyncPolicy::BOOT_WIFI_TIMEOUT_MS, 8000U); }

TEST(CloudSyncPolicy, StartsOnlyAfterFiveSecondsOnAStaticReaderPage) {
  EXPECT_FALSE(CloudSyncPolicy::readerIdleForSync(4999, 0, true, false));
  EXPECT_TRUE(CloudSyncPolicy::readerIdleForSync(5000, 0, true, false));
  EXPECT_FALSE(CloudSyncPolicy::readerIdleForSync(6000, 0, false, false));
  EXPECT_FALSE(CloudSyncPolicy::readerIdleForSync(6000, 0, true, true));
}

TEST(CloudSyncPolicy, ReaderIdleCheckHandlesMillisRollover) {
  EXPECT_TRUE(CloudSyncPolicy::readerIdleForSync(3000U, UINT32_MAX - 2999U, true, false));
}

TEST(CloudSyncPolicy, BoundsTheUnresponsiveHttpsWindow) { EXPECT_LE(CloudSyncPolicy::HTTP_TIMEOUT_MS, 10U * 1000U); }

// A configured device that has not synced yet this boot must never be labelled
// "Sync is off": the worker's status atomic still holds its initial value.
TEST(CloudSyncPolicy, ConfiguredDeviceIsNotReportedAsOff) {
  const auto fresh = CloudSyncPolicy::resolveStatusDisplay(true, 0);
  EXPECT_FALSE(fresh.disabled);
  EXPECT_EQ(fresh.lastSyncedAt, 0);

  const auto synced = CloudSyncPolicy::resolveStatusDisplay(true, 1750000000);
  EXPECT_FALSE(synced.disabled);
  EXPECT_EQ(synced.lastSyncedAt, 1750000000);
}

TEST(CloudSyncPolicy, UnconfiguredDeviceIsReportedAsOff) {
  EXPECT_TRUE(CloudSyncPolicy::resolveStatusDisplay(false, 0).disabled);
}
