// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <zlib.h>
#include <ZipWrapper.h>
#include <ZipHeader.h>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)


TEST(FileEntry, DefaultIsInvalid)
{
    zipios::FileEntry entry;
    EXPECT_FALSE(entry.isValid());
    EXPECT_TRUE(entry.getName().empty());
    EXPECT_EQ(entry.getIndex(), -1);
}

TEST(FileEntry, ConstructedWithName)
{
    zipios::FileEntry entry("path/to/file.txt", 5);
    EXPECT_TRUE(entry.isValid());
    EXPECT_EQ(entry.getName(), "path/to/file.txt");
    EXPECT_EQ(entry.getFileName(), "path/to/file.txt");
    EXPECT_EQ(entry.getIndex(), 5);
    EXPECT_EQ(entry.toString(), "path/to/file.txt");
    EXPECT_FALSE(entry.isDirectory());
}

TEST(FileEntry, DirectoryDetection)
{
    zipios::FileEntry dir("some/directory/", 0);
    EXPECT_TRUE(dir.isDirectory());

    zipios::FileEntry file("some/file.txt", 1);
    EXPECT_FALSE(file.isDirectory());

    zipios::FileEntry empty;
    EXPECT_FALSE(empty.isDirectory());
}

TEST(FileEntry, DefaultIndex)
{
    zipios::FileEntry entry("name.txt");
    EXPECT_EQ(entry.getIndex(), -1);
    EXPECT_TRUE(entry.isValid());
}


TEST(ZipCDirEntry, ConstructFromString)
{
    zipios::ZipCDirEntry entry("archive/content.xml");
    EXPECT_TRUE(entry.isValid());
    EXPECT_EQ(entry.getName(), "archive/content.xml");
}

TEST(ZipCDirEntry, DefaultIsInvalid)
{
    zipios::ZipCDirEntry entry;
    EXPECT_FALSE(entry.isValid());
}


namespace
{
std::string createTempDir()
{
    auto base = std::filesystem::temp_directory_path()
        / ("fc_zip_test_" + std::to_string(std::rand()));
    std::filesystem::create_directories(base);
    return base.string();
}

// Write a zip file with the given entries (name -> content)
void writeZipFile(const std::string& path,
                  const std::vector<std::pair<std::string, std::string>>& entries,
                  int level = Z_DEFAULT_COMPRESSION)
{
    zipios::ZipOutputStream out(path);
    out.setLevel(level);
    for (const auto& [name, content] : entries) {
        out.putNextEntry(name);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    out.close();
}

// Write a zip to an ostream with the given entries
void writeZipToStream(std::ostream& os,
                      const std::vector<std::pair<std::string, std::string>>& entries,
                      int level = Z_DEFAULT_COMPRESSION)
{
    zipios::ZipOutputStream out(os);
    out.setLevel(level);
    for (const auto& [name, content] : entries) {
        out.putNextEntry(name);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }
    out.close();
}
}  // namespace



class ZipRoundTripTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tmpDir = createTempDir();
        zipPath = tmpDir + "/test.zip";
    }
    void TearDown() override
    {
        std::filesystem::remove_all(tmpDir);
    }

    std::string tmpDir;
    std::string zipPath;
};

TEST_F(ZipRoundTripTest, SingleEntry)
{
    std::string content = "Hello, World!";
    writeZipFile(zipPath, {{"greeting.txt", content}});

    zipios::ZipFile zf(zipPath);
    EXPECT_TRUE(zf.isValid());
    EXPECT_EQ(zf.size(), 1);

    auto entries = zf.entries();
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0]->getName(), "greeting.txt");
    EXPECT_FALSE(entries[0]->isDirectory());

    auto is = zf.getInputStream("greeting.txt");
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, content);
}

TEST_F(ZipRoundTripTest, MultipleEntries)
{
    std::vector<std::pair<std::string, std::string>> entries = {
        {"file1.txt", "Content of file 1"},
        {"subdir/file2.xml", "<root><child/></root>"},
        {"file3.dat", "Third file data"},
    };
    writeZipFile(zipPath, entries);

    zipios::ZipFile zf(zipPath);
    EXPECT_TRUE(zf.isValid());
    EXPECT_EQ(zf.size(), 3);

    for (const auto& [name, content] : entries) {
        auto entry = zf.getEntry(name);
        ASSERT_NE(entry, nullptr) << "Entry not found: " << name;
        EXPECT_EQ(entry->getName(), name);

        auto is = zf.getInputStream(name);
        ASSERT_NE(is, nullptr) << "No stream for: " << name;
        std::string readBack((std::istreambuf_iterator<char>(*is)),
                              std::istreambuf_iterator<char>());
        EXPECT_EQ(readBack, content) << "Content mismatch for: " << name;
    }
}

TEST_F(ZipRoundTripTest, BinaryContent)
{
    std::string binary;
    for (int i = 0; i < 256; ++i) {
        binary.push_back(static_cast<char>(i));
    }
    // Repeat to make it larger
    std::string content;
    for (int i = 0; i < 100; ++i) {
        content += binary;
    }

    writeZipFile(zipPath, {{"binary.bin", content}});

    zipios::ZipFile zf(zipPath);
    auto is = zf.getInputStream("binary.bin");
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack.size(), content.size());
    EXPECT_EQ(readBack, content);
}

