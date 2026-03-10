// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2022 Werner Mayer <wmayer[at]users.sourceforge.net>     *
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


#include "ZipHeader.h"

#include <algorithm>
#include <iterator>
#include <vector>
#include <zip.h>


using zipios::ConstEntryPointer;
using zipios::FileCollection;
using zipios::ZipHeader;


struct ZipHeader::Impl
{
    std::vector<char> archiveData;
    zip_source_t* source = nullptr;
    zip_t* archive = nullptr;

    ~Impl()
    {
        if (archive) {
            zip_close(archive);
        }
    }
};


ZipHeader::ZipHeader(std::istream& inp, int s_off, int e_off)
    : d(std::make_unique<Impl>())
{
    // Determine size of stream portion
    inp.seekg(0, std::ios::end);
    auto totalSize = inp.tellg();

    auto startPos = static_cast<std::streampos>(s_off);
    auto endPos = totalSize - static_cast<std::streamoff>(e_off);
    auto dataSize = endPos - startPos;

    if (dataSize <= 0) {
        _valid = false;
        return;
    }

    // Read the relevant portion into memory
    d->archiveData.resize(static_cast<size_t>(dataSize));
    inp.seekg(startPos);
    inp.read(d->archiveData.data(), dataSize);

    if (!inp) {
        _valid = false;
        return;
    }

    // Open the zip from the in-memory buffer
    zip_error_t zerr;
    zip_error_init(&zerr);
    d->source = zip_source_buffer_create(d->archiveData.data(),
                                         d->archiveData.size(), 0, &zerr);
    if (!d->source) {
        zip_error_fini(&zerr);
        _valid = false;
        return;
    }

    d->archive = zip_open_from_source(d->source, ZIP_RDONLY, &zerr);
    if (!d->archive) {
        zip_source_free(d->source);
        d->source = nullptr;
        zip_error_fini(&zerr);
        throw zipios::FCollException("Unable to find zip structure in stream");
    }
    zip_error_fini(&zerr);

    // Build entries list
    zip_int64_t numEntries = zip_get_num_entries(d->archive, 0);
    _entries.reserve(static_cast<size_t>(numEntries));
    for (zip_int64_t i = 0; i < numEntries; ++i) {
        const char* name = zip_get_name(d->archive, static_cast<zip_uint64_t>(i), 0);
        if (name) {
            _entries.push_back(
                std::make_shared<zipios::FileEntry>(name, static_cast<int>(i)));
        }
    }

    _valid = true;
}

ZipHeader::~ZipHeader() = default;

std::unique_ptr<FileCollection> ZipHeader::clone() const
{
    // Cannot deep-copy; return an invalid collection
    return std::unique_ptr<FileCollection>(new ZipHeader());
}

void ZipHeader::close()
{
    _valid = false;
    if (d->archive) {
        zip_close(d->archive);
        d->archive = nullptr;
    }
}

std::unique_ptr<std::istream> ZipHeader::getInputStream(const ConstEntryPointer& entry)
{
    if (!_valid) {
        throw zipios::InvalidStateException("Attempt to use an invalid FileCollection");
    }
    if (!entry) {
        return nullptr;
    }
    return getInputStream(entry->getName());
}

std::unique_ptr<std::istream> ZipHeader::getInputStream(const std::string& entry_name,
                                                        MatchPath matchpath)
{
    if (!_valid) {
        throw zipios::InvalidStateException("Attempt to use an invalid ZipHeader");
    }

    auto ent = getEntry(entry_name, matchpath);
    if (!ent) {
        return nullptr;
    }

    auto idx = static_cast<zip_uint64_t>(ent->getIndex());

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(d->archive, idx, 0, &stat) < 0) {
        throw zipios::IOException("Failed to stat entry: " + entry_name);
    }

    zip_file_t* zf = zip_fopen_index(d->archive, idx, 0);
    if (!zf) {
        throw zipios::IOException(std::string("Failed to open entry: ") + entry_name
                                  + " (" + zip_strerror(d->archive) + ")");
    }

    std::vector<char> buf(static_cast<size_t>(stat.size));
    zip_int64_t bytesRead = zip_fread(zf, buf.data(), stat.size);
    if (bytesRead < 0) {
        std::string err = zip_file_strerror(zf);
        zip_fclose(zf);
        throw zipios::IOException("Failed to read entry: " + entry_name + " (" + err + ")");
    }
    zip_fclose(zf);
    buf.resize(static_cast<size_t>(bytesRead));

    return std::make_unique<zipios::MemoryIStream>(std::move(buf));
}

// Private default constructor for clone()
ZipHeader::ZipHeader()
    : d(std::make_unique<Impl>())
{
    _valid = false;
}
