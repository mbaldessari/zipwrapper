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

#pragma once

#include <cstring>
#include <istream>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>


// Note: namespace "zipios" is retained for source compatibility with existing call sites.
// This is NOT the zipios++ library; it is a thin wrapper around libzip.
namespace zipios
{


/// These mirror the old zipios++ exception hierarchy.  We keep the same
/// names so that catch blocks throughout FreeCAD don't need touching.
class FCollException: public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class InvalidStateException: public std::logic_error
{
public:
    using std::logic_error::logic_error;
};

class IOException: public std::ios_base::failure
{
public:
    using std::ios_base::failure::failure;
};



/// Simple streambuf that sits on top of a vector<char>.  We need this
/// because libzip gives us raw byte buffers and the rest of FreeCAD
/// expects std::istream everywhere.  Supports both read and write so
/// we can also use it to collect data before handing it off to libzip.
class MemoryStreamBuf: public std::streambuf
{
public:
    MemoryStreamBuf() = default;

    explicit MemoryStreamBuf(std::vector<char> data)
        : _data(std::move(data))
    {
        resetPointers();
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        _data.insert(_data.end(), s, s + n);
        return n;
    }

    int overflow(int ch) override
    {
        if (ch != EOF) {
            _data.push_back(static_cast<char>(ch));
        }
        return ch;
    }

    std::streamsize xsgetn(char* s, std::streamsize n) override
    {
        std::streamsize avail = egptr() - gptr();
        // Extra parens around std::min to stop MSVC's min macro from messing things up
        std::streamsize toRead = (std::min)(n, avail);
        std::memcpy(s, gptr(), static_cast<size_t>(toRead));
        gbump(static_cast<int>(toRead));
        return toRead;
    }

    int underflow() override
    {
        if (gptr() < egptr()) {
            return static_cast<unsigned char>(*gptr());
        }
        return EOF;
    }

    std::streampos seekoff(std::streamoff off,
                           std::ios_base::seekdir way,
                           std::ios_base::openmode which) override
    {
        std::streampos newPos;
        if (which & std::ios_base::in) {
            if (way == std::ios_base::beg) {
                newPos = off;
            }
            else if (way == std::ios_base::cur) {
                newPos = (gptr() - eback()) + off;
            }
            else {
                newPos = (egptr() - eback()) + off;
            }
            if (newPos < 0 || newPos > (egptr() - eback())) {
                return std::streampos(-1);
            }
            setg(eback(), eback() + newPos, egptr());
            return newPos;
        }
        return std::streampos(-1);
    }

    std::streampos seekpos(std::streampos pos, std::ios_base::openmode which) override
    {
        return seekoff(pos, std::ios_base::beg, which);
    }

    const std::vector<char>& data() const
    {
        return _data;
    }
    std::vector<char>& data()
    {
        return _data;
    }

    void resetPointers()
    {
        if (_data.empty()) {
            setg(nullptr, nullptr, nullptr);
            return;
        }
        char* base = _data.data();
        setg(base, base, base + _data.size());
    }

private:
    std::vector<char> _data;
};

/// Convenience wrapper: an istream that owns its MemoryStreamBuf so you
/// can return it from a function without worrying about the buffer lifetime.
class MemoryIStream: public std::istream
{
public:
    explicit MemoryIStream(std::vector<char> data)
        : std::istream(nullptr)
        , _buf(std::move(data))
    {
        rdbuf(&_buf);
    }

private:
    MemoryStreamBuf _buf;
};



/// Represents a single entry (file or directory) inside a zip archive.
/// Mostly just carries the name and index -- we don't bother with sizes
/// or timestamps since FreeCAD never needed those from zipios++ either.
class FileEntry
{
public:
    FileEntry() = default;
    explicit FileEntry(std::string name, int index = -1)
        : _name(std::move(name))
        , _index(index)
        , _valid(true)
    {}

    /// Returns the full entry path (e.g. "subdir/file.txt").
    std::string getName() const
    {
        return _name;
    }
    /// Alias for getName(); kept for API compatibility.
    std::string getFileName() const
    {
        return _name;
    }
    bool isValid() const
    {
        return _valid;
    }
    int getIndex() const
    {
        return _index;
    }
    /// Zip convention: directories end with '/'
    bool isDirectory() const
    {
        return !_name.empty() && _name.back() == '/';
    }
    std::string toString() const
    {
        return _name;
    }

private:
    std::string _name;
    int _index = -1;
    bool _valid = false;
};

using ConstEntryPointer = std::shared_ptr<const FileEntry>;
using ConstEntries = std::vector<ConstEntryPointer>;



/// Thin subclass of FileEntry that used to hold central-directory fields
/// in zipios++.  Kept around because a few call sites create ZipCDirEntry
/// objects to pass to putNextEntry().
class ZipCDirEntry: public FileEntry
{
public:
    ZipCDirEntry() = default;
    explicit ZipCDirEntry(const std::string& filename)
        : FileEntry(filename)
    {}
};



/// Base class for anything that contains a bunch of zip entries.  Provides
/// entry lookup (by full path or just filename) and the getInputStream()
/// interface that subclasses implement to actually hand out data.
class FileCollection
{
public:
    enum MatchPath
    {
        MATCH,
        IGNORE_PATH
    };

