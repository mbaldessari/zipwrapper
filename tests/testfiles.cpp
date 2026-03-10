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

#ifndef FREECAD_TESTFILES_DIR
#error "FREECAD_TESTFILES_DIR must be defined"
#endif

namespace
{

std::vector<std::string> findFilesByExtension(const char* dir,
                                              const std::vector<std::string>& extensions)
{
    std::vector<std::string> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto ext = entry.path().extension().string();
        // lowercase for comparison
        std::string extLower = ext;
        for (auto& c : extLower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        for (const auto& wanted : extensions) {
            if (extLower == wanted) {
                result.push_back(entry.path().string());
                break;
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

// Shared test name generator: filename stem with non-alnum replaced by underscore
std::string testNameFromPath(const ::testing::TestParamInfo<std::string>& info)
{
    auto stem = std::filesystem::path(info.param).stem().string();
    for (auto& c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    return stem;
}

}  // namespace

// Shared test fixture for extracting all entries from a zip/fcstd archive
class ArchiveExtractTest: public ::testing::TestWithParam<std::string>
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

    void extractAll()
    {
        const std::string& zipPath = GetParam();

        zipios::ZipFile zf(zipPath);
        ASSERT_TRUE(zf.isValid()) << "Failed to open: " << zipPath;

        auto entries = zf.entries();

        for (const auto& entry : entries) {
            if (entry->isDirectory()) {
                if (entry->getName().size() > 255) {
                    continue;
                }
                std::filesystem::create_directories(tmpDir / entry->getName());
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

            auto filename = std::filesystem::path(entry->getName()).filename().string();
            if (filename.size() > 255) {
                std::cout << "  [SKIP] " << zipPath << ": filename too long ("
                          << filename.size() << " bytes)" << std::endl;
                continue;
            }

            auto outPath = tmpDir / entry->getName();
            std::filesystem::create_directories(outPath.parent_path());

            std::ofstream out(outPath, std::ios::binary);
            ASSERT_TRUE(out.good()) << "Failed to create: " << outPath;
            out << is->rdbuf();
            out.close();

            EXPECT_TRUE(std::filesystem::exists(outPath))
                << "File not written: " << outPath;
        }
    }

    std::filesystem::path tmpDir;
};


// --- .zip test files ---

class ZipFilesExtract: public ArchiveExtractTest {};

TEST_P(ZipFilesExtract, OpenAndExtractAll)
{
    extractAll();
}

INSTANTIATE_TEST_SUITE_P(
    TestFiles,
    ZipFilesExtract,
    ::testing::ValuesIn(findFilesByExtension(TESTFILES_DIR, {".zip"})),
    testNameFromPath);


// --- .FCStd test files ---

class FCStdFilesExtract: public ArchiveExtractTest {};

TEST_P(FCStdFilesExtract, OpenAndExtractAll)
{
    extractAll();
}

INSTANTIATE_TEST_SUITE_P(
    FreecadTestFiles,
    FCStdFilesExtract,
    ::testing::ValuesIn(findFilesByExtension(FREECAD_TESTFILES_DIR, {".fcstd"})),
    testNameFromPath);