TEST_F(ZipRoundTripTest, EmptyEntry)
{
    writeZipFile(zipPath, {{"empty.txt", ""}});

    zipios::ZipFile zf(zipPath);
    EXPECT_EQ(zf.size(), 1);

    auto is = zf.getInputStream("empty.txt");
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)),
                          std::istreambuf_iterator<char>());
    EXPECT_TRUE(readBack.empty());
}

TEST_F(ZipRoundTripTest, StoreUncompressed)
{
    std::string content = "This data should be stored without compression";
    writeZipFile(zipPath, {{"stored.txt", content}}, 0);

    zipios::ZipFile zf(zipPath);
    auto is = zf.getInputStream("stored.txt");
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, content);
}

TEST_F(ZipRoundTripTest, MaxCompression)
{
    // Highly compressible content
    std::string content(10000, 'A');
    writeZipFile(zipPath, {{"compressed.txt", content}}, Z_BEST_COMPRESSION);

    zipios::ZipFile zf(zipPath);
    auto is = zf.getInputStream("compressed.txt");
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, content);
}

TEST_F(ZipRoundTripTest, PutNextEntryWithZipCDirEntry)
{
    zipios::ZipOutputStream out(zipPath);
    out.putNextEntry(zipios::ZipCDirEntry("via_cdirentry.txt"));
    std::string content = "Written via ZipCDirEntry";
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();

    zipios::ZipFile zf(zipPath);
    EXPECT_EQ(zf.size(), 1);
    auto is = zf.getInputStream("via_cdirentry.txt");
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, content);
}

TEST_F(ZipRoundTripTest, CloseEntryExplicitly)
{
    zipios::ZipOutputStream out(zipPath);
    out.putNextEntry("first.txt");
    out << "First";
    out.closeEntry();
    out.putNextEntry("second.txt");
    out << "Second";
    out.closeEntry();
    out.close();

    zipios::ZipFile zf(zipPath);
    EXPECT_EQ(zf.size(), 2);

    auto is1 = zf.getInputStream("first.txt");
    ASSERT_NE(is1, nullptr);
    std::string r1((std::istreambuf_iterator<char>(*is1)), std::istreambuf_iterator<char>());
    EXPECT_EQ(r1, "First");

    auto is2 = zf.getInputStream("second.txt");
    ASSERT_NE(is2, nullptr);
    std::string r2((std::istreambuf_iterator<char>(*is2)), std::istreambuf_iterator<char>());
    EXPECT_EQ(r2, "Second");
}

TEST_F(ZipRoundTripTest, FinishEqualsClose)
{
    zipios::ZipOutputStream out(zipPath);
    out.putNextEntry("data.txt");
    out << "via finish";
    out.finish();

    zipios::ZipFile zf(zipPath);
    EXPECT_EQ(zf.size(), 1);
    auto is = zf.getInputStream("data.txt");
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)), std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, "via finish");
}



TEST_F(ZipRoundTripTest, WriteToOstream)
{
    std::ostringstream oss(std::ios::binary);
    writeZipToStream(oss, {{"streamed.txt", "Stream content"}});

    // Read back via ZipInputStream from the stringstream
    std::string zipData = oss.str();
    EXPECT_FALSE(zipData.empty());

    std::istringstream iss(zipData, std::ios::binary);
    zipios::ZipInputStream zis(iss);

    // First entry is auto-loaded by constructor
    std::string readBack((std::istreambuf_iterator<char>(zis)), std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, "Stream content");
}



class ZipInputStreamTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tmpDir = createTempDir();
        zipPath = tmpDir + "/input_test.zip";
    }
    void TearDown() override
    {
        std::filesystem::remove_all(tmpDir);
    }

    std::string tmpDir;
    std::string zipPath;
};