    FileCollection() = default;
    FileCollection(const FileCollection&) = default;
    FileCollection& operator=(const FileCollection&) = default;
    virtual ~FileCollection() = default;

    virtual ConstEntries entries() const
    {
        return _entries;
    }

    virtual ConstEntryPointer getEntry(const std::string& name,
                                       MatchPath matchpath = MATCH) const;

    virtual std::unique_ptr<std::istream> getInputStream(const ConstEntryPointer& entry) = 0;
    virtual std::unique_ptr<std::istream> getInputStream(const std::string& entry_name,
                                                         MatchPath matchpath = MATCH) = 0;

    virtual bool isValid() const
    {
        return _valid;
    }

    virtual void close()
    {
        _valid = false;
    }

    virtual std::unique_ptr<FileCollection> clone() const = 0;

    std::string getName() const
    {
        return _name;
    }

    size_t size() const
    {
        return _entries.size();
    }

protected:
    // NOLINTBEGIN
    ConstEntries _entries;
    std::string _name;
    bool _valid = false;
    // NOLINTEND
};



/// Opens a zip archive for reading via libzip.  Entry contents are read
/// into memory on demand (getInputStream), which is fine for .FCStd files
/// since individual entries are rarely huge.  Not copyable because we hold
/// a libzip handle; use clone() if you need a second instance.
class ZipFile: public FileCollection
{
public:
    ZipFile();
    /// Open a zip file.  s_off/e_off are leftovers from the zipios++ API,
    /// we just ignore them here -- offset logic lives in ZipHeader now.
    explicit ZipFile(const std::string& name, int s_off = 0, int e_off = 0);
    ~ZipFile() override;

    ZipFile(const ZipFile&) = delete;
    ZipFile& operator=(const ZipFile&) = delete;
    ZipFile(ZipFile&& other) noexcept;
    ZipFile& operator=(ZipFile&& other) noexcept;

    std::unique_ptr<FileCollection> clone() const override;
    std::unique_ptr<std::istream> getInputStream(const ConstEntryPointer& entry) override;
    std::unique_ptr<std::istream> getInputStream(const std::string& entry_name,
                                                 MatchPath matchpath = MATCH) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};



/// Builds a zip archive by collecting entries in memory and then flushing
/// the whole thing to disk (or to an ostream) when close()/finish() is
/// called.  This matches how FreeCAD saves .FCStd files: write each XML
/// blob or binary chunk as a separate entry, then finalize.
class ZipOutputStream: public std::ostream
{
public:
    explicit ZipOutputStream(const std::string& filename);
    /// Write zip data into an existing ostream (buffered in memory until close)
    explicit ZipOutputStream(std::ostream& os);
    ~ZipOutputStream() override;

    ZipOutputStream(const ZipOutputStream&) = delete;
    ZipOutputStream& operator=(const ZipOutputStream&) = delete;

    void putNextEntry(const std::string& entryName);
    void putNextEntry(const ZipCDirEntry& entry);
    void setComment(const std::string& comment);
    void setLevel(int level);
    void closeEntry();
    void close();
    void finish();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};



/// Sequential reader for zip archives.  Call getNextEntry() to advance
/// through entries one by one -- after each call, reading from this stream
/// gives you that entry's data.  Used mainly by Document restore and the
/// merge-documents path.
class ZipInputStream: public std::istream
{
public:
    explicit ZipInputStream(std::istream& is);
    explicit ZipInputStream(const std::string& filename);
    ~ZipInputStream() override;

    ZipInputStream(const ZipInputStream&) = delete;
    ZipInputStream& operator=(const ZipInputStream&) = delete;

    ConstEntryPointer getNextEntry();
    void closeEntry();
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};



/// Wraps an ostream with on-the-fly gzip compression using zlib.
/// Used for writing the GuiDocument.xml.gz inside .FCStd files.
/// Just write to it like a normal stream and call close() when done.
class GZIPOutputStream: public std::ostream
{
public:
    explicit GZIPOutputStream(std::ostream& os);
    ~GZIPOutputStream() override;

    GZIPOutputStream(const GZIPOutputStream&) = delete;
    GZIPOutputStream& operator=(const GZIPOutputStream&) = delete;

    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};



/// Aggregates multiple FileCollections under one roof.  Keeps raw pointers
/// so the caller is responsible for keeping the added collections alive.
class CollectionCollection: public FileCollection
{
public:
    CollectionCollection() = default;

    bool addCollection(FileCollection& col);
    std::unique_ptr<FileCollection> clone() const override;

    ConstEntries entries() const override;
    ConstEntryPointer getEntry(const std::string& name,
                               MatchPath matchpath = MATCH) const override;
    std::unique_ptr<std::istream> getInputStream(const ConstEntryPointer& entry) override;
    std::unique_ptr<std::istream> getInputStream(const std::string& entry_name,
                                                 MatchPath matchpath = MATCH) override;
    void close() override;

private:
    std::vector<FileCollection*> _collections;
};


}  // namespace zipios
