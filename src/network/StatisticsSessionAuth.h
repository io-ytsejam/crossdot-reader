#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

class StatisticsSessionAuth {
 public:
  static constexpr size_t OTP_DIGITS = 8;
  static constexpr size_t TOKEN_HEX_DIGITS = 32;
  static constexpr uint32_t ATTEMPT_INTERVAL_MS = 1000;

  enum class PairResult { Success, InvalidCode, RateLimited, AlreadyPaired };

  void start(const char* newOtp, const char* newToken) {
    clear();
    copyFixed(otp, sizeof(otp), newOtp);
    copyFixed(token, sizeof(token), newToken);
  }

  void clear() {
    memset(otp, 0, sizeof(otp));
    memset(token, 0, sizeof(token));
    paired = false;
    hasAttempt = false;
    lastAttemptMs = 0;
  }

  PairResult pair(const std::string_view suppliedOtp, const uint32_t nowMs) {
    if (paired) return PairResult::AlreadyPaired;
    if (hasAttempt && static_cast<uint32_t>(nowMs - lastAttemptMs) < ATTEMPT_INTERVAL_MS) {
      return PairResult::RateLimited;
    }

    hasAttempt = true;
    lastAttemptMs = nowMs;
    if (!secureEquals(suppliedOtp, otp, OTP_DIGITS)) return PairResult::InvalidCode;

    paired = true;
    return PairResult::Success;
  }

  bool authorize(const std::string_view authorization) const {
    constexpr std::string_view prefix = "Bearer ";
    if (!paired || authorization.size() < prefix.size()) return false;
    if (authorization.substr(0, prefix.size()) != prefix) return false;
    return secureEquals(authorization.substr(prefix.size()), token, TOKEN_HEX_DIGITS);
  }

  const char* pairingCode() const { return otp; }
  const char* bearerToken() const { return token; }
  bool isPaired() const { return paired; }

 private:
  char otp[OTP_DIGITS + 1] = {};
  char token[TOKEN_HEX_DIGITS + 1] = {};
  uint32_t lastAttemptMs = 0;
  bool paired = false;
  bool hasAttempt = false;

  static void copyFixed(char* destination, const size_t destinationSize, const char* source) {
    if (!source || destinationSize == 0) return;
    strncpy(destination, source, destinationSize - 1);
    destination[destinationSize - 1] = '\0';
  }

  static bool secureEquals(const std::string_view supplied, const char* expected, const size_t expectedLength) {
    size_t difference = supplied.size() ^ expectedLength;
    for (size_t index = 0; index < expectedLength; ++index) {
      const uint8_t suppliedByte = index < supplied.size() ? static_cast<uint8_t>(supplied[index]) : 0;
      difference |= suppliedByte ^ static_cast<uint8_t>(expected[index]);
    }
    return difference == 0;
  }
};