TEST_F(ZipInputStreamTest, AutoLoadsFirstEntry)
{
    writeZipFile(zipPath, {{"first.txt", "FirstContent"}, {"second.txt", "SecondContent"}});

    std::ifstream file(zipPath, std::ios::binary);
    zipios::ZipInputStream zis(file);

    // The first entry should be auto-loaded — we can read without calling getNextEntry()
    std::string content((std::istreambuf_iterator<char>(zis)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "FirstContent");
}

TEST_F(ZipInputStreamTest, IterateAllEntries)
{
    std::vector<std::pair<std::string, std::string>> entries = {
        {"a.txt", "Alpha"},
        {"b.txt", "Bravo"},
        {"c.txt", "Charlie"},
    };
    writeZipFile(zipPath, entries);

    std::ifstream file(zipPath, std::ios::binary);
    zipios::ZipInputStream zis(file);

    // First entry is auto-loaded
    std::string content0((std::istreambuf_iterator<char>(zis)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content0, "Alpha");

    // Advance to second entry
    auto entry1 = zis.getNextEntry();
    ASSERT_NE(entry1, nullptr);
    EXPECT_EQ(entry1->getName(), "b.txt");
    std::string content1((std::istreambuf_iterator<char>(zis)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content1, "Bravo");

    // Advance to third entry
    auto entry2 = zis.getNextEntry();
    ASSERT_NE(entry2, nullptr);
    EXPECT_EQ(entry2->getName(), "c.txt");
    std::string content2((std::istreambuf_iterator<char>(zis)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content2, "Charlie");
}

TEST_F(ZipInputStreamTest, ThrowsWhenExhausted)
{
    writeZipFile(zipPath, {{"only.txt", "data"}});

    std::ifstream file(zipPath, std::ios::binary);
    zipios::ZipInputStream zis(file);

    // First entry auto-loaded; calling getNextEntry should throw since there's only 1 entry
    EXPECT_THROW(zis.getNextEntry(), std::exception);
}

TEST_F(ZipInputStreamTest, FromFilenameConstructor)
{
    writeZipFile(zipPath, {{"test.txt", "FromFilename"}});

    zipios::ZipInputStream zis(zipPath);
    std::string content((std::istreambuf_iterator<char>(zis)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "FromFilename");
}

TEST_F(ZipInputStreamTest, FromFilenameIterate)
{
    writeZipFile(zipPath, {{"one.txt", "1"}, {"two.txt", "2"}});

    zipios::ZipInputStream zis(zipPath);

    // First entry auto-loaded
    std::string c1((std::istreambuf_iterator<char>(zis)), std::istreambuf_iterator<char>());
    EXPECT_EQ(c1, "1");

    auto entry = zis.getNextEntry();
    EXPECT_EQ(entry->getName(), "two.txt");
    std::string c2((std::istreambuf_iterator<char>(zis)), std::istreambuf_iterator<char>());
    EXPECT_EQ(c2, "2");
}

TEST_F(ZipInputStreamTest, CloseAndEntry)
{
    writeZipFile(zipPath, {{"test.txt", "data"}});
    zipios::ZipInputStream zis(zipPath);
    zis.closeEntry();  // should not crash
    zis.close();       // should not crash
}

TEST_F(ZipInputStreamTest, NonExistingFileThrows)
{
    EXPECT_THROW(zipios::ZipInputStream("/no/such/file.zip"), zipios::IOException);
}

TEST_F(ZipInputStreamTest, InvalidZipDataThrows)
{
    // Write garbage data
    std::string garbage = "This is not a zip file";
    std::istringstream iss(garbage, std::ios::binary);
    EXPECT_THROW(zipios::ZipInputStream zis(iss), zipios::IOException);
}

TEST_F(ZipInputStreamTest, BinaryRoundTrip)
{
    // Create binary content with all byte values
    std::string binary;
    for (int i = 0; i < 256; ++i) {
        binary.push_back(static_cast<char>(i));
    }

    writeZipFile(zipPath, {{"binary.bin", binary}});

    zipios::ZipInputStream zis(zipPath);
    std::string readBack((std::istreambuf_iterator<char>(zis)), std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, binary);
}



class ZipFileDataTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tmpDir = createTempDir();
        zipPath = tmpDir + "/data_test.zip";
    }
    void TearDown() override
    {
        std::filesystem::remove_all(tmpDir);
    }

    std::string tmpDir;
    std::string zipPath;
};

TEST_F(ZipFileDataTest, GetEntryMatch)
{
    writeZipFile(zipPath, {{"dir/file.txt", "data"}});

    zipios::ZipFile zf(zipPath);
    auto entry = zf.getEntry("dir/file.txt", zipios::FileCollection::MATCH);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->getName(), "dir/file.txt");

    // Should not find with wrong path
    auto missing = zf.getEntry("other/file.txt", zipios::FileCollection::MATCH);
    EXPECT_EQ(missing, nullptr);
}

TEST_F(ZipFileDataTest, GetEntryIgnorePath)
{
    writeZipFile(zipPath, {{"deep/path/file.txt", "data"}});

    zipios::ZipFile zf(zipPath);

    // IGNORE mode should match by filename only
    auto entry = zf.getEntry("file.txt", zipios::FileCollection::IGNORE_PATH);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->getName(), "deep/path/file.txt");
}

TEST_F(ZipFileDataTest, GetInputStreamByEntry)
{
    writeZipFile(zipPath, {{"entry.txt", "EntryContent"}});

    zipios::ZipFile zf(zipPath);
    auto entry = zf.getEntry("entry.txt");
    ASSERT_NE(entry, nullptr);

    auto is = zf.getInputStream(entry);
    ASSERT_NE(is, nullptr);
    std::string content((std::istreambuf_iterator<char>(*is)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "EntryContent");
}

TEST_F(ZipFileDataTest, GetInputStreamByName)
{
    writeZipFile(zipPath, {{"named.txt", "NamedContent"}});

    zipios::ZipFile zf(zipPath);
    auto is = zf.getInputStream("named.txt");
    ASSERT_NE(is, nullptr);
    std::string content((std::istreambuf_iterator<char>(*is)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "NamedContent");
}

TEST_F(ZipFileDataTest, GetInputStreamNullForMissing)
{
    writeZipFile(zipPath, {{"exists.txt", "data"}});

    zipios::ZipFile zf(zipPath);
    auto is = zf.getInputStream("missing.txt");
    EXPECT_FALSE(is);
}

TEST_F(ZipFileDataTest, GetInputStreamNullEntry)
{
    writeZipFile(zipPath, {{"entry.txt", "data"}});

    zipios::ZipFile zf(zipPath);
    zipios::ConstEntryPointer nullEntry;
    auto is = zf.getInputStream(nullEntry);
    EXPECT_FALSE(is);
}

TEST_F(ZipFileDataTest, CloneCreatesIndependentCopy)
{
    writeZipFile(zipPath, {{"clone.txt", "CloneData"}});

    zipios::ZipFile zf(zipPath);
    auto cloned = zf.clone();

    EXPECT_TRUE(cloned->isValid());
    EXPECT_EQ(cloned->size(), 1);
    EXPECT_EQ(cloned->getName(), zipPath);

    auto entry = cloned->getEntry("clone.txt");
    ASSERT_NE(entry, nullptr);

    auto is = cloned->getInputStream(entry);
    ASSERT_NE(is, nullptr);
    std::string content((std::istreambuf_iterator<char>(*is)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "CloneData");
}

TEST_F(ZipFileDataTest, MoveConstructor)
{
    writeZipFile(zipPath, {{"move.txt", "MoveData"}});

    zipios::ZipFile original(zipPath);
    EXPECT_TRUE(original.isValid());

    zipios::ZipFile moved(std::move(original));
    EXPECT_TRUE(moved.isValid());
    EXPECT_EQ(moved.size(), 1);

    auto is = moved.getInputStream("move.txt");
    ASSERT_NE(is, nullptr);
    std::string content((std::istreambuf_iterator<char>(*is)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "MoveData");
}

TEST_F(ZipFileDataTest, MoveAssignment)
{
    writeZipFile(zipPath, {{"assign.txt", "AssignData"}});

    zipios::ZipFile original(zipPath);
    zipios::ZipFile target;
    EXPECT_FALSE(target.isValid());

    target = std::move(original);
    EXPECT_TRUE(target.isValid());
    EXPECT_EQ(target.size(), 1);

    auto is = target.getInputStream("assign.txt");
    ASSERT_NE(is, nullptr);
    std::string content((std::istreambuf_iterator<char>(*is)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "AssignData");
}

TEST_F(ZipFileDataTest, CloseInvalidatesAccess)
{
    writeZipFile(zipPath, {{"test.txt", "data"}});

    zipios::ZipFile zf(zipPath);
    EXPECT_TRUE(zf.isValid());
    zf.close();
    EXPECT_FALSE(zf.isValid());
}

TEST_F(ZipFileDataTest, EntriesOrderPreserved)
{
    std::vector<std::pair<std::string, std::string>> items = {
        {"zebra.txt", "z"},
        {"alpha.txt", "a"},
        {"middle.txt", "m"},
    };
    writeZipFile(zipPath, items);

    zipios::ZipFile zf(zipPath);
    auto entries = zf.entries();
    ASSERT_EQ(entries.size(), 3);
    EXPECT_EQ(entries[0]->getName(), "zebra.txt");
    EXPECT_EQ(entries[1]->getName(), "alpha.txt");
    EXPECT_EQ(entries[2]->getName(), "middle.txt");
}

TEST_F(ZipFileDataTest, StreamIsSeekable)
{
    std::string content = "ABCDEFGHIJ";
    writeZipFile(zipPath, {{"seekable.txt", content}});

    zipios::ZipFile zf(zipPath);
    auto is = zf.getInputStream("seekable.txt");
    ASSERT_NE(is, nullptr);

    // Read first 3 chars
    char buf[4] = {};
    is->read(buf, 3);
    EXPECT_EQ(std::string(buf), "ABC");

    // Seek back to beginning
    is->seekg(0, std::ios::beg);
    is->read(buf, 3);
    EXPECT_EQ(std::string(buf), "ABC");

    // Seek to position 5
    is->seekg(5, std::ios::beg);
    is->read(buf, 3);
    EXPECT_EQ(std::string(buf), "FGH");
}



class GZIPTest: public ::testing::Test
{
protected:
    // Decompress gzip data using zlib
    static std::string decompressGzip(const std::string& compressed)
    {
        z_stream strm {};
        strm.zalloc = Z_NULL;
        strm.zfree = Z_NULL;
        strm.opaque = Z_NULL;
        strm.avail_in = static_cast<uInt>(compressed.size());
        strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));

        // MAX_WBITS + 16 for gzip decoding
        if (inflateInit2(&strm, MAX_WBITS + 16) != Z_OK) {
            return {};
        }

        std::string result;
        char outBuf[16384];
        int ret;
        do {
            strm.avail_out = sizeof(outBuf);
            strm.next_out = reinterpret_cast<Bytef*>(outBuf);
            ret = inflate(&strm, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                inflateEnd(&strm);
                return {};
            }
            size_t have = sizeof(outBuf) - strm.avail_out;
            result.append(outBuf, have);
        } while (ret != Z_STREAM_END);

        inflateEnd(&strm);
        return result;
    }
};

TEST_F(GZIPTest, CompressAndDecompress)
{
    std::string original = "Hello, this is a test of GZIP compression!";
    std::ostringstream oss(std::ios::binary);

    {
        zipios::GZIPOutputStream gzos(oss);
        gzos.write(original.data(), static_cast<std::streamsize>(original.size()));
        gzos.close();
    }

    std::string compressed = oss.str();
    EXPECT_FALSE(compressed.empty());
    // Check gzip magic bytes
    ASSERT_GE(compressed.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(compressed[0]), 0x1F);
    EXPECT_EQ(static_cast<unsigned char>(compressed[1]), 0x8B);

    std::string decompressed = decompressGzip(compressed);
    EXPECT_EQ(decompressed, original);
}

TEST_F(GZIPTest, StreamOperator)
{
    std::string original = "Stream operator test";
    std::ostringstream oss(std::ios::binary);

    {
        zipios::GZIPOutputStream gzos(oss);
        gzos << original;
        gzos.close();
    }

    std::string decompressed = decompressGzip(oss.str());
    EXPECT_EQ(decompressed, original);
}

TEST_F(GZIPTest, LargeData)
{
    // Generate ~100KB of data
    std::string original;
    for (int i = 0; i < 10000; ++i) {
        original += "Line " + std::to_string(i) + ": some test data\n";
    }

    std::ostringstream oss(std::ios::binary);
    {
        zipios::GZIPOutputStream gzos(oss);
        gzos.write(original.data(), static_cast<std::streamsize>(original.size()));
        gzos.close();
    }

    std::string compressed = oss.str();
    // Compressed should be smaller than original for repetitive data
    EXPECT_LT(compressed.size(), original.size());

    std::string decompressed = decompressGzip(compressed);
    EXPECT_EQ(decompressed, original);
}

TEST_F(GZIPTest, BinaryData)
{
    std::string original;
    for (int i = 0; i < 256; ++i) {
        original.push_back(static_cast<char>(i));
    }

    std::ostringstream oss(std::ios::binary);
    {
        zipios::GZIPOutputStream gzos(oss);
        gzos.write(original.data(), static_cast<std::streamsize>(original.size()));
        gzos.close();
    }

    std::string decompressed = decompressGzip(oss.str());
    EXPECT_EQ(decompressed, original);
}

TEST_F(GZIPTest, EmptyData)
{
    std::ostringstream oss(std::ios::binary);
    {
        zipios::GZIPOutputStream gzos(oss);
        gzos.close();
    }

    std::string compressed = oss.str();
    EXPECT_FALSE(compressed.empty());  // Even empty gzip has header/trailer

    std::string decompressed = decompressGzip(compressed);
    EXPECT_TRUE(decompressed.empty());
}

TEST_F(GZIPTest, MultipleWrites)
{
    std::ostringstream oss(std::ios::binary);
    {
        zipios::GZIPOutputStream gzos(oss);
        gzos << "Part1";
        gzos << " ";
        gzos << "Part2";
        gzos.close();
    }

    std::string decompressed = decompressGzip(oss.str());
    EXPECT_EQ(decompressed, "Part1 Part2");
}



class CollectionCollectionDataTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tmpDir = createTempDir();
        zip1Path = tmpDir + "/coll1.zip";
        zip2Path = tmpDir + "/coll2.zip";
    }
    void TearDown() override
    {
        std::filesystem::remove_all(tmpDir);
    }

    std::string tmpDir;
    std::string zip1Path;
    std::string zip2Path;
};

TEST_F(CollectionCollectionDataTest, AggregatesEntries)
{
    writeZipFile(zip1Path, {{"a.txt", "A"}, {"b.txt", "B"}});
    writeZipFile(zip2Path, {{"c.txt", "C"}});

    zipios::ZipFile zf1(zip1Path);
    zipios::ZipFile zf2(zip2Path);

    zipios::CollectionCollection cc;
    EXPECT_TRUE(cc.addCollection(zf1));
    EXPECT_TRUE(cc.addCollection(zf2));
    EXPECT_TRUE(cc.isValid());

    auto entries = cc.entries();
    EXPECT_EQ(entries.size(), 3);
}

TEST_F(CollectionCollectionDataTest, GetEntryAcrossCollections)
{
    writeZipFile(zip1Path, {{"first.txt", "F"}});
    writeZipFile(zip2Path, {{"second.txt", "S"}});

    zipios::ZipFile zf1(zip1Path);
    zipios::ZipFile zf2(zip2Path);

    zipios::CollectionCollection cc;
    cc.addCollection(zf1);
    cc.addCollection(zf2);

    auto e1 = cc.getEntry("first.txt");
    ASSERT_NE(e1, nullptr);
    EXPECT_EQ(e1->getName(), "first.txt");

    auto e2 = cc.getEntry("second.txt");
    ASSERT_NE(e2, nullptr);
    EXPECT_EQ(e2->getName(), "second.txt");

    auto missing = cc.getEntry("nope.txt");
    EXPECT_EQ(missing, nullptr);
}

TEST_F(CollectionCollectionDataTest, GetInputStreamAcrossCollections)
{
    writeZipFile(zip1Path, {{"from1.txt", "Content1"}});
    writeZipFile(zip2Path, {{"from2.txt", "Content2"}});

    zipios::ZipFile zf1(zip1Path);
    zipios::ZipFile zf2(zip2Path);

    zipios::CollectionCollection cc;
    cc.addCollection(zf1);
    cc.addCollection(zf2);

    auto is1 = cc.getInputStream("from1.txt");
    ASSERT_NE(is1, nullptr);
    std::string c1((std::istreambuf_iterator<char>(*is1)), std::istreambuf_iterator<char>());
    EXPECT_EQ(c1, "Content1");

    auto is2 = cc.getInputStream("from2.txt");
    ASSERT_NE(is2, nullptr);
    std::string c2((std::istreambuf_iterator<char>(*is2)), std::istreambuf_iterator<char>());
    EXPECT_EQ(c2, "Content2");
}

TEST_F(CollectionCollectionDataTest, RejectsInvalidCollection)
{
    zipios::ZipFile invalid;
    EXPECT_FALSE(invalid.isValid());

    zipios::CollectionCollection cc;
    EXPECT_FALSE(cc.addCollection(invalid));
    EXPECT_FALSE(cc.isValid());
}

TEST_F(CollectionCollectionDataTest, CloseInvalidatesAll)
{
    writeZipFile(zip1Path, {{"test.txt", "data"}});

    zipios::ZipFile zf(zip1Path);
    zipios::CollectionCollection cc;
    cc.addCollection(zf);
    EXPECT_TRUE(cc.isValid());

    cc.close();
    EXPECT_FALSE(cc.isValid());
}



TEST(ExceptionTypes, FCollExceptionIsRuntimeError)
{
    try {
        throw zipios::FCollException("test");
    }
    catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "test");
    }
}

