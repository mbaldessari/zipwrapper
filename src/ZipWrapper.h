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
#include <unordered_map>
#include <vector>


/// @brief Zip I/O classes for reading and writing zip archives.
///
/// The namespace name "zipios" is retained for source compatibility with
/// existing call sites. This is **not** the zipios++ library; it is a thin
/// C++17 wrapper around libzip and zlib.
namespace zipios
{


/// @brief Exception thrown when a file collection operation fails.
///
/// Mirrors the old zipios++ exception hierarchy so that existing catch blocks
/// throughout FreeCAD continue to work without modification.
class FCollException: public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// @brief Exception thrown when an object is used in an invalid state.
///
/// For example, attempting to read from a ZipFile after it has been closed.
class InvalidStateException: public std::logic_error
{
public:
    using std::logic_error::logic_error;
};

/// @brief Exception thrown on I/O errors during zip operations.
///
/// Wraps std::ios_base::failure for consistency with the zipios++ API.
class IOException: public std::ios_base::failure
{
public:
    using std::ios_base::failure::failure;
};



/// @brief A streambuf backed by a std::vector<char>, supporting both read and write.
///
/// libzip gives us raw byte buffers, but the rest of FreeCAD expects
/// std::istream everywhere. This class bridges the gap. It also supports
/// write mode so it can collect data before handing it off to libzip.
class MemoryStreamBuf: public std::streambuf
{
public:
    /// @brief Construct an empty streambuf (write mode).
    MemoryStreamBuf() = default;

    /// @brief Construct a streambuf pre-loaded with data for reading.
    /// @param data The byte buffer to make readable. Ownership is taken via move.
    explicit MemoryStreamBuf(std::vector<char> data)
        : _data(std::move(data))
    {
        resetPointers();
    }

    /// @brief Write a sequence of characters to the buffer.
    /// @param s Pointer to the character sequence.
    /// @param n Number of characters to write.
    /// @return The number of characters written (always @p n).
    std::streamsize xsputn(const char* s, std::streamsize n) override
    {
        _data.insert(_data.end(), s, s + n);
        return n;
    }

    /// @brief Write a single character to the buffer.
    /// @param ch The character to write, or EOF.
    /// @return The character written, or EOF.
    int overflow(int ch) override
    {
        if (ch != EOF) {
            _data.push_back(static_cast<char>(ch));
        }
        return ch;
    }

    /// @brief Read a sequence of characters from the buffer.
    /// @param s Pointer to the destination buffer.
    /// @param n Maximum number of characters to read.
    /// @return The number of characters actually read.
    std::streamsize xsgetn(char* s, std::streamsize n) override
    {
        std::streamsize avail = egptr() - gptr();
        // Extra parens around std::min to stop MSVC's min macro from messing things up
        std::streamsize toRead = (std::min)(n, avail);
        std::memcpy(s, gptr(), static_cast<size_t>(toRead));
        gbump(static_cast<int>(toRead));
        return toRead;
    }

    /// @brief Peek at the next character without advancing the read position.
    /// @return The next character as an unsigned char, or EOF if no data remains.
    int underflow() override
    {
        if (gptr() < egptr()) {
            return static_cast<unsigned char>(*gptr());
        }
        return EOF;
    }

    /// @brief Seek to a position relative to the beginning, current, or end.
    /// @param off Offset from the reference point.
    /// @param way The reference point (beg, cur, or end).
    /// @param which Must include std::ios_base::in; output seeking is not supported.
    /// @return The new absolute position, or -1 on failure.
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

    /// @brief Seek to an absolute position.
    /// @param pos The target position.
    /// @param which Must include std::ios_base::in.
    /// @return The new absolute position, or -1 on failure.
    std::streampos seekpos(std::streampos pos, std::ios_base::openmode which) override
    {
        return seekoff(pos, std::ios_base::beg, which);
    }

