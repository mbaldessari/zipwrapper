// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2025 FreeCAD Project Association                        *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "ZipWrapper.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <zip.h>
#include <zlib.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif


namespace zipios
{


namespace
{

// Reject obviously corrupted or malicious entries that would exhaust memory.
// 1 GiB is far above any realistic .FCStd entry but still prevents OOM.
constexpr zip_uint64_t maxZipEntrySize = zip_uint64_t{1} << 30;

// Extract the filename part from a path (after the last '/')
std::string_view filenameFromPath(const std::string& path)
{
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return std::string_view(path).substr(pos + 1);
}

}  // namespace



ConstEntryPointer FileCollection::getEntry(const std::string& name, MatchPath matchpath) const
{
    if (matchpath == MATCH) {
        auto it = _index.find(name);
        if (it != _index.end()) {
            return it->second;
        }
    }
    else {
        auto target = filenameFromPath(name);
        for (const auto& ep : _entries) {
            if (filenameFromPath(ep->getName()) == target) {
                return ep;
            }
        }
    }
    return {};
}



struct ZipFile::Impl
{
    zip_t* archive = nullptr;
    std::string filename;
};

ZipFile::ZipFile()
    : d(std::make_unique<Impl>())
{}

ZipFile::ZipFile(const std::string& name, int /*s_off*/, int /*e_off*/)
    : d(std::make_unique<Impl>())
{
    d->filename = name;
    _name = name;

    int err = 0;
    d->archive = zip_open(name.c_str(), ZIP_RDONLY, &err);
    if (!d->archive) {
        throw FCollException("Failed to open zip file: " + name);
    }

    // Build the entry list up front so callers can iterate without
    // having to poke at the libzip handle directly.
    zip_int64_t numEntries = zip_get_num_entries(d->archive, 0);
    _entries.reserve(static_cast<size_t>(numEntries));
    for (zip_int64_t i = 0; i < numEntries; ++i) {
        const char* entryName = zip_get_name(d->archive, static_cast<zip_uint64_t>(i), 0);
        if (entryName) {
            _entries.push_back(std::make_shared<FileEntry>(entryName, static_cast<int>(i)));
        }
    }
    buildIndex();
    _valid = true;
}

ZipFile::~ZipFile()
{
    if (d && d->archive) {
        zip_close(d->archive);
    }
}

ZipFile::ZipFile(ZipFile&& other) noexcept
    : FileCollection(std::move(other))
    , d(std::move(other.d))
{
    other._valid = false;
}

ZipFile& ZipFile::operator=(ZipFile&& other) noexcept
{
    if (this != &other) {
        if (d && d->archive) {
            zip_close(d->archive);
        }
        FileCollection::operator=(std::move(other));
        d = std::move(other.d);
        other._valid = false;
    }
    return *this;
}

std::unique_ptr<FileCollection> ZipFile::clone() const
{
    if (!_valid || !d || !d->archive) {
        return std::make_unique<ZipFile>();
    }
    return std::make_unique<ZipFile>(d->filename);
}

std::unique_ptr<std::istream> ZipFile::getInputStream(const ConstEntryPointer& entry)
{
    if (!_valid || !d || !d->archive) {
        throw InvalidStateException("Attempt to use an invalid ZipFile");
    }
    if (!entry) {
        return nullptr;
    }
    return getInputStream(entry->getName());
}

std::unique_ptr<std::istream> ZipFile::getInputStream(const std::string& entry_name,
                                                      MatchPath matchpath)
{
    if (!_valid || !d || !d->archive) {
        throw InvalidStateException("Attempt to use an invalid ZipFile");
    }

    auto entry = getEntry(entry_name, matchpath);
    if (!entry) {
        return nullptr;
    }

    zip_int64_t idx = zip_name_locate(d->archive, entry->getName().c_str(), 0);
    if (idx < 0) {
        return nullptr;
    }

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(d->archive, static_cast<zip_uint64_t>(idx), 0, &stat) < 0) {
        throw IOException("Failed to stat entry: " + entry->getName());
    }

    if (!(stat.valid & ZIP_STAT_SIZE)) {
        throw IOException("Entry size unknown: " + entry->getName());
    }
    if (stat.size > maxZipEntrySize) {
        throw IOException("Entry exceeds 1 GiB size limit: " + entry->getName());
    }

