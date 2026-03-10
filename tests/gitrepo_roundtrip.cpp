// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <ZipWrapper.h>

// This test zips the current git working tree into a temporary archive,
// extracts every entry back out, and verifies byte-for-byte equality
// with the originals on disk (excludes .git dir itself)

namespace fs = std::filesystem;

namespace
{

// Get the repo root by looking for .git starting from SOURCE_DIR
std::string repoRoot()
{
    fs::path dir = SOURCE_DIR;  // set via CMake compile definition
    while (!dir.empty()) {
        if (fs::exists(dir / ".git")) {
            return dir.string();
        }
        auto parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return SOURCE_DIR;
}

// Read an entire file into a string (binary mode)
std::string readFileContents(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::vector<std::string> collectFiles(const std::string& root)
{
    std::vector<std::string> result;

    // Top-level text files
    for (const auto& name : {"CMakeLists.txt", "README.md", ".gitignore", ".gitmodules"}) {
        auto p = fs::path(root) / name;
        if (fs::is_regular_file(p)) {
            result.emplace_back(name);
        }
    }

    for (const auto& dir : {"src", "tests", "testfiles", "freecadtestfiles"}) {
        auto dirPath = fs::path(root) / dir;
        if (!fs::is_directory(dirPath)) {
            continue;
        }
        for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
            if (entry.is_regular_file()) {
                result.push_back(fs::relative(entry.path(), root).string());
            }
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace


class GitRepoRoundTrip : public ::testing::Test
{
protected:
    void SetUp() override
    {
        root = repoRoot();
        tmpDir = fs::temp_directory_path()
                 / ("zw_repo_test_" + std::to_string(std::rand()));
        fs::create_directories(tmpDir);
        zipPath = (tmpDir / "repo.zip").string();
    }
    void TearDown() override
    {
        fs::remove_all(tmpDir);
    }

    std::string root;
    fs::path tmpDir;
    std::string zipPath;
};

TEST_F(GitRepoRoundTrip, ZipAndVerify)
{
    auto files = collectFiles(root);
    ASSERT_FALSE(files.empty()) << "No files found in repo root: " << root;

    // --- Create the zip ---
    {
        zipios::ZipOutputStream zos(zipPath);
        for (const auto& relPath : files) {
            std::string absPath = (fs::path(root) / relPath).string();
            std::string content = readFileContents(absPath);

            zos.putNextEntry(relPath);
            zos.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        zos.close();
    }

    // --- Open the zip and verify ---
    zipios::ZipFile zf(zipPath);
    EXPECT_TRUE(zf.isValid());
    EXPECT_EQ(zf.size(), files.size());

    for (const auto& relPath : files) {
        auto entry = zf.getEntry(relPath);
        ASSERT_NE(entry, nullptr) << "Missing zip entry: " << relPath;

        auto is = zf.getInputStream(entry);
        ASSERT_NE(is, nullptr) << "No stream for: " << relPath;

        std::string fromZip((std::istreambuf_iterator<char>(*is)),
                             std::istreambuf_iterator<char>());

        std::string absPath = (fs::path(root) / relPath).string();
        std::string fromDisk = readFileContents(absPath);

        EXPECT_EQ(fromZip.size(), fromDisk.size())
            << "Size mismatch for: " << relPath;
        EXPECT_EQ(fromZip, fromDisk)
            << "Content mismatch for: " << relPath;
    }
}

TEST_F(GitRepoRoundTrip, ZipAndVerifyViaZipInputStream)
{
    auto files = collectFiles(root);
    ASSERT_FALSE(files.empty());

    {
        zipios::ZipOutputStream zos(zipPath);
        for (const auto& relPath : files) {
            std::string absPath = (fs::path(root) / relPath).string();
            std::string content = readFileContents(absPath);

            zos.putNextEntry(relPath);
            zos.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        zos.close();
    }

    zipios::ZipInputStream zis(zipPath);

    // First entry is auto-loaded by constructor; verify it
    std::string firstContent((std::istreambuf_iterator<char>(zis)),
                              std::istreambuf_iterator<char>());

    std::string firstAbsPath = (fs::path(root) / files[0]).string();
    std::string firstFromDisk = readFileContents(firstAbsPath);
    EXPECT_EQ(firstContent, firstFromDisk)
        << "Content mismatch for first entry: " << files[0];

    // Iterate remaining entries
    for (size_t i = 1; i < files.size(); ++i) {
        auto entry = zis.getNextEntry();
        ASSERT_NE(entry, nullptr) << "Null entry at index " << i;

        std::string entryName = entry->getName();
        std::string content((std::istreambuf_iterator<char>(zis)),
                             std::istreambuf_iterator<char>());

        std::string absPath = (fs::path(root) / entryName).string();
        std::string fromDisk = readFileContents(absPath);

        EXPECT_EQ(content, fromDisk)
            << "Content mismatch for: " << entryName;
    }
}
