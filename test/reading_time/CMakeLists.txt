add_executable(ReadingTimeTest
  ReadingTimeTest.cpp
)

target_link_libraries(ReadingTimeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingTimeTest)