    /// @brief Access the underlying data buffer (const).
    /// @return A const reference to the internal vector.
    const std::vector<char>& data() const
    {
        return _data;
    }

    /// @brief Access the underlying data buffer (mutable).
    /// @return A mutable reference to the internal vector.
    std::vector<char>& data()
    {
        return _data;
    }

    /// @brief Reset the read pointers to cover the entire buffer.
    ///
    /// Call this after modifying the internal data vector directly
    /// to make the new contents readable.
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

/// @brief An istream that owns its MemoryStreamBuf.
///
/// This allows returning an istream from a function without the caller
/// having to worry about the underlying buffer's lifetime.
class MemoryIStream: public std::istream
{
public:
    /// @brief Construct a readable stream from a byte buffer.
    /// @param data The data to read from. Ownership is taken via move.
    explicit MemoryIStream(std::vector<char> data)
        : std::istream(nullptr)
        , _buf(std::move(data))
    {
        rdbuf(&_buf);
    }

private:
    MemoryStreamBuf _buf;
};



/// @brief Represents a single entry (file or directory) inside a zip archive.
///
/// Carries the entry name and its index within the archive. Sizes and
/// timestamps are not tracked because FreeCAD never needed them.
class FileEntry
{
public:
    /// @brief Construct an invalid (empty) entry.
    FileEntry() = default;

    /// @brief Construct a named entry.
    /// @param name Full path of the entry inside the archive (e.g. "subdir/file.txt").
    /// @param index Zero-based index within the archive, or -1 if unknown.
    explicit FileEntry(std::string name, int index = -1)
        : _name(std::move(name))
        , _index(index)
        , _valid(true)
    {}

    /// @brief Get the full entry path (e.g. "subdir/file.txt").
    /// @return A const reference to the entry name.
    const std::string& getName() const
    {
        return _name;
    }

    /// @brief Alias for getName(); kept for API compatibility.
    /// @return A const reference to the entry name.
    const std::string& getFileName() const
    {
        return _name;
    }

    /// @brief Check whether this entry was properly initialized.
    /// @return @c true if this entry is valid, @c false otherwise.
    bool isValid() const
    {
        return _valid;
    }

    /// @brief Get the zero-based index of this entry within the archive.
    /// @return The entry index, or -1 if unknown.
    int getIndex() const
    {
        return _index;
    }

    /// @brief Check whether this entry represents a directory.
    /// @return @c true if the entry name ends with '/'.
    /// @note Zip convention: directory entries always end with '/'.
    bool isDirectory() const
    {
        return !_name.empty() && _name.back() == '/';
    }

    /// @brief Convert to string (returns the entry name).
    /// @return A const reference to the entry name.
    const std::string& toString() const
    {
        return _name;
    }

private:
    std::string _name;
    int _index = -1;
    bool _valid = false;
};

/// @brief Shared pointer to an immutable FileEntry.
using ConstEntryPointer = std::shared_ptr<const FileEntry>;
/// @brief A vector of ConstEntryPointer values.
using ConstEntries = std::vector<ConstEntryPointer>;



/// @brief A FileEntry subclass representing a zip central-directory entry.
///
/// In the original zipios++ library this carried central-directory fields.
/// Here it is kept as a thin subclass for API compatibility -- a few call
/// sites create ZipCDirEntry objects to pass to ZipOutputStream::putNextEntry().
class ZipCDirEntry: public FileEntry
{
public:
    /// @brief Construct an invalid entry.
    ZipCDirEntry() = default;

    /// @brief Construct a central-directory entry with the given filename.
    /// @param filename The entry name inside the archive.
    explicit ZipCDirEntry(const std::string& filename)
        : FileEntry(filename)
    {}
};



/// @brief Abstract base class for containers of zip entries.
///
/// Provides entry lookup (by full path or just filename) and the
/// getInputStream() interface that subclasses implement to deliver data.
class FileCollection
{
public:
    /// @brief Controls how entry names are matched in lookup methods.
    enum MatchPath
    {
        MATCH,       ///< Match the full path (e.g. "subdir/file.txt").
        IGNORE_PATH  ///< Match only the filename part, ignoring directories.
    };