TEST(ExceptionTypes, InvalidStateExceptionIsLogicError)
{
    try {
        throw zipios::InvalidStateException("invalid state");
    }
    catch (const std::logic_error& e) {
        EXPECT_STREQ(e.what(), "invalid state");
    }
}

TEST(ExceptionTypes, IOExceptionIsIOSFailure)
{
    try {
        throw zipios::IOException("io error");
    }
    catch (const std::ios_base::failure& e) {
        EXPECT_NE(std::string(e.what()).find("io error"), std::string::npos);
    }
}



class ZipHeaderTest: public ::testing::Test
{
protected:
    void SetUp() override
    {
        tmpDir = createTempDir();
        zipPath = tmpDir + "/header_test.zip";
    }
    void TearDown() override
    {
        std::filesystem::remove_all(tmpDir);
    }

    std::string tmpDir;
    std::string zipPath;
};

TEST_F(ZipHeaderTest, ReadFromStream)
{
    writeZipFile(zipPath, {{"doc.xml", "<root/>"},  {"data.bin", "binary"}});

    std::ifstream file(zipPath, std::ios::binary);
    zipios::ZipHeader hdr(file);
    EXPECT_TRUE(hdr.isValid());
    EXPECT_EQ(hdr.size(), 2);

    auto entries = hdr.entries();
    ASSERT_EQ(entries.size(), 2);
    EXPECT_EQ(entries[0]->getName(), "doc.xml");
    EXPECT_EQ(entries[1]->getName(), "data.bin");
}

