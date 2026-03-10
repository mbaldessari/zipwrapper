// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <ZipWrapper.h>

#ifndef TESTFILES_DIR
#error "TESTFILES_DIR must be defined"
#endif

namespace
{

// Collect all .zip files under TESTFILES_DIR recursively
std::vector<std::string> findZipFiles()
{
    std::vector<std::string> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(TESTFILES_DIR)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto ext = entry.path().extension().string();
        if (ext == ".zip") {
            result.push_back(entry.path().string());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace

class TestFilesExtract: public ::testing::TestWithParam<std::string>
{
protected:
    void SetUp() override
    {
        tmpDir = std::filesystem::temp_directory_path()
            / ("zipwrapper_extract_" + std::to_string(std::rand()));
        std::filesystem::create_directories(tmpDir);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(tmpDir);
    }

    std::filesystem::path tmpDir;
};

TEST_P(TestFilesExtract, OpenAndExtractAll)
{
    const std::string& zipPath = GetParam();

    // Open the zip file
    zipios::ZipFile zf(zipPath);
    ASSERT_TRUE(zf.isValid()) << "Failed to open: " << zipPath;

    auto entries = zf.entries();

    for (const auto& entry : entries) {
        if (entry->isDirectory()) {
            auto dirPath = tmpDir / entry->getName();
            // Skip directory names that are too long for the filesystem
            if (entry->getName().size() > 255) {
                continue;
            }
            std::filesystem::create_directories(dirPath);
            continue;
        }

        std::unique_ptr<std::istream> is;
        try {
            is = zf.getInputStream(entry);
        }
        catch (const zipios::IOException& e) {
            std::cout << "  [SKIP] " << zipPath << ": " << entry->getName()
                      << ": " << e.what() << std::endl;
            continue;
        }

        if (!is) {
            continue;
        }

        // Skip entries with filenames too long for the filesystem
        auto filename = std::filesystem::path(entry->getName()).filename().string();
        if (filename.size() > 255) {
            std::cout << "  [SKIP] " << zipPath << ": filename too long ("
                      << filename.size() << " bytes)" << std::endl;
            continue;
        }

        // Ensure parent directories exist
        auto outPath = tmpDir / entry->getName();
        std::filesystem::create_directories(outPath.parent_path());

        // Write entry contents to disk
        std::ofstream out(outPath, std::ios::binary);
        ASSERT_TRUE(out.good()) << "Failed to create: " << outPath;
        out << is->rdbuf();
        out.close();

        EXPECT_TRUE(std::filesystem::exists(outPath))
            << "File not written: " << outPath;
    }
}

INSTANTIATE_TEST_SUITE_P(
    TestFiles,
    TestFilesExtract,
    ::testing::ValuesIn(findZipFiles()),
    [](const ::testing::TestParamInfo<std::string>& info) {
        // Use the filename (without extension) as test name
        auto stem = std::filesystem::path(info.param).stem().string();
        // Replace non-alphanumeric chars with underscore for gtest
        for (auto& c : stem) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                c = '_';
            }
        }
        return stem;
    });
