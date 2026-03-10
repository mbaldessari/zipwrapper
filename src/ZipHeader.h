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


#pragma once

#include "ZipWrapper.h"

namespace zipios
{

/// @brief A FileCollection that reads a zip archive from a std::istream.
///
/// Similar to ZipFile but accepts a stream instead of a file path.
/// This is useful when the zip data is embedded inside a larger file
/// (e.g. a binary program with an appended zip payload) -- the start
/// and end offsets allow extracting just the zip portion.
///
/// @note The entire relevant portion of the stream is copied into memory
///       on construction.
class ZipHeader: public FileCollection
{
public:
    /// @brief Open a zip archive from an input stream.
    ///
    /// If the zip data is embedded inside a larger file, use @p s_off
    /// and @p e_off to delimit the zip region.
    ///
    /// @param inp  The input stream containing zip data.
    /// @param s_off Byte offset from the start of the stream where the
    ///              zip data begins (default 0).
    /// @param e_off Byte offset from the **end** of the stream where the
    ///              zip data ends. This is a positive number even though
    ///              the offset is towards the beginning of the file (default 0).
    /// @throw FCollException If the stream does not contain a valid zip archive
    ///                       at the specified offsets.
    explicit ZipHeader(std::istream& inp, int s_off = 0, int e_off = 0);
    ~ZipHeader() override;

    ZipHeader(const ZipHeader&) = delete;
    ZipHeader& operator=(const ZipHeader&) = delete;

    /// @brief Create a copy of this instance.
    /// @return An invalid (empty) ZipHeader, since deep-copying the
    ///         underlying libzip state is not supported.
    std::unique_ptr<FileCollection> clone() const override;

    /// @brief Close the archive and release resources.
    void close() override;

    /// @copydoc FileCollection::getInputStream(const ConstEntryPointer&)
    std::unique_ptr<std::istream> getInputStream(const ConstEntryPointer& entry) override;

    /// @copydoc FileCollection::getInputStream(const std::string&, MatchPath)
    std::unique_ptr<std::istream> getInputStream(const std::string& entry_name,
                                                 MatchPath matchpath = MATCH) override;

private:
    ZipHeader();  // for clone()
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace zipios
