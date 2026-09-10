add_executable(OpdsParserTest
  OpdsParserTest.cpp
  ${REPO_ROOT}/lib/OpdsParser/OpdsParser.cpp
)

target_include_directories(OpdsParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/OpdsParser
  ${REPO_ROOT}/lib/XmlParserUtils
)

target_link_libraries(OpdsParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  expat
)

gtest_discover_tests(OpdsParserTest)