TEST_F(ZipHeaderTest, GetInputStream)
{
    writeZipFile(zipPath, {{"content.txt", "Hello ZipHeader"}});

    std::ifstream file(zipPath, std::ios::binary);
    zipios::ZipHeader hdr(file);
    auto is = hdr.getInputStream("content.txt");
    ASSERT_TRUE(is);
    std::string content((std::istreambuf_iterator<char>(*is)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "Hello ZipHeader");
}

TEST_F(ZipHeaderTest, GetInputStreamByEntry)
{
    writeZipFile(zipPath, {{"entry.txt", "EntryData"}});

    std::ifstream file(zipPath, std::ios::binary);
    zipios::ZipHeader hdr(file);
    auto entry = hdr.getEntry("entry.txt");
    ASSERT_NE(entry, nullptr);

    auto is = hdr.getInputStream(entry);
    ASSERT_TRUE(is);
    std::string content((std::istreambuf_iterator<char>(*is)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "EntryData");
}

TEST_F(ZipHeaderTest, MissingEntryReturnsNull)
{
    writeZipFile(zipPath, {{"exists.txt", "data"}});

    std::ifstream file(zipPath, std::ios::binary);
    zipios::ZipHeader hdr(file);
    auto is = hdr.getInputStream("missing.txt");
    EXPECT_FALSE(is);
}

TEST_F(ZipHeaderTest, CloneReturnsInvalid)
{
    writeZipFile(zipPath, {{"test.txt", "data"}});

    std::ifstream file(zipPath, std::ios::binary);
    zipios::ZipHeader hdr(file);
    EXPECT_TRUE(hdr.isValid());

    auto cloned = hdr.clone();
    EXPECT_FALSE(cloned->isValid());
}

TEST_F(ZipHeaderTest, CloseInvalidates)
{
    writeZipFile(zipPath, {{"test.txt", "data"}});

    std::ifstream file(zipPath, std::ios::binary);
    zipios::ZipHeader hdr(file);
    EXPECT_TRUE(hdr.isValid());
    hdr.close();
    EXPECT_FALSE(hdr.isValid());
}

// --- Unicode and encoding tests ---

TEST_F(ZipRoundTripTest, Utf8EntryNames)
{
    // Latin accented characters, CJK, Cyrillic
    std::vector<std::pair<std::string, std::string>> entries = {
        {"café.txt", "coffee"},
        {"naïve.txt", "innocence"},
        {"Ångström.txt", "unit"},
        {"文件.txt", "Chinese for file"},
        {"Привет.txt", "Russian hello"},
        {"résumé/données.xml", "nested accented path"},
    };
    writeZipFile(zipPath, entries);

    zipios::ZipFile zf(zipPath);
    EXPECT_EQ(zf.size(), entries.size());

    for (const auto& [name, content] : entries) {
        auto entry = zf.getEntry(name);
        ASSERT_NE(entry, nullptr) << "Missing entry: " << name;
        auto is = zf.getInputStream(name);
        ASSERT_NE(is, nullptr) << "No stream for: " << name;
        std::string readBack((std::istreambuf_iterator<char>(*is)),
                              std::istreambuf_iterator<char>());
        EXPECT_EQ(readBack, content) << "Content mismatch for: " << name;
    }
}

TEST_F(ZipRoundTripTest, EmojiEntryName)
{
    // Emoji are 4-byte UTF-8 sequences, edge case
    writeZipFile(zipPath, {{"docs/📐design.txt", "blueprint"}});

    zipios::ZipFile zf(zipPath);
    EXPECT_EQ(zf.size(), 1);
    auto is = zf.getInputStream("docs/📐design.txt");
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, "blueprint");
}

TEST_F(ZipRoundTripTest, Utf8Content)
{
    // Make sure multi-byte content survives the round trip intact
    std::string utf8Content = "Ελληνικά • 日本語 • العربية • 한국어\n"
                              "Ñoño — „Anführungszeichen\" — œuvre\n"
                              "emoji: 🔧🛠️⚙️\n";

    writeZipFile(zipPath, {{"multilang.txt", utf8Content}});

    zipios::ZipFile zf(zipPath);
    auto is = zf.getInputStream("multilang.txt");
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, utf8Content);
}

