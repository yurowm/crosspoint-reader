add_executable(ReadingStatsCalendarTest
  ReadingStatsCalendarTest.cpp
  ${REPO_ROOT}/src/ReadingStatsCalendar.cpp
)

target_include_directories(ReadingStatsCalendarTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(ReadingStatsCalendarTest PRIVATE GTest::gtest_main crosspoint_test_common)

gtest_discover_tests(ReadingStatsCalendarTest)
