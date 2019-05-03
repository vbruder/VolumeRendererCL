# Description #

An interactive, cross platform volume raycaster based on the OpenCL compute API.
It features early ray termination, object and image order empty space skipping, local illumination, and various gradient based shading techniques.
It can display volume (timeseries) data sets and uses the [Qt](https://www.qt.io) framework for the GUI. 

# Setup and build #

To compile the code you need:

* An OpenCL 1.2 capable device and drivers/libraries with image support. It is recommended to update your GPU driver before building/running.
* Qt version 5.6 or higher.
* A compiler capable of c++14.
* CMake version 3.9 or higher.

Use CMake to build the volume rasycaster:
```
git clone https://theVall@bitbucket.org/theVall/basicvolumeraycaster.git
cd basicvolumeraycaster
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/Qt/install
make -j `nproc`
```
Make sure to replace the CMAKE_PREFIX_PATH with the path to your Qt install directory, e.g. ```/home/username/Qt/5.11.2/gcc_64/```

# Confirmed to build/run on the following configurations #

* NVIDIA Maxwell & Pascal, AMD Fiji & Vega, Intel Gen9 GPU & Skylake CPU
* GCC 5.3.1 & 7.3.0, Visual Studio 2015 (v140), Clang 6.0
* Qt 5.11.2
* CMake 3.10.2 & 3.12.2

# Screenshots #

![2019-05-02-richtmyer](https://github.com/vbruder/VolumeRendererCL/blob/develop/screenshots/2019-05-02-richtmyer_meshkov.png)

![2019-05-02-backpack](https://github.com/vbruder/VolumeRendererCL/blob/develop/screenshots/2019-05-02-backpack.png)

# Planned changes/extensions #

*  Out of core rendering for timeseries data on dGPUs.

# License #

Copyright (C) 2017-2018 Valentin Bruder vbruder@gmail.com

This software is licensed under [LGPLv3+](https://www.gnu.org/licenses/lgpl-3.0.en.html).

# Credits #
	
  * Color wheel from Mattia Basaglia's Qt-Color-Widgets: https://github.com/mbasaglia/Qt-Color-Widgets
  * OpenCL utils based on Erik Smistad's OpenCLUtilityLibrary: https://github.com/smistad/OpenCLUtilityLibrary
  * Transfer function editor based on Qt sample code.