    FileCollection() = default;
    /// @brief Copy constructor (default).
    FileCollection(const FileCollection&) = default;
    /// @brief Copy assignment operator (default).
    /// @return A reference to this object.
    FileCollection& operator=(const FileCollection&) = default;
    virtual ~FileCollection() = default;

    /// @brief Get all entries in this collection.
    /// @return A vector of shared pointers to immutable FileEntry objects.
    virtual ConstEntries entries() const
    {
        return _entries;
    }

    /// @brief Look up a single entry by name.
    /// @param name The entry name to search for.
    /// @param matchpath Whether to match the full path or just the filename.
    /// @return A shared pointer to the matching entry, or nullptr if not found.
    virtual ConstEntryPointer getEntry(const std::string& name,
                                       MatchPath matchpath = MATCH) const;

    /// @brief Get an input stream for reading the given entry's data.
    /// @param entry The entry to read.
    /// @return A unique_ptr to an istream, or nullptr if the entry is null.
    /// @throw InvalidStateException If the collection has been closed.
    /// @throw IOException If reading the entry data fails.
    virtual std::unique_ptr<std::istream> getInputStream(const ConstEntryPointer& entry) = 0;

    /// @brief Get an input stream for the entry with the given name.
    /// @param entry_name The name of the entry to read.
    /// @param matchpath Whether to match the full path or just the filename.
    /// @return A unique_ptr to an istream, or nullptr if no matching entry exists.
    /// @throw InvalidStateException If the collection has been closed.
    /// @throw IOException If reading the entry data fails.
    virtual std::unique_ptr<std::istream> getInputStream(const std::string& entry_name,
                                                         MatchPath matchpath = MATCH) = 0;

    /// @brief Check whether this collection is in a usable state.
    /// @return @c true if the collection is valid, @c false after close().
    virtual bool isValid() const
    {
        return _valid;
    }

    /// @brief Close the collection, releasing resources.
    ///
    /// After calling close(), isValid() returns @c false and further
    /// operations on the collection will throw.
    virtual void close()
    {
        _valid = false;
    }

    /// @brief Create a deep copy of this collection.
    /// @return A unique_ptr to the cloned collection.
    virtual std::unique_ptr<FileCollection> clone() const = 0;

    /// @brief Get the name of this collection (typically the archive file path).
    /// @return A const reference to the collection name.
    const std::string& getName() const
    {
        return _name;
    }

    /// @brief Get the number of entries in this collection.
    /// @return The entry count.
    size_t size() const
    {
        return _entries.size();
    }

protected:
    /// @brief Build the name-to-entry index from _entries.
    ///
    /// Subclasses must call this after populating _entries.
    void buildIndex()
    {
        _index.clear();
        _index.reserve(_entries.size());
        for (const auto& ep : _entries) {
            _index.emplace(ep->getName(), ep);
        }
    }

    // NOLINTBEGIN
    ConstEntries _entries;   ///< The list of entries in this collection.
    std::string _name;       ///< The name/path of this collection.
    bool _valid = false;     ///< Whether the collection is in a usable state.
    std::unordered_map<std::string, ConstEntryPointer> _index; ///< Name-to-entry lookup index.
    // NOLINTEND
};



/// @brief Opens a zip archive on disk for reading via libzip.
///
/// Entry contents are read into memory on demand via getInputStream(),
/// which is fine for .FCStd files since individual entries are rarely huge.
/// Not copyable because the instance holds a libzip handle; use clone()
/// if you need a second instance (it re-opens the file).
class ZipFile: public FileCollection
{
public:
    /// @brief Construct an invalid (empty) ZipFile.
    ZipFile();

