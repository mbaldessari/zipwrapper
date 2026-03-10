// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include <memory>
#include <ZipWrapper.h>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
TEST(Collection, TestValidity)
{
    zipios::CollectionCollection cc;
    EXPECT_EQ(cc.isValid(), false);
    EXPECT_EQ(cc.entries().empty(), true);
    EXPECT_EQ(cc.getEntry("inexistant", zipios::FileCollection::MatchPath::MATCH), nullptr);
    EXPECT_EQ(cc.getEntry("inexistant", zipios::FileCollection::MatchPath::IGNORE_PATH), nullptr);
    EXPECT_FALSE(cc.getInputStream("inexistant", zipios::FileCollection::MatchPath::MATCH));
    EXPECT_FALSE(cc.getInputStream("inexistant", zipios::FileCollection::MatchPath::IGNORE_PATH));
    EXPECT_EQ(cc.size(), 0);
    cc.close();
    EXPECT_EQ(cc.isValid(), false);
}

TEST(Collection, TestClone)
{
    zipios::CollectionCollection cc;
    auto pointer = cc.clone();
    EXPECT_EQ(pointer->isValid(), false);
    EXPECT_EQ(pointer->entries().empty(), true);
    EXPECT_EQ(pointer->getEntry("inexistant", zipios::FileCollection::MatchPath::MATCH), nullptr);
    EXPECT_EQ(pointer->getEntry("inexistant", zipios::FileCollection::MatchPath::IGNORE_PATH), nullptr);
    EXPECT_FALSE(pointer->getInputStream("inexistant", zipios::FileCollection::MatchPath::MATCH));
    EXPECT_FALSE(pointer->getInputStream("inexistant", zipios::FileCollection::MatchPath::IGNORE_PATH));
    EXPECT_EQ(pointer->size(), 0);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
