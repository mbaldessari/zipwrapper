// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <ZipWrapper.h>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
TEST(ZipFile, TestDefault)
{
    zipios::ZipFile zf;
    EXPECT_EQ(zf.isValid(), false);
}

TEST(ZipFile, TestNonExisting)
{
    EXPECT_THROW(zipios::ZipFile("this/file/does/not/exist"), zipios::FCollException);
}

class ZipFileTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::ofstream os("empty.zip", std::ios::out | std::ios::binary);
        os << static_cast<char>(0x50);
        os << static_cast<char>(0x4B);
        os << static_cast<char>(0x05);
        os << static_cast<char>(0x06);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
        os << static_cast<char>(0x00);
    }
    void TearDown() override
    {
        // delete empty.zip
        std::remove("empty.zip");
    }
};

TEST_F(ZipFileTest, TestValid)
{
    zipios::ZipFile zf("empty.zip");
    EXPECT_EQ(zf.isValid(), true);
    EXPECT_EQ(zf.entries().empty(), true);
    EXPECT_EQ(zf.getName(), "empty.zip");
    EXPECT_EQ(zf.size(), 0);
    zf.close();
    EXPECT_EQ(zf.isValid(), false);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