    /// @brief Open a zip archive from a file on disk.
    /// @param name Path to the zip file.
    /// @param s_off Start offset (ignored; kept for API compatibility).
    /// @param e_off End offset (ignored; offset logic lives in ZipHeader).
    /// @throw FCollException If the file cannot be opened as a zip archive.
    explicit ZipFile(const std::string& name, int s_off = 0, int e_off = 0);
    ~ZipFile() override;

    ZipFile(const ZipFile&) = delete;
    ZipFile& operator=(const ZipFile&) = delete;

    /// @brief Move-construct a ZipFile, transferring ownership of the archive handle.
    /// @param other The source ZipFile (left invalid after the move).
    ZipFile(ZipFile&& other) noexcept;

    /// @brief Move-assign a ZipFile, transferring ownership of the archive handle.
    /// @param other The source ZipFile (left invalid after the move).
    /// @return A reference to this object.
    ZipFile& operator=(ZipFile&& other) noexcept;

    /// @brief Re-open the same file to create an independent copy.
    /// @return A new ZipFile reading the same archive, or an invalid ZipFile on failure.
    std::unique_ptr<FileCollection> clone() const override;

    /// @copydoc FileCollection::getInputStream(const ConstEntryPointer&)
    std::unique_ptr<std::istream> getInputStream(const ConstEntryPointer& entry) override;

    /// @copydoc FileCollection::getInputStream(const std::string&, MatchPath)
    std::unique_ptr<std::istream> getInputStream(const std::string& entry_name,
                                                 MatchPath matchpath = MATCH) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};



/// @brief Builds a zip archive in memory, flushing to disk or an ostream on close.
///
/// Entries are collected in memory and the complete zip is written when
/// close() or finish() is called. This matches how FreeCAD saves .FCStd
/// files: write each XML blob or binary chunk as a separate entry, then finalize.
///
/// @note Callers should call close() or finish() explicitly before destruction
///       if they need to handle I/O errors. The destructor calls close() but
///       swallows exceptions.
class ZipOutputStream: public std::ostream
{
public:
    /// @brief Create a ZipOutputStream that writes to a file.
    /// @param filename Path where the zip archive will be created.
    explicit ZipOutputStream(const std::string& filename);

    /// @brief Create a ZipOutputStream that writes into an existing ostream.
    /// @param os The target output stream. Data is buffered in memory until close().
    /// @note The caller must keep @p os alive until close() returns.
    explicit ZipOutputStream(std::ostream& os);

    ~ZipOutputStream() override;

    ZipOutputStream(const ZipOutputStream&) = delete;
    ZipOutputStream& operator=(const ZipOutputStream&) = delete;

    /// @brief Begin a new entry with the given name.
    ///
    /// Any previously open entry is finalized automatically. After this call,
    /// data written to the stream goes into the new entry.
    /// @param entryName The name for the new entry inside the archive.
    void putNextEntry(const std::string& entryName);

    /// @brief Begin a new entry from a ZipCDirEntry.
    /// @param entry The central-directory entry whose name is used.
    void putNextEntry(const ZipCDirEntry& entry);

    /// @brief Set the archive-level comment.
    /// @param comment The comment string.
    void setComment(const std::string& comment);

    /// @brief Set the compression level for subsequent entries.
    /// @param level zlib compression level (0 = store, 1-9 = deflate, -1 = default).
    void setLevel(int level);

    /// @brief Finalize the current entry without starting a new one.
    void closeEntry();

    /// @brief Finalize all entries and write the zip archive to the target.
    /// @throw IOException If the archive cannot be written (e.g. disk full).
    void close();