TEST_F(ZipRoundTripTest, Utf8ViaZipInputStream)
{
    // Same thing but going through ZipInputStream instead of ZipFile
    std::string name = "données.txt";
    std::string content = "Ça marche très bien";
    writeZipFile(zipPath, {{name, content}});

    zipios::ZipInputStream zis(zipPath);
    std::string readBack((std::istreambuf_iterator<char>(zis)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, content);
}

TEST_F(ZipRoundTripTest, WindowsStyleBackslashPath)
{
    // Some old Windows zip tools store paths with backslashes.
    // libzip normalizes to forward slashes on read, so we just make sure
    // we can write a name containing a backslash and read it back.
    std::string name = "folder\\subfolder\\file.txt";
    writeZipFile(zipPath, {{name, "backslash content"}});

    zipios::ZipFile zf(zipPath);
    EXPECT_EQ(zf.size(), 1);

    auto entries = zf.entries();
    ASSERT_EQ(entries.size(), 1);
    // The stored name should survive as-is (libzip doesn't mangle on write)
    std::string storedName = entries[0]->getName();

    auto is = zf.getInputStream(storedName);
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, "backslash content");
}

TEST_F(ZipRoundTripTest, HighLatinAndSpecialChars)
{
    // Characters from the CP437/Windows-1252 danger zone: these are the
    // ones that get garbled when a zip tool writes CP437 but we read as UTF-8.
    // Here we write proper UTF-8 and verify it comes back cleanly.
    std::vector<std::pair<std::string, std::string>> entries = {
        {"Ärger.txt", "German umlaut"},
        {"über.txt", "more umlauts"},
        {"piñata.txt", "tilde-n"},
        {"æøå.txt", "Nordic"},
        {"ß.txt", "Eszett"},
        {"£€¥.txt", "currency symbols"},
    };
    writeZipFile(zipPath, entries);

    zipios::ZipFile zf(zipPath);
    EXPECT_EQ(zf.size(), entries.size());

    for (const auto& [name, content] : entries) {
        auto is = zf.getInputStream(name);
        ASSERT_NE(is, nullptr) << "No stream for: " << name;
        std::string readBack((std::istreambuf_iterator<char>(*is)),
                              std::istreambuf_iterator<char>());
        EXPECT_EQ(readBack, content) << "Mismatch for: " << name;
    }
}

