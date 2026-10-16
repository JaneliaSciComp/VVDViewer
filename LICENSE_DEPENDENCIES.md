# Licenses for software dependencies

## FlouRender

The [Flourender codebase](https://github.com/SCIInstitute/fluorender) from which VVDViewer was derived is licensed according to the MIT License as follows:

Copyright (c) 2014 Scientific Computing and Imaging Institute,
University of Utah.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

## Third-party libraries in the binary packages

The release packages (built with `package.ps1` / `package.sh`) bundle libraries that
vcpkg builds from source (wxWidgets, ffmpeg, libCZI, HDF5, Boost, libtiff, ...) and
libraries from the Vulkan SDK. Each package lists all of them with their licenses in
`THIRD_PARTY_NOTICES.txt` and contains every license text in `licenses/`; both are
generated from the vcpkg build (`packaging/collect_licenses.cmake`).

ffmpeg is built under the LGPL by default and is shipped, together with libCZI (LGPL),
as a separate dynamic library. Packages built with `-EnableGPL` / `--enable-gpl` also
contain the GPL x264/x265 encoders and are distributed under the GPL (file names end in
`-gpl`). The source code of the GPL/LGPL components is published next to each package
(`*-sources` archive).