    /// @brief Alias for close().
    /// @throw IOException If the archive cannot be written.
    void finish();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};



/// @brief Sequential reader for zip archives.
///
/// Call getNextEntry() to advance through entries one by one. After each
/// call, reading from this stream yields that entry's decompressed data.
/// Used mainly by FreeCAD's document restore and merge-documents paths.
///
/// @note The entire archive is read into memory on construction.
class ZipInputStream: public std::istream
{
public:
    /// @brief Construct from an input stream containing zip data.
    /// @param is The input stream to read the archive from. The entire
    ///           stream is consumed into memory immediately.
    /// @throw IOException If the stream cannot be read or is not a valid zip.
    explicit ZipInputStream(std::istream& is);

    /// @brief Construct by opening a zip file on disk.
    /// @param filename Path to the zip file.
    /// @throw IOException If the file cannot be opened or is not a valid zip.
    explicit ZipInputStream(const std::string& filename);

    ~ZipInputStream() override;

    ZipInputStream(const ZipInputStream&) = delete;
    ZipInputStream& operator=(const ZipInputStream&) = delete;

    /// @brief Advance to the next entry in the archive.
    ///
    /// After this call, reading from the stream yields the entry's data.
    /// @return A shared pointer to the new current entry.
    /// @throw FCollException If there are no more entries.
    /// @throw IOException If the entry data cannot be read.
    ConstEntryPointer getNextEntry();

    /// @brief No-op; kept for zipios++ API compatibility.
    ///
    /// Entry advancement is handled entirely by getNextEntry().
    void closeEntry();

    /// @brief Close the underlying archive and release resources.
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};



/// @brief Wraps an ostream with on-the-fly gzip compression using zlib.
///
/// Used for writing GuiDocument.xml.gz inside .FCStd files. Write to it
/// like a normal stream and call close() when done to flush and finalize
/// the gzip trailer.
///
/// @note The caller must keep the wrapped ostream alive until close() returns.
///       The destructor calls close() but swallows exceptions.
class GZIPOutputStream: public std::ostream
{
public:
    /// @brief Construct a gzip-compressing stream that writes to @p os.
    /// @param os The target output stream for compressed data.
    /// @throw IOException If zlib initialization fails.
    explicit GZIPOutputStream(std::ostream& os);
    ~GZIPOutputStream() override;

    GZIPOutputStream(const GZIPOutputStream&) = delete;
    GZIPOutputStream& operator=(const GZIPOutputStream&) = delete;

    /// @brief Flush remaining data and finalize the gzip stream.
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};



/// @brief Aggregates multiple FileCollection instances under one roof.
///
/// Searches across all added collections are performed in insertion order;
/// the first match wins.
///
/// @note This class stores raw pointers, so the caller is responsible for
///       keeping the added collections alive for the lifetime of this object.
class CollectionCollection: public FileCollection
{
public:
    CollectionCollection() = default;

    /// @brief Add a collection to this aggregate.
    /// @param col The collection to add (must remain valid for the lifetime of this object).
    /// @return @c true if the collection was added (i.e. it was valid), @c false otherwise.
    bool addCollection(FileCollection& col);

    /// @brief Clone this aggregate (shallow -- the same raw pointers are copied).
    /// @return A new CollectionCollection pointing to the same underlying collections.
    std::unique_ptr<FileCollection> clone() const override;

    /// @brief Get all entries across all contained collections.
    /// @return A merged vector of entries from every contained collection.
    ConstEntries entries() const override;

    /// @copydoc FileCollection::getEntry
    ConstEntryPointer getEntry(const std::string& name,
                               MatchPath matchpath = MATCH) const override;

    /// @copydoc FileCollection::getInputStream(const ConstEntryPointer&)
    std::unique_ptr<std::istream> getInputStream(const ConstEntryPointer& entry) override;

    /// @copydoc FileCollection::getInputStream(const std::string&, MatchPath)
    std::unique_ptr<std::istream> getInputStream(const std::string& entry_name,
                                                 MatchPath matchpath = MATCH) override;

    /// @brief Close all contained collections.
    void close() override;

private:
    std::vector<FileCollection*> _collections;
};


}  // namespace zipios