TEST_F(ZipRoundTripTest, MixedAsciiAndUtf8Entries)
{
    // Realistic scenario: a .FCStd-like archive where most names are ASCII
    // but one or two have accented characters
    std::vector<std::pair<std::string, std::string>> entries = {
        {"Document.xml", "<doc/>"},
        {"Pièce.brp", "shape data"},
        {"GuiDocument.xml", "<gui/>"},
        {"Körper/Shape.brp", "body shape"},
    };
    writeZipFile(zipPath, entries);

    zipios::ZipFile zf(zipPath);
    EXPECT_EQ(zf.size(), entries.size());

    for (const auto& [name, content] : entries) {
        auto entry = zf.getEntry(name);
        ASSERT_NE(entry, nullptr) << "Missing: " << name;
        auto is = zf.getInputStream(entry);
        ASSERT_NE(is, nullptr);
        std::string readBack((std::istreambuf_iterator<char>(*is)),
                              std::istreambuf_iterator<char>());
        EXPECT_EQ(readBack, content);
    }
}

TEST_F(ZipRoundTripTest, Utf8ArchiveComment)
{
    // Verify setComment handles UTF-8 without truncation or corruption
    zipios::ZipOutputStream out(zipPath);
    out.setComment("Kommentar: Ünïcödé 测试 🎉");
    out.putNextEntry("test.txt");
    out << "data";
    out.close();

    // We can't easily read the comment back through our API, but at
    // minimum the archive should still be valid and readable.
    zipios::ZipFile zf(zipPath);
    EXPECT_TRUE(zf.isValid());
    EXPECT_EQ(zf.size(), 1);
    auto is = zf.getInputStream("test.txt");
    ASSERT_NE(is, nullptr);
    std::string readBack((std::istreambuf_iterator<char>(*is)),
                          std::istreambuf_iterator<char>());
    EXPECT_EQ(readBack, "data");
}


TEST_F(ZipHeaderTest, InvalidStreamThrows)
{
    std::string garbage = "not a zip file";
    std::istringstream iss(garbage);
    EXPECT_THROW(zipios::ZipHeader hdr(iss), zipios::FCollException);
}

