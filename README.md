# VVDViewer


This is the open-source repository for VVDViewer, an interactive rendering tool for confocal microscopy data visualization. It combines the rendering of multi-channel volume data and polygon mesh data, where the properties of each dataset can be adjusted independently and quickly. The tool is designed for neurobiologists, to better visualize the fluorescent-stained confocal samples. This code base started as a fork of [Fluorender](https://github.com/SCIInstitute/fluorender).

## Known Issues (for Mac)

If you encounter the message 'VVDViewer can't be opened because Apple cannot check it for malicious software' on your Mac, please right-click the app and choose 'Open' from the context menu to launch it. Although the same security notification will appear, you will have the option to open it.

Older versions of VVDViewer might not be compatible with the latest Mac OS, Sonoma. If you encounter any issues on Sonoma, please ensure that VVDViewer is updated to the most recent version. 
 

## Download 

Click this button to download the latest VVDViewer:

[![download](https://img.shields.io/static/v1?style=for-the-badge&logo=&label=&message=Download&color=0f83db)](https://github.com/JaneliaSciComp/VVDViewer/releases/latest)
 
If you get Vulkan-related errors, please try to update your graphics device driver.

## Documentation

### User Manual

https://github.com/JaneliaSciComp/VVDViewer/wiki
  
### Using VVDViewer for large volume segmentation (by Joshua Lillvis)

https://github.com/JaneliaSciComp/exllsm-circuit-reconstruction/blob/master/docs/NeuronSegmentation.md

Joshua L Lillvis, Hideo Otsuna, Xiaoyu Ding, Igor Pisarev, Takashi Kawase, Jennifer Colonell, Konrad Rokicki, Cristian Goina, Ruixuan Gao, Amy Hu, Kaiyu Wang, John Bogovic, Daniel E Milkie, Linus Meienberg, Brett D Mensh, Edward S Boyden, Stephan Saalfeld, Paul W Tillberg, Barry J Dickson (2022) Rapid reconstruction of neural circuits using tissue expansion and light sheet microscopy *eLife* [11:e81248](https://doi.org/10.7554/eLife.81248  )

## Licenses

VVDViewer is open source software licensed under the [BSD 3-Clause License](LICENSE). You can also view the [licenses for our dependencies](LICENSE_DEPENDENCIES.md).


## Building VVDViewer

### Automated build (recommended)

VVDViewer ships with a one-command auto-build that **downloads and builds every
third-party dependency from source** (via [vcpkg](https://github.com/microsoft/vcpkg))
and then builds the application with CMake. All dependencies are linked
**statically except wxWidgets**, which is built as a shared library (DLL/dylib/so).

The only dependency you must install yourself is the **Vulkan SDK**
(https://vulkan.lunarg.com/), so that the `VULKAN_SDK` environment variable is set.

Prerequisites:
 * Vulkan SDK (sets `VULKAN_SDK`)
 * CMake 3.21+ and Git
 * Windows: Visual Studio 2019/2022/2026 with the **"Desktop development with C++"** workload — this
   must include a **Windows 10/11 SDK** (it provides `rc.exe`/`mt.exe`, required to link native code).
   `build.ps1` auto-selects the newest Visual Studio installed (*including prereleases such as VS 2026*)
   and stops early with instructions if the Windows SDK is missing.
 * macOS: Xcode + command line tools
 * Linux: a C++ toolchain plus the system display dev packages (the script lists them), e.g.
   `sudo apt install build-essential pkg-config libgtk-3-dev libx11-dev libxcb1-dev libwayland-dev wayland-protocols libva-dev`

Then, from the repository root:

```powershell
# Windows (PowerShell)
.\build.ps1                 # Release build
.\build.ps1 -Config Debug -Clean
```

```bash
# macOS / Linux
bash build.sh               # Release build
bash build.sh --debug --clean
```

On the **first run** this downloads and compiles all dependencies, which typically takes
45–90 minutes; subsequent builds reuse the vcpkg binary cache and are incremental. vcpkg is
cloned into `./vcpkg` automatically (or set `VCPKG_ROOT` to reuse an existing checkout).

The finished application lands in `build/bin/<Config>/` (`VVDViewer.exe` on Windows, a
`VVDViewer.app` bundle on macOS). On Windows the wxWidgets DLLs are deployed next to the
executable automatically; every other dependency is linked statically into `VVDMainFrame`.

Notes:
 * The **Nikon ND2** reader depends on a proprietary, closed-source SDK and is therefore
   **disabled by default**. Pass `-EnableND2` (PowerShell) / `--enable-nd2` (shell) and
   provide the SDK via `-DND2_INCLUDE_DIR` / `-DND2_LIBRARY_DIR` to enable it.
 * The Zeiss **CZI** reader (libCZI) is built automatically by vcpkg and is enabled.

The manual, dependency-by-dependency instructions below remain available for advanced setups.

### Dependencies
 * Git (https://git-scm.com/)
 * CMake 3.21+ (http://www.cmake.org/)
 * wxWidgets (https://github.com/wxWidgets/wxWidgets)
 * Windows 7+ : Visual Studio 11.0 2012+
 * OSX 10.9+  : Latest Xcode and command line tools, homebrew
 * Other platforms may work, but are not officially supported.
 * Boost 1.59.0+ (http://www.boost.org/users/download/#live)

We recommend building VVDViewer outside of the source tree. <br/>
<h4>OSX</h4> 

1) Clone the latest wxWidgets using GIT (<code>git clone https://github.com/wxWidgets/wxWidgets.git</code>).
   
   * The steps following will assume the wxWidgets root directory is at <code>~/wxWidgets</code>

2) Build wxWidgets from the command line.
   * <code>cd ~/wxWidgets/</code>
   
   * <code>mkdir mybuild</code>
   
   * <code>cd mybuild</code>
   
   * <code>../configure --disable-shared --enable-macosx_arch=x86_64 --enable-unicode --with-cocoa --enable-debug --with-macosx-version-min=10.9 --enable-stl --enable-std_containers --enable-std_iostreams --enable-std_string --enable-std_string_conv_in_wxstring --with-libpng --with-libtiff --with-libjpeg</code>
   
   * <code>make</code>

   * <code>make install</code>

4) Get homebrew, boost, freetype, xz, libtiff, libjpeg, zlib, openssl and curl.

   * <code>ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"</code>

   * <code>brew install boost</code>

   * <code>brew install freetype</code>

   * <code>brew install xz</code>
   
   * <code>brew install libtiff</code>
   * <code>brew link libtiff --force</code>

   * <code>brew install jpeg</code>
   * <code>brew link jpeg --force</code>

   * <code>brew install zlib</code>
   * <code>brew link zlib --force</code>
   
   * <code>brew install openssl</code>
   * <code>brew link openssl --force</code>

   * <code>brew install curl --with-openssl</code>
   * <code>brew link curl --force</code>

5) Get and build VVDViewer

   * <code>git clone https://github.com/takashi310/VVD_Viewer.git</code><br/>
   
   * <code>mkdir build</code><br/>
   
   * <code>cd build</code><br/>

   * <code>cmake ../VVD_Viewer -G Xcode</code><br/>

5) Open the Xcode file generated to build and run VVDViewer.

<h4>Windows</h4> 

1) Clone the latest wxWidgets using GIT (<code>git clone git@github.com:wxWidgets/wxWidgets.git</code>).
   
   * The steps following will assume the wxWidgets repository is at <code>C:\wxWidgets</code>

2) Open a 64 bit Visual Studio command prompt to build wxWidgets. (make sure you use the prompt version you wish to build all dependencies, IE , MSVC 14.0 2015 x64)

   * Go to directory <code>C:\wxWidgets\build\msw</code>
  
   * Type <code>nmake /f makefile.vc TARGET_CPU=x64 BUILD=debug</code> to build debug libraries.

   * Type <code>nmake /f makefile.vc TARGET_CPU=x64 BUILD=release</code> to build release libraries.
   
3) Download and build boost.

   * Download boost (http://www.boost.org/users/download/#live) and extract onto your machine.
   
   * Build boost using <code>bootstrap.exe</code> and <code>b2.exe --toolset=msvc-11.0 --build-type=complete architecture=x86 address-model=64 stage</code> in the boost directory in a MSVC prompt. (change the toolset to the version of MSVC you are using, and omit address-model and architecture for 32-bit)
   
   * The steps following will assume the boost root directory is at <code>C:\boost_1_59_0</code> (your version might differ).

   * In a separate directory, checkout a separate boost process library : <code>git clone git@github.com:basisunus/boost_process.git</code> and merge the contents into the boost directory you built in.

4) Build or Download libjpeg, libtiff, openssl, zlib, openssl and curl.
   * If you use Visual Studio 12.0 2013+, also build ffmpeg.

5) You may need to add lines to <code>C:\Program Files (x86)\CMake X.X\share\cmake-x.x\Modules\FindwxWidgets.cmake</code> (x's are your version) for wxWidgets 3.* if it still complains that you haven't installed wxWidgets.
   
   * Starting about line 277, you will have listed a few sets of library versions to search for like <code>wxbase29${_UCD}${_DBG}</code> <br/>
   
   * In 4 places, you will need to add above each line with a "29" a new line that is exactly the same, but with a "31" instead, assuming your version of wxWidgets is 3.1.*). <br/>

6) Download VVDViewer using Git <code>git clone https://github.com/takashi310/VVD_Viewer.git</code>

7) Use the <code>C:\Program Files(x86)\CMake2.8\bin\cmake-gui.exe</code> program to configure build properties and generate your Visual Studio Solution file. (Remember to keep your MSVC version consistent)
   
   * Select your VVDViewer source and build directories (create a new folder for building), and add the locations of boost and wxWidgets. <br/>
   	- Choose the VVDViewer main folder for source and create a new folder for the build. <br/>

   	- Click Configure.  NOTE: You may need to display advanced options to set below options. <br/>
   	
   	- Choose the build type <code>CMAKE_BUILD_TYPE</code> to be "Debug" or "Release" <br/>

   	- Be sure to set <code>wxWidgets_LIB_DIR</code> to <code>C:\wxWidgets\lib\vc_x64_lib</code>. (this will differ from 32 bit)
   	
   	- Be sure to set <code>wxWidgets_ROOT_DIR</code> to <code>C:\wxWidgets</code>.
   	
   	- Be sure to set <code>Boost_INCLUDE_DIR</code> to <code>C:\boost_1_59_0</code> (assuming this is your boost dir). <br/>

	- Be sure to set <code>Boost_INCLUDE_DIR</code> to <code>C:\boost_1_59_0</code> (assuming this is your boost dir). <br/>

	- Be sure to set <code>Boost_CHRONO_LIBRARY_DEBUG</code> to <code>C:\boost_1_59_0\x64\lib\libboost_chrono-vc110-mt-gd-1_59.lib</code> (assuming this is your boost dir). <br/>

	- Be sure to set <code>Boost_CHRONO_LIBRARY_RELEASE</code> to <code>C:\boost_1_59_0\x64\lib\libboost_chrono-vc110-mt-s-1_59.lib</code> (assuming this is your boost dir). <br/>

	- Be sure to set <code>Boost_SYSTEM_LIBRARY_DEBUG</code> to <code>C:\boost_1_59_0\x64\lib\libboost_system-vc110-mt-gd-1_59.lib</code> (assuming this is your boost dir). <br/>

	- Be sure to set <code>Boost_SYSTEM_LIBRARY_RELEASE</code> to <code>C:\boost_1_59_0\x64\lib\libboost_system-vc110-mt-s-1_59.lib</code> (assuming this is your boost dir). <br/>

	- Be sure to set <code>CURL_INCLUDE_DIR</code> to <code>C:\libcurl</code> (assuming this is your libcurl dir). <br/>

	- Be sure to set <code>CURL_LIBRARY</code> to <code>C:\libcurl\libcurl.lib</code> (assuming this is your libcurl dir). <br/>

	- Be sure to set <code>JPEG_INCLUDE_DIR</code> to <code>C:\libjpeg</code> (assuming this is your libjpeg dir). <br/>

	- Be sure to set <code>JPEG_LIBRARY</code> to <code>C:\libjpeg\jpeg.lib</code> (assuming this is your libjpeg dir). <br/>

	- Be sure to set <code>ZLIB_INCLUDE_DIR</code> to <code>C:\zlib</code> (assuming this is your zlib dir). <br/>

	- Be sure to set <code>ZLIB_LIBRARY</code> to <code>C:\zlib\zlib.lib</code> (assuming this is your zlib dir). <br/>

	- Be sure to set <code>LIB_EAY_DEBUG</code> and <code>LIB_EAY_RELEASE</code> to <code>C:\openssl\lib\libeay32.lib</code> (assuming this is your openssl dir). <br/>

	- Be sure to set <code>SSL_EAY_DEBUG</code> and <code>SSL_EAY_RELEASE</code> to <code>C:\openssl\lib\ssleay32.lib</code> (assuming this is your openssl dir). <br/>

	- If you use Visual Studio 12.0 2013+, delete all files in <code>VVD_Viewer\fluorender\ffmpeg\Win64</code> and copy your ffmpeg files in <code>C:\ffmpeg</code> (assuming this is your ffmpeg dir). <br/>
   	
   	- Click Generate. 

   * You may also generate using the command prompt, but you must explicitly type the paths for the cmake command. <br/>
   
    - Open Visual Studio Command Prompt. Go to the CMakeLists.txt directory. <br/>
    	
    - Type <code> cmake -G "Visual Studio 14 2015 Win64" -DwxWidgets_LIB_DIR="C:\wxWidgets\lib\vc_x64_lib" -DwxWidgets_ROOT_DIR="C:\wxWidgets" -DBoost_INCLUDE_DIR="C:\boost_1_55_0" -DCMAKE_BUILD_TYPE="Debug" ..</code> in your build directory (again assuming these are your directory locations / Generator versions, and the build folder is in the VVDViewer root directory). <br/>
    	
   * Open the Visual Studio SLN file generated by CMake (found in your "build" directory). <br/>
   
   * Build the solution. Use CMake to generate both "Release" and "Debug" configurations if you wish to build both in Visual Studio.<br/><br/>
    	**Notes for Visual Studio**
    - Visual Studio may not set the correct machine target when building 64 bit. 
     Check <code>Project Properties -> Configuration Properties -> Linker -> Command line</code>. Make sure "Additional Options" is <code>/machine:X64</code> NOT <code>/machine:X86</code>. <br/>
    	
    - You may need to right-click VVD_Viewer project on the Solution Explorer to "Set as StartUp Project" for it to run. <br/>
    - If you are building on Windows 8 or later, you will need to set a Visual Studio Graphics Option. This enables the application to build in higher definition.<br/>
      <code>Project Properties -> Manifest Tool -> Input and Output -> Enable DPI Awareness -> Yes </code> <br/>
