add_executable(StatisticsSessionAuthTest
  StatisticsSessionAuthTest.cpp
)

target_link_libraries(StatisticsSessionAuthTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(StatisticsSessionAuthTest PRIVATE
  ${REPO_ROOT}/src/network
)

gtest_discover_tests(StatisticsSessionAuthTest)