TEST_F(ZipHeaderTest, WithOffsets)
{
    // Create a zip embedded between prefix/suffix padding
    std::string prefix(128, 'P');
    std::string suffix(64, 'S');

    writeZipFile(zipPath, {{"inside.txt", "offset content"}});
    std::ifstream zipFile(zipPath, std::ios::binary);
    std::string zipData((std::istreambuf_iterator<char>(zipFile)),
                         std::istreambuf_iterator<char>());
    zipFile.close();

    // Write prefix + zip + suffix to a new file
    std::string paddedPath = tmpDir + "/padded.bin";
    {
        std::ofstream padded(paddedPath, std::ios::binary);
        padded.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
        padded.write(zipData.data(), static_cast<std::streamsize>(zipData.size()));
        padded.write(suffix.data(), static_cast<std::streamsize>(suffix.size()));
    }

    std::ifstream padded(paddedPath, std::ios::binary);
    zipios::ZipHeader hdr(padded, static_cast<int>(prefix.size()),
                          static_cast<int>(suffix.size()));
    EXPECT_TRUE(hdr.isValid());
    EXPECT_EQ(hdr.size(), 1);

    auto is = hdr.getInputStream("inside.txt");
    ASSERT_TRUE(is);
    std::string content((std::istreambuf_iterator<char>(*is)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "offset content");
}

// --- Zip Slip (CVE-2018-1002200) tests ---

TEST_F(ZipRoundTripTest, ZipSlipEntriesFilteredFromZipFile)
{
    // The zip-slip test archive contains "good.txt" and a path-traversal entry
    std::string slipZip = std::string(TESTFILES_DIR) + "/zip-slip-vulnerability/zip-slip.zip";
    zipios::ZipFile zf(slipZip);
    EXPECT_TRUE(zf.isValid());

    auto entries = zf.entries();
    // Only safe entries should be visible
    for (const auto& entry : entries) {
        EXPECT_EQ(entry->getName().find(".."), std::string::npos)
            << "Path traversal entry not filtered: " << entry->getName();
    }

    // The safe entry should still be accessible
    auto goodEntry = zf.getEntry("good.txt");
    EXPECT_NE(goodEntry, nullptr);
}

TEST_F(ZipRoundTripTest, ZipSlipWindowsEntriesFilteredFromZipFile)
{
    std::string slipZip = std::string(TESTFILES_DIR) + "/zip-slip-vulnerability/zip-slip-win.zip";
    zipios::ZipFile zf(slipZip);
    EXPECT_TRUE(zf.isValid());

    auto entries = zf.entries();
    for (const auto& entry : entries) {
        EXPECT_EQ(entry->getName().find(".."), std::string::npos)
            << "Path traversal entry not filtered: " << entry->getName();
    }

    auto goodEntry = zf.getEntry("good.txt");
    EXPECT_NE(goodEntry, nullptr);
}

TEST_F(ZipRoundTripTest, ZipSlipEntriesFilteredFromZipInputStream)
{
    std::string slipZip = std::string(TESTFILES_DIR) + "/zip-slip-vulnerability/zip-slip.zip";
    zipios::ZipInputStream zis(slipZip);

    // The first (and only visible) entry should be good.txt, not the traversal one.
    // ZipInputStream auto-loads the first safe entry.
    std::string content((std::istreambuf_iterator<char>(zis)),
                         std::istreambuf_iterator<char>());
    EXPECT_FALSE(content.empty());

    // There should be no more entries (traversal one was skipped)
    EXPECT_THROW(zis.getNextEntry(), std::exception);
}

TEST_F(ZipRoundTripTest, ZipSlipEntriesFilteredFromZipHeader)
{
    std::string slipZip = std::string(TESTFILES_DIR) + "/zip-slip-vulnerability/zip-slip.zip";
    std::ifstream file(slipZip, std::ios::binary);
    zipios::ZipHeader hdr(file);
    EXPECT_TRUE(hdr.isValid());

    auto entries = hdr.entries();
    for (const auto& entry : entries) {
        EXPECT_EQ(entry->getName().find(".."), std::string::npos)
            << "Path traversal entry not filtered: " << entry->getName();
    }
}

TEST_F(ZipRoundTripTest, ZipSlipAbsolutePathFiltered)
{
    // Create a zip with an absolute path entry
    zipios::ZipOutputStream zos(zipPath);
    zos.putNextEntry("/etc/passwd");
    zos << "root:x:0:0";
    zos.putNextEntry("safe.txt");
    zos << "safe content";
    zos.close();

    zipios::ZipFile zf(zipPath);
    auto entries = zf.entries();

    // Only safe.txt should be visible
    EXPECT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0]->getName(), "safe.txt");
}

TEST_F(ZipRoundTripTest, ZipSlipDoubleDotInFilenameAllowed)
{
    // A file named "foo..bar" is NOT a traversal — ".." must be a path component
    writeZipFile(zipPath, {{"foo..bar.txt", "ok"}, {"dir/file..ext", "also ok"}});

    zipios::ZipFile zf(zipPath);
    EXPECT_EQ(zf.size(), 2);

    auto e1 = zf.getEntry("foo..bar.txt");
    EXPECT_NE(e1, nullptr);

    auto e2 = zf.getEntry("dir/file..ext");
    EXPECT_NE(e2, nullptr);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
