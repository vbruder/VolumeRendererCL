# Description #

An interactive, cross platform volume renderer (ray caster and path tracer) based on the OpenCL compute API.
It features early ray termination, object order and image order empty space skipping, local illumination, and various gradient based shading techniques.
The rederer is designed to run on the GPU in single node environments.
Execution on CPU is possible but not recommended due to severe performance issues.

The code is structured in the following parts:
- *kernel*: OpenCL C parallel volume rendering kernel
- *core*: C++ interface to volume rendering kernel
- *oclutils*: utilities for setting up OpenCL and OpenCL-OpenGL interop
- *io*: volume data file reader (at the moment only dat/raw format)
- *qt*: everything GUI related: OpenGL screen quad rendering, mouse/keyboard interaction, transfer function editor, color picker, parameter controls, histogram rendering... 

This renderer is primarily used for as a basis for my research projects.
Therefore, the code may be lacking documentation, automated testing, and portability.
However, I try to write/keep clean and understandable code and test on different systems.

## Dependencies / Requirements ##

- [Qt](https://www.qt.io) 5.10 or later for the GUI.
- [CMake](https://cmake.org) 3.9 or later for building.
- C++14 compiler
- OpenCL 1.2 (capable device & drivers, headers, [C++ bindings](https://github.com/KhronosGroup/OpenCL-CLHPP/releases))
- OpenGL for dispaying the texture generated with OpenCL
- OpenMP 4 (recommended for faster loading/initialization)

## Volume data ##

The renderer can display volume (timeseries) data sets.
An excellent resource for those is Pavol Klacansky's [Open Scientific Visualization Datasets](https://klacansky.com/open-scivis-datasets/) page.
For data loading you should create and select a .dat file containing a description of the raw (binary) volume data following this scheme:

```
ObjectFileName: 	relative/path/to/binaryData.raw
Resolution: 		256 256 256 
SliceThickness:		1.0 1.0 1.0
Format: 		UCHAR
```

The `ObjectFileName` may contain multiple paths to different time steps.
Alternatively, `Resolution` may be extended with a fourth dimension if the raw file names of the timesteps contain a suffix with ascending numbering.
Currently supported formats are: `UCHAR`, `USHORT`, and `FLOAT`

## Setup and build ##

Use CMake to build the volume renderer:
```
git clone https://github.com/vbruder/VolumeRendererCL.git
cd VolumeRendererCL
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt/install
make -j `nproc`
```
Make sure to replace the CMAKE_PREFIX_PATH with the path to your Qt install directory, e.g. ```/home/username/Qt/5.11.2/gcc_64/```

## Sytem and hardware ##

Currently, I develop and test on an Ubuntu 18.04 based Linux using GCC 7.4.0 and an NVIDIA Titan X (Pascal).
Sporadically, I try to also test on a Windows 10 system using the Visual Studio 17 compiler and an AMD Vega FE, as well as on my integrated Intel Gen9 GPU (NEO driver stack).

Systems/hardware I successfully ran this renderer on: 
* NVIDIA Maxwell & Pascal, AMD Fiji & Vega, Intel Gen9 GPU & Skylake CPU
* GCC 5.3.1 & 7.4.0, Visual Studio 2017 (v140), Clang 6.0
* Qt 5.11.2 & 5.12.2
* CMake 3.10.2 & 3.12.2

## Screenshots ##

![2019-05-02-richtmyer](https://github.com/vbruder/VolumeRendererCL/blob/develop/screenshots/2019-05-02-richtmyer_meshkov.png)

![2019-05-02-backpack](https://github.com/vbruder/VolumeRendererCL/blob/develop/screenshots/2019-05-02-backpack.png)

## Planned changes/extensions ##

*  Out of core rendering for timeseries data on dGPUs.

## License ##

Copyright (C) 2017-2019 Valentin Bruder vbruder@gmail.com

This software is licensed under [LGPLv3+](https://www.gnu.org/licenses/lgpl-3.0.en.html).

## Credits ##
	
  * Color wheel from Mattia Basaglia's Qt-Color-Widgets: https://github.com/mbasaglia/Qt-Color-Widgets
  * OpenCL utils based on Erik Smistad's OpenCLUtilityLibrary: https://github.com/smistad/OpenCLUtilityLibrary
  * Transfer function editor based on Qt sample code.