    zip_file_t* zf = zip_fopen_index(d->archive, static_cast<zip_uint64_t>(idx), 0);
    if (!zf) {
        throw IOException(std::string("Failed to open entry: ") + entry->getName()
                          + " (" + zip_strerror(d->archive) + ")");
    }

    std::vector<char> buf(static_cast<size_t>(stat.size));
    zip_uint64_t totalRead = 0;
    while (totalRead < stat.size) {
        zip_int64_t n = zip_fread(zf, buf.data() + totalRead, stat.size - totalRead);
        if (n < 0) {
            std::string err = zip_file_strerror(zf);
            zip_fclose(zf);
            throw IOException("Failed to read entry: " + entry->getName() + " (" + err + ")");
        }
        if (n == 0) {
            break;
        }
        totalRead += static_cast<zip_uint64_t>(n);
    }
    zip_fclose(zf);
    buf.resize(static_cast<size_t>(totalRead));

    return std::make_unique<MemoryIStream>(std::move(buf));
}



struct ZipOutputStream::Impl
{
    // Per-entry data stored in memory
    struct EntryData
    {
        std::string name;
        std::vector<char> data;
    };

    std::vector<EntryData> entries;
    MemoryStreamBuf currentBuf;
    std::string currentEntryName;
    std::string comment;
    int level = Z_DEFAULT_COMPRESSION;
    bool hasCurrentEntry = false;

    // We either write to a file path or pipe into an existing ostream.
    // Both paths end up going through libzip's zip_open / zip_source API.
    std::string targetFile;
    std::ostream* targetStream = nullptr;
    bool closed = false;

    void flushCurrentEntry()
    {
        if (hasCurrentEntry) {
            entries.push_back({currentEntryName, currentBuf.data()});
            currentBuf = MemoryStreamBuf();
            hasCurrentEntry = false;
        }
    }

    void writeZip()
    {
        if (closed) {
            return;
        }
        closed = true;

        if (!targetFile.empty()) {
            writeZipToFile(targetFile);
        }
        else if (targetStream) {
            writeZipToStream(*targetStream);
        }
    }

