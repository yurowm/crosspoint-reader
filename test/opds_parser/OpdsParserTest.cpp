#include <gtest/gtest.h>

#include <OpdsParser.h>

#include <cstring>

TEST(OpdsParser, CapturesBookIdentityTimestampAndNextPage) {
  constexpr const char* xml = R"xml(<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <link rel="next" href="/calibre/opds/navcatalog/all?offset=30" type="application/atom+xml"/>
  <entry>
    <title>Example</title><author><name>A. Writer</name></author>
    <id>urn:uuid:book-42</id><updated>2026-09-08T17:12:11+00:00</updated>
    <link rel="http://opds-spec.org/acquisition" type="application/epub+zip"
          href="/calibre/get/epub/42/ebooks"/>
  </entry>
</feed>)xml";

  OpdsParser parser;
  EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(xml), std::strlen(xml)), std::strlen(xml));
  parser.flush();

  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1U);
  const auto& book = parser.getEntries().front();
  EXPECT_EQ(book.type, OpdsEntryType::BOOK);
  EXPECT_EQ(book.id, "urn:uuid:book-42");
  EXPECT_EQ(book.updated, "2026-09-08T17:12:11+00:00");
  EXPECT_EQ(book.href, "/calibre/get/epub/42/ebooks");
  EXPECT_EQ(parser.getNextPageUrl(), "/calibre/opds/navcatalog/all?offset=30");
}
