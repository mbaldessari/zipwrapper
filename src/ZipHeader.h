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

/** ZipHeader is a FileCollection, where the files are stored in a .zip file.
 * The class is similar to ZipFile with the difference that instead of
 * a file a std::istream can be passed.
 */
class ZipHeader: public FileCollection
{
public:
    /** Opens the zip file name. If the zip "file" is
        embedded in a file that contains other data, e.g. a binary
        program, the offset of the zip file start and end must be
        specified.
        @param inp The input stream of the zip file to open.
        @param s_off Offset relative to the start of the file, that
        indicates the beginning of the zip file.
        @param e_off Offset relative to the end of the file, that
        indicates the end of the zip file. The offset is a positive number,
        even though the offset is towards the beginning of the file.
        @throw FCollException Thrown if the specified file name is not a valid zip
        archive.
        @throw IOException Thrown if an I/O problem is encountered, while the directory
        of the specified zip archive is being read. */
    explicit ZipHeader(std::istream& inp, int s_off = 0, int e_off = 0);
    ~ZipHeader() override;

    ZipHeader(const ZipHeader&) = delete;
    ZipHeader& operator=(const ZipHeader&) = delete;

    /** Create a copy of this instance. */
    std::unique_ptr<FileCollection> clone() const override;

    void close() override;

    std::unique_ptr<std::istream> getInputStream(const ConstEntryPointer& entry) override;
    std::unique_ptr<std::istream> getInputStream(const std::string& entry_name,
                                                 MatchPath matchpath = MATCH) override;

private:
    ZipHeader();  // for clone()
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace zipios