    void writeZipToFile(const std::string& filename)
    {
        int err = 0;
        zip_t* archive = zip_open(filename.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
        if (!archive) {
            throw IOException("Failed to create zip file: " + filename);
        }
        writeEntriesToArchive(archive);

        if (!comment.empty()) {
            zip_set_archive_comment(archive, comment.c_str(),
                                    static_cast<zip_uint16_t>(comment.size()));
        }

        if (zip_close(archive) < 0) {
            zip_discard(archive);
            throw IOException("Failed to close zip file: " + filename);
        }
    }

    void writeZipToStream(std::ostream& os)
    {
        // libzip needs a real fd to write to, so we go through a temp file
        // and then shovel the bytes into the caller's ostream.  A bit ugly
        // but it works and avoids reimplementing zip serialization by hand.
        auto tmpTemplate = std::filesystem::temp_directory_path() / "fc_zip_XXXXXX";
        std::string tmpFile;

#ifdef _WIN32
        std::string tmpl = tmpTemplate.string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        if (_mktemp_s(buf.data(), buf.size()) != 0) {
            throw IOException("Failed to create temp file for zip output");
        }
        tmpFile = buf.data();
        // Atomically claim the file to match mkstemp behavior on Unix and
        // prevent another process from creating a file (or symlink) at this
        // path between name generation and writeZipToFile.
        int fd = _open(tmpFile.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY,
                       _S_IREAD | _S_IWRITE);
        if (fd == -1) {
            throw IOException("Failed to create temp file for zip output");
        }
        _close(fd);
#else
        tmpFile = tmpTemplate.string();
        int fd = mkstemp(tmpFile.data());
        if (fd == -1) {
            throw IOException("Failed to create temp file for zip output");
        }
        ::close(fd);
#endif

        try {
            writeZipToFile(tmpFile);

            std::ifstream tmp(tmpFile, std::ios::binary);
            if (tmp) {
                os << tmp.rdbuf();
            }
        }
        catch (...) {
            std::filesystem::remove(tmpFile);
            throw;
        }

        std::filesystem::remove(tmpFile);
    }

    void writeEntriesToArchive(zip_t* archive)
    {
        for (auto& entry : entries) {
            zip_source_t* src = zip_source_buffer(archive, entry.data.data(),
                                                  entry.data.size(), 0);
            if (!src) {
                throw IOException("Failed to create zip source for entry: " + entry.name);
            }
            zip_int64_t idx = zip_file_add(archive, entry.name.c_str(), src,
                                           ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
            if (idx < 0) {
                zip_source_free(src);
                throw IOException("Failed to add entry to zip: " + entry.name);
            }

            if (level == 0) {
                zip_set_file_compression(archive, static_cast<zip_uint64_t>(idx),
                                         ZIP_CM_STORE, 0);
            }
            else {
                zip_set_file_compression(archive, static_cast<zip_uint64_t>(idx),
                                         ZIP_CM_DEFLATE,
                                         static_cast<zip_uint32_t>(level));
            }
        }
    }
};

ZipOutputStream::ZipOutputStream(const std::string& filename)
    : std::ostream(nullptr)
    , d(std::make_unique<Impl>())
{
    d->targetFile = filename;
    rdbuf(&d->currentBuf);
}

ZipOutputStream::ZipOutputStream(std::ostream& os)
    : std::ostream(nullptr)
    , d(std::make_unique<Impl>())
{
    d->targetStream = &os;
    rdbuf(&d->currentBuf);
}

ZipOutputStream::~ZipOutputStream()
{
    // Best-effort close: close() may throw (e.g. disk full, I/O error). Callers
    // should call close() or finish() explicitly before destruction if they need
    // to handle failures; we swallow here to satisfy noexcept-ish destructor.
    try {
        close();
    }
    catch (...) {
    }
}

void ZipOutputStream::putNextEntry(const std::string& entryName)
{
    d->flushCurrentEntry();
    d->currentEntryName = entryName;
    d->currentBuf = MemoryStreamBuf();
    rdbuf(&d->currentBuf);
    d->hasCurrentEntry = true;
    clear();
}

void ZipOutputStream::putNextEntry(const ZipCDirEntry& entry)
{
    putNextEntry(entry.getName());
}

void ZipOutputStream::setComment(const std::string& comment)
{
    d->comment = comment;
}

void ZipOutputStream::setLevel(int level)
{
    d->level = level;
}

void ZipOutputStream::closeEntry()
{
    d->flushCurrentEntry();
}

void ZipOutputStream::close()
{
    d->flushCurrentEntry();
    d->writeZip();
}

void ZipOutputStream::finish()
{
    close();
}



struct ZipInputStream::Impl
{
    zip_t* archive = nullptr;
    zip_source_t* source = nullptr;
    std::vector<char> archiveData;
    zip_int64_t numEntries = 0;
    zip_int64_t currentIndex = -1;
    MemoryStreamBuf currentBuf;

    void openArchive(const std::string& errorContext)
    {
        zip_error_t zerr;
        zip_error_init(&zerr);
        source = zip_source_buffer_create(archiveData.data(),
                                          archiveData.size(), 0, &zerr);
        if (!source) {
            zip_error_fini(&zerr);
            throw IOException("Failed to create zip source from " + errorContext);
        }

        archive = zip_open_from_source(source, ZIP_RDONLY, &zerr);
        if (!archive) {
            zip_source_free(source);
            source = nullptr;
            zip_error_fini(&zerr);
            throw IOException("Failed to open zip from " + errorContext);
        }
        zip_error_fini(&zerr);

        numEntries = zip_get_num_entries(archive, 0);
    }

    ~Impl()
    {
        if (archive) {
            zip_close(archive);
        }
        // NB: after zip_open_from_source succeeds, the archive owns the source
        // so we must not free it ourselves
    }
};

ZipInputStream::ZipInputStream(std::istream& is)
    : std::istream(nullptr)
    , d(std::make_unique<Impl>())
{
    // Slurp the whole stream into memory -- libzip wants random access and
    // std::istream doesn't guarantee seekability, so this is the safe bet.
    d->archiveData.assign(std::istreambuf_iterator<char>(is),
                          std::istreambuf_iterator<char>());
    d->openArchive("stream");

    rdbuf(&d->currentBuf);
    if (d->numEntries > 0) {
        getNextEntry();
    }
}

ZipInputStream::ZipInputStream(const std::string& filename)
    : std::istream(nullptr)
    , d(std::make_unique<Impl>())
{
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw IOException("Failed to open file: " + filename);
    }
    d->archiveData.assign(std::istreambuf_iterator<char>(file),
                          std::istreambuf_iterator<char>());
    file.close();

    d->openArchive("file: " + filename);

    rdbuf(&d->currentBuf);
    if (d->numEntries > 0) {
        getNextEntry();
    }
}

ZipInputStream::~ZipInputStream() = default;

ConstEntryPointer ZipInputStream::getNextEntry()
{
    d->currentIndex++;

    if (d->currentIndex >= d->numEntries) {
        throw FCollException("No more entries in zip archive");
    }

    auto idx = static_cast<zip_uint64_t>(d->currentIndex);
    const char* name = zip_get_name(d->archive, idx, 0);
    if (!name) {
        throw IOException("Failed to get entry name");
    }

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(d->archive, idx, 0, &stat) < 0) {
        throw IOException("Failed to stat entry");
    }

