add_executable(StatisticsImportParserTest
  StatisticsImportParserTest.cpp
  ${REPO_ROOT}/src/StatisticsImportParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(StatisticsImportParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(StatisticsImportParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StatisticsImportParserTest)