#include "varn/xml/XmlSerializer.h"

#include <gtest/gtest.h>

#include <cctype>
#include <string>

namespace varn::xml {

TEST(XmlSanitizeName, ReplacesUnsafeCharacters) {
    EXPECT_EQ(XmlSerializer::sanitizeElementName("a b"), "a_b");
    EXPECT_EQ(XmlSerializer::sanitizeElementName("<script>"), "_script_");
    EXPECT_EQ(XmlSerializer::sanitizeElementName("a\"/>b"), "a___b");
}

TEST(XmlSanitizeName, GivesEmptyAndDigitLeadingNamesSafeForms) {
    EXPECT_FALSE(XmlSerializer::sanitizeElementName("").empty());
    const std::string fromDigit = XmlSerializer::sanitizeElementName("1abc");
    ASSERT_FALSE(fromDigit.empty());
    EXPECT_FALSE(std::isdigit(static_cast<unsigned char>(fromDigit.front())));
}

} // namespace varn::xml