    if (!(stat.valid & ZIP_STAT_SIZE) || stat.size > maxZipEntrySize) {
        throw IOException("Zip entry size missing or exceeds 1 GiB limit");
    }

    zip_file_t* zf = zip_fopen_index(d->archive, idx, 0);
    if (!zf) {
        throw IOException("Failed to open entry in zip");
    }

    std::vector<char> buf(static_cast<size_t>(stat.size));
    zip_uint64_t totalRead = 0;
    while (totalRead < stat.size) {
        zip_int64_t n = zip_fread(zf, buf.data() + totalRead, stat.size - totalRead);
        if (n < 0) {
            zip_fclose(zf);
            throw IOException("Failed to read entry data");
        }
        if (n == 0) {
            break;
        }
        totalRead += static_cast<zip_uint64_t>(n);
    }
    zip_fclose(zf);
    buf.resize(static_cast<size_t>(totalRead));

    d->currentBuf = MemoryStreamBuf(std::move(buf));
    rdbuf(&d->currentBuf);
    clear();

    return std::make_shared<const FileEntry>(name, static_cast<int>(d->currentIndex));
}

void ZipInputStream::closeEntry()
{
    // No-op: kept for zipios++ API compatibility; entry is advanced by getNextEntry().
}

void ZipInputStream::close()
{
    if (d->archive) {
        zip_close(d->archive);
        d->archive = nullptr;
    }
}



namespace
{

class GZIPStreamBuf: public std::streambuf
{
public:
    explicit GZIPStreamBuf(std::ostream& os)
        : _os(os)
    {
        _zstream.zalloc = Z_NULL;
        _zstream.zfree = Z_NULL;
        _zstream.opaque = Z_NULL;
        // Adding 16 to windowBits tells zlib to produce gzip output
        // instead of raw deflate -- see zlib manual
        int ret = deflateInit2(&_zstream, Z_DEFAULT_COMPRESSION,
                               Z_DEFLATED, MAX_WBITS + 16,
                               8, Z_DEFAULT_STRATEGY);
        if (ret != Z_OK) {
            throw IOException("Failed to initialize gzip deflate");
        }
        _initialized = true;
        setp(_inBuf.data(), _inBuf.data() + _inBuf.size());
    }

    ~GZIPStreamBuf() override
    {
        try {
            finalize();
        }
        catch (...) {
        }
    }

    void finalize()
    {
        if (!_initialized) {
            return;
        }

        // Flush remaining data
        flushBuffer();

        // Finalize the stream
        _zstream.avail_in = 0;
        _zstream.next_in = nullptr;
        int ret;
        do {
            _zstream.avail_out = static_cast<uInt>(_outBuf.size());
            _zstream.next_out = reinterpret_cast<Bytef*>(_outBuf.data());
            ret = deflate(&_zstream, Z_FINISH);
            size_t have = _outBuf.size() - _zstream.avail_out;
            if (have > 0) {
                _os.write(_outBuf.data(), static_cast<std::streamsize>(have));
            }
        } while (ret == Z_OK || ret == Z_BUF_ERROR);

        deflateEnd(&_zstream);
        _initialized = false;
    }

protected:
    int overflow(int ch) override
    {
        if (ch != EOF) {
            *pptr() = static_cast<char>(ch);
            pbump(1);
        }
        if (flushBuffer() == EOF) {
            return EOF;
        }
        return ch;
    }

    int sync() override
    {
        return flushBuffer() == EOF ? -1 : 0;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        std::streamsize written = 0;
        while (n > 0) {
            std::streamsize avail = epptr() - pptr();
            std::streamsize toWrite = std::min(n, avail);
            std::memcpy(pptr(), s, static_cast<size_t>(toWrite));
            pbump(static_cast<int>(toWrite));
            s += toWrite;
            n -= toWrite;
            written += toWrite;
            if (pptr() == epptr()) {
                if (flushBuffer() == EOF) {
                    break;
                }
            }
        }
        return written;
    }

private:
    int flushBuffer()
    {
        size_t dataSize = static_cast<size_t>(pptr() - pbase());
        if (dataSize == 0) {
            return 0;
        }

        _zstream.avail_in = static_cast<uInt>(dataSize);
        _zstream.next_in = reinterpret_cast<Bytef*>(_inBuf.data());

        do {
            _zstream.avail_out = static_cast<uInt>(_outBuf.size());
            _zstream.next_out = reinterpret_cast<Bytef*>(_outBuf.data());
            int ret = deflate(&_zstream, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR) {
                return EOF;
            }
            size_t have = _outBuf.size() - _zstream.avail_out;
            if (have > 0) {
                _os.write(_outBuf.data(), static_cast<std::streamsize>(have));
                if (!_os) {
                    return EOF;
                }
            }
        } while (_zstream.avail_out == 0);

        setp(_inBuf.data(), _inBuf.data() + _inBuf.size());
        return 0;
    }

    std::ostream& _os;
    z_stream _zstream {};
    bool _initialized = false;
    std::array<char, 16384> _inBuf {};
    std::array<char, 16384> _outBuf {};
};

}  // namespace

struct GZIPOutputStream::Impl
{
    GZIPStreamBuf buf;
    explicit Impl(std::ostream& os)
        : buf(os)
    {}
};

GZIPOutputStream::GZIPOutputStream(std::ostream& os)
    : std::ostream(nullptr)
    , d(std::make_unique<Impl>(os))
{
    rdbuf(&d->buf);
}

GZIPOutputStream::~GZIPOutputStream()
{
    try {
        close();
    }
    catch (...) {
    }
}

void GZIPOutputStream::close()
{
    d->buf.finalize();
}



bool CollectionCollection::addCollection(FileCollection& col)
{
    if (!col.isValid()) {
        return false;
    }
    _collections.push_back(&col);
    _valid = true;
    return true;
}

std::unique_ptr<FileCollection> CollectionCollection::clone() const
{
    auto cc = std::make_unique<CollectionCollection>();
    cc->_collections = _collections;
    cc->_valid = _valid;
    return cc;
}

ConstEntries CollectionCollection::entries() const
{
    ConstEntries result;
    for (auto* col : _collections) {
        auto ents = col->entries();
        result.insert(result.end(), ents.begin(), ents.end());
    }
    return result;
}

ConstEntryPointer CollectionCollection::getEntry(const std::string& name,
                                                  MatchPath matchpath) const
{
    for (auto* col : _collections) {
        auto e = col->getEntry(name, matchpath);
        if (e) {
            return e;
        }
    }
    return {};
}

std::unique_ptr<std::istream> CollectionCollection::getInputStream(const ConstEntryPointer& entry)
{
    for (auto* col : _collections) {
        auto e = col->getEntry(entry->getName());
        if (e) {
            return col->getInputStream(e);
        }
    }
    return nullptr;
}

std::unique_ptr<std::istream> CollectionCollection::getInputStream(const std::string& entry_name,
                                                                   MatchPath matchpath)
{
    for (auto* col : _collections) {
        auto e = col->getEntry(entry_name, matchpath);
        if (e) {
            return col->getInputStream(e);
        }
    }
    return nullptr;
}

void CollectionCollection::close()
{
    for (auto* col : _collections) {
        col->close();
    }
    _valid = false;
}


}  // namespace zipios
