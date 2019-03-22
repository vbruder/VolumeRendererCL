/**
 * \file
 *
 * \author Valentin Bruder
 *
 * \copyright Copyright (C) 2018 Valentin Bruder
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "src/core/volumerendercl.h"

#include <functional>
#include <algorithm>
#include <numeric>

#include <omp.h>

static const size_t LOCAL_SIZE = 8;    // 8*8=64 is wavefront size or 2*warp size

/**
 * @brief RoundPow2
 * @param iNumber
 * @return
 */
static unsigned int RoundPow2(unsigned int n)
{
    // next highest power of 2
    // (cf: http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2)
    unsigned int val = n - 1u;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val++;
    // previous power of 2
    unsigned int x = val >> 1;
    // round to nearest of the two
    return (val - n) > (n - x) ? x : val;
}


/**
 * @brief VolumeRenderCL::VolumeRenderCL
 */
VolumeRenderCL::VolumeRenderCL() :
    _volLoaded(false)
  , _lastExecTime(0.0)
  , _modelScale{1.0, 1.0, 1.0}
  , _useGL(true)
  , _useImgESS(false)
{
    std::mt19937 _generator(42);
}


/**
 * @brief VolumeRenderCL::~VolumeRenderCL
 */
VolumeRenderCL::~VolumeRenderCL()
{
}


/**
 * @brief VolumeRenderCL::logCLerror
 * @param error
 */
void VolumeRenderCL::logCLerror(cl::Error error)
{
    std::cerr << "Error in " << error.what() << ": "
              << getCLErrorString(error.err()) << std::endl;
    throw std::runtime_error( "ERROR: " + std::string(error.what()) + " ("
                              + getCLErrorString(error.err()) + ")");
}


/**
 * @brief VolumeRenderCL::initialize
 */
void VolumeRenderCL::initialize(bool useGL, bool useCPU, cl_vendor vendor,
                                const std::string deviceName, const int platformId)
{
    cl_device_type type = useCPU ? CL_DEVICE_TYPE_CPU : CL_DEVICE_TYPE_GPU;
    try // opencl scope
    {
        // FIXME: Using CPU segfaults on most tff changes - too many enques for down sampling?
        if (useGL && !useCPU)
        {
            _useGL = useGL;
            _contextCL = createCLGLContext(_currentDevice, type, vendor);
        }
        else
        {
            if (useGL)
                std::cout << "Cannot use OpenGL context sharing with CPU devices. "
                          << "Using buffer generation instead." << std::endl;
            if (deviceName.empty())
                _contextCL = createCLContext(type, vendor);
            else if (platformId >= 0)
            {
                std::vector<cl::Platform> platforms;
                cl::Platform::get(&platforms);
                std::vector<cl::Device> devices;
                platforms[static_cast<size_t>(platformId)].getDevices(type, &devices);

                for(unsigned int i = 0; i < devices.size(); ++i)
                {
                    if (devices[i].getInfo<CL_DEVICE_NAME>() == deviceName)
                    {
                        std::vector<cl::Device> singleDevice;
                        singleDevice.push_back(devices[i]);
                        _contextCL = createCLContext(singleDevice);
                        _currentDevice = deviceName;
                        break;
                    }
                }
            }
            _useGL = false;
        }

        cl_command_queue_properties cqp = 0;
#ifdef CL_QUEUE_PROFILING_ENABLE
        cqp = CL_QUEUE_PROFILING_ENABLE;
#endif
        _queueCL = cl::CommandQueue(_contextCL, cqp);
    }
    catch (cl::Error err)
    {
        logCLerror(err);
    }

#ifdef _WIN32
    initKernel("kernels//volumeraycast.cl", "-DCL_STD=CL1.2 -DESS");
#else
    initKernel("kernels/volumeraycast.cl", "-DCL_STD=CL1.2 -DESS");
#endif // _WIN32

    // upload volume data to device if already loaded
    if (_dr.has_data())
    {
        // update all memory objects
        volDataToCLmem(_dr.data());
    }
}


/**
 * @brief VolumeRenderCL::initKernel
 * @param fileName
 * @param buildFlags
 */
void VolumeRenderCL::initKernel(const std::string fileName, const std::string buildFlags)
{
    try
    {
        cl::Program program = buildProgramFromSource(_contextCL, fileName, buildFlags);
        _raycastKernel = cl::Kernel(program, "volumeRender");
        cl_float16 view = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
        _raycastKernel.setArg(VIEW, view);
        _raycastKernel.setArg(SAMPLING_RATE, 1.5f);      // default step size 1.0*voxel size
        _raycastKernel.setArg(ORTHO, 0);                // perspective cam by default
        _raycastKernel.setArg(ILLUMINATION, 1);         // illumination on by default
        _raycastKernel.setArg(SHOW_ESS, 0);
        _raycastKernel.setArg(LINEAR, 1);
        cl_float4 bgColor = {{1.f, 1.f, 1.f, 1.f}};
        _raycastKernel.setArg(BACKGROUND, bgColor);
        _raycastKernel.setArg(AO, 0);                   // ambient occlusion off by default
        _raycastKernel.setArg(CONTOURS, 0);             // contour lines off by default
        _raycastKernel.setArg(AERIAL, 0);               // aerial perspective off by defualt
        _raycastKernel.setArg(IMG_ESS, 0);
        _raycastKernel.setArg(RNG_SEED, uint(_generator()));

        _genBricksKernel = cl::Kernel(program, "generateBricks");
        _downsamplingKernel = cl::Kernel(program, "downsampling");
    }
    catch (cl::Error err)
    {
        logCLerror(err);
    }
}


/**
 * @brief VolumeRenderCL::setMemObjects
 */
void VolumeRenderCL::setMemObjectsRaycast(const size_t t)
{
    _raycastKernel.setArg(VOLUME, _volumesMem.at(t));
    _raycastKernel.setArg(BRICKS, _bricksMem.at(t));
    _raycastKernel.setArg(TFF, _tffMem);
    if (_useGL)
        _raycastKernel.setArg(OUTPUT, _outputMem);
    else
        _raycastKernel.setArg(OUTPUT, _outputMemNoGL);
    _raycastKernel.setArg(TFF_PREFIX, _tffPrefixMem);
    cl_float3 modelScale = {{_modelScale[0], _modelScale[1], _modelScale[2]}};
    _raycastKernel.setArg(MODEL_SCALE, modelScale);

    _raycastKernel.setArg(IN_HIT_IMG, _inputHitMem);
    _raycastKernel.setArg(OUT_HIT_IMG, _outputHitMem);

    _raycastKernel.setArg(RNG_SEED, uint(_generator()));

    _raycastKernel.setArg(IN_ACCUMULATE, _inAccumulate);
    _raycastKernel.setArg(OUT_ACCUMULATE, _outAccumulate);
    _raycastKernel.setArg(ITERATION, _iteration);
}


/**
 * @brief VolumeRenderCL::setMemObjectsBrickGen
 */
void VolumeRenderCL::setMemObjectsBrickGen(const size_t t)
{
    if (_volumesMem.size() <= static_cast<size_t>(t) || _bricksMem.size() <= static_cast<size_t>(t))
        throw std::runtime_error("Error loading timeseries data: size mismatch.");
    _genBricksKernel.setArg(VOLUME, _volumesMem.at(t));
    _genBricksKernel.setArg(BRICKS, _bricksMem.at(t));
}


/**
 * @brief VolumeRenderCL::setMemObjectsDownsampling
 *
 * TODO: Add support for downsampling of whole timeseries.
 */
const std::string VolumeRenderCL::volumeDownsampling(const size_t t, const int factor)
{
    if (!_dr.has_data())
        throw std::runtime_error("No volume data is loaded.");
    if (factor < 2)
        throw std::invalid_argument("Factor must be greater or equal 2.");

    std::array<unsigned int, 3> texSize = {1u, 1u, 1u};
    texSize.at(0) = static_cast<unsigned int>(ceil(_dr.properties().volume_res.at(0) /
                                                         static_cast<double>(factor)));
    texSize.at(1) = static_cast<unsigned int>(ceil(_dr.properties().volume_res.at(1) /
                                                         static_cast<double>(factor)));
    texSize.at(2) = static_cast<unsigned int>(ceil(_dr.properties().volume_res.at(2) /
                                                         static_cast<double>(factor)));

    if (texSize.at(0) < 64)
    {
        std::cerr << "Error: Down sampled volume size would be smaller than 64. Aborting."
                  << std::endl;
        throw std::invalid_argument("Could not create down-sampled volume data set, because \
                                     the resolution would be smaller than the minimum (64x64x64).");
    }

    cl::ImageFormat format;
    format.image_channel_order = CL_R;
    unsigned int formatMultiplier = 1;

    if (_dr.properties().format == "UCHAR")
        format.image_channel_data_type = CL_UNORM_INT8;
    else if (_dr.properties().format == "USHORT")
    {
        format.image_channel_data_type = CL_UNORM_INT16;
        formatMultiplier = 2;
    }
    else if (_dr.properties().format == "FLOAT")
    {
        format.image_channel_data_type = CL_FLOAT;
        formatMultiplier = 4;
    }
    else
        throw std::invalid_argument("Unknown or invalid volume data format.");

    try
    {
        cl::Image3D lowResVol = cl::Image3D(_contextCL,
                                            CL_MEM_WRITE_ONLY,
                                            format,
                                            texSize.at(0), texSize.at(1), texSize.at(2),
                                            0, 0, nullptr);
        _downsamplingKernel.setArg(VOLUME, _volumesMem.at(t));
        _downsamplingKernel.setArg(1, lowResVol);

        cl::NDRange globalThreads(texSize.at(0), texSize.at(1), texSize.at(2));
        cl::Event ndrEvt;
        _queueCL.enqueueNDRangeKernel(_downsamplingKernel, cl::NullRange,
                                      globalThreads, cl::NullRange, nullptr, &ndrEvt);
        _queueCL.finish();    // global sync

        // read back volume data
        std::vector<unsigned char> outputData(texSize.at(0)*texSize.at(1)*texSize.at(2)
                                              *formatMultiplier);
        std::array<size_t, 3> origin = {{0, 0, 0}};
        std::array<size_t, 3> region = {{texSize.at(0), texSize.at(1), texSize.at(2)}};
        _queueCL.enqueueReadImage(lowResVol, CL_TRUE, origin, region, 0, 0, outputData.data());
        _queueCL.flush();    // global sync

        // dump to file
        size_t lastindex = _dr.properties().dat_file_name.find_last_of(".");
        std::string rawname = _dr.properties().dat_file_name.substr(0, lastindex);
        rawname += "_";
        rawname += std::to_string(texSize.at(0));
        std::ofstream file(rawname + ".raw", std::ios::out|std::ios::binary);
        std::cout << "Writing downsampled volume data to "
                  << rawname << "_" << std::to_string(texSize.at(0)) << ".raw ...";
        std::copy(outputData.cbegin(), outputData.cend(),
                  std::ostream_iterator<unsigned char>(file));
        file.close();

        // Generate .dat file and write out
        std::ofstream datFile(rawname + ".dat", std::ios::out);
        lastindex = rawname.find_last_of(".");
        size_t firstindex = rawname.find_last_of("/\\");
        std::string rawnameShort = rawname.substr(firstindex + 1, lastindex);
        datFile << "ObjectFileName: \t" << rawnameShort << ".raw\n";
        datFile << "Resolution: \t\t" << texSize.at(0) << " " << texSize.at(1) << " "
                                      << texSize.at(2) << "\n";
        datFile << "SliceThickness: \t" << _dr.properties().slice_thickness.at(0) << " "
                << _dr.properties().slice_thickness.at(1) << " "
                << _dr.properties().slice_thickness.at(2)
                << "\n";
        datFile << "Format: \t\t\t" << _dr.properties().format << "\n";
        datFile.close();
        std::cout << " Done." << std::endl;
        return rawname;
    }
    catch (cl::Error err)
    {
        logCLerror(err);
    }
}


/**
 * @brief VolumeRenderCL::calcScaling
 */
void VolumeRenderCL::calcScaling()
{
    if (!_dr.has_data())
        return;

    _modelScale = { static_cast<float>(_dr.properties().volume_res.at(0)),
                    static_cast<float>(_dr.properties().volume_res.at(1)),
                    static_cast<float>(_dr.properties().volume_res.at(2)) };

    std::valarray<float> thickness = { static_cast<float>(_dr.properties().slice_thickness.at(0)),
                                       static_cast<float>(_dr.properties().slice_thickness.at(1)),
                                       static_cast<float>(_dr.properties().slice_thickness.at(2)) };
    _modelScale *= thickness*(1.f/thickness[0]);
#undef max  // error here if we don't undef max
    _modelScale = _modelScale.max() / _modelScale;
}


/**
 * @brief VolumeRenderCL::scaleVolume
 * @param scale
 */
void VolumeRenderCL::scaleVolume(std::valarray<float> scale)
{
    _modelScale *= scale;
}


/**
 * @brief VolumeRenderCL::updateKernelArgs
 * @param viewMat
 */
void VolumeRenderCL::updateView(const std::array<float, 16> viewMat)
{
    if (!_dr.has_data() || _modelScale.size() < 3)
        return;

    cl_float16 view;
    for (size_t i = 0; i < 16; ++i)
        view.s[i] = viewMat[i];
    try{
        _raycastKernel.setArg(VIEW, view);
    } catch (cl::Error err) {
        logCLerror(err);
    }
    _iteration = 0;
}


/**
 * @brief VolumeRenderCL::updateStepSize
 * @param stepSize
 */
void VolumeRenderCL::updateSamplingRate(const double samplingRate)
{
    try{
        _raycastKernel.setArg(SAMPLING_RATE, static_cast<cl_float>(samplingRate));
    } catch (cl::Error err) {
        logCLerror(err);
    }
}


/**
 * @brief VolumeRenderCL::updateOutputImg
 * @param width
 * @param height
 */
void VolumeRenderCL::updateOutputImg(const size_t width, const size_t height, GLuint texId)
{
    cl::ImageFormat format;
    format.image_channel_order = CL_RGBA;
    format.image_channel_data_type = CL_FLOAT;
    try
    {
        if (_useGL)
        {
            _outputMem = cl::ImageGL(_contextCL, CL_MEM_WRITE_ONLY, GL_TEXTURE_2D, 0, texId);
        }
        else
        {
            _outputMemNoGL = cl::Image2D(_contextCL, CL_MEM_WRITE_ONLY, format, width, height);
            _raycastKernel.setArg(OUTPUT, _outputMemNoGL);
        }

        std::vector<unsigned int> initBuff((width/LOCAL_SIZE+ 1)*(height/LOCAL_SIZE+ 1), 1u);
        format = cl::ImageFormat(CL_R, CL_UNSIGNED_INT8);
        _outputHitMem = cl::Image2D(_contextCL, CL_MEM_READ_WRITE, format,
                                    width/LOCAL_SIZE + 1, height/LOCAL_SIZE + 1);
        _inputHitMem = cl::Image2D(_contextCL, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, format,
                                   width/LOCAL_SIZE + 1, height/LOCAL_SIZE + 1, 0,
                                   const_cast<unsigned int*>(initBuff.data()));

        _inAccumulate = cl::Image2D(_contextCL, CL_MEM_READ_WRITE, format, width, height);
        _outAccumulate = cl::Image2D(_contextCL, CL_MEM_READ_WRITE, format, width, height);
    }
    catch (cl::Error err)
    {
        logCLerror(err);
    }
}


/**
 * @brief VolumeRenderCL::runRaycast
 * @param imgSize
 */
void VolumeRenderCL::runRaycast(const size_t width, const size_t height, const size_t t)
{
    if (!this->_volLoaded)
        return;
    try // opencl scope
    {
        setMemObjectsRaycast(t);
        cl::NDRange globalThreads(width + (LOCAL_SIZE - width % LOCAL_SIZE), height
                                  + (LOCAL_SIZE - height % LOCAL_SIZE));
        cl::NDRange localThreads(LOCAL_SIZE, LOCAL_SIZE);
        cl::Event ndrEvt;

        std::vector<cl::Memory> memObj;
        memObj.push_back(_outputMem);
        _queueCL.enqueueAcquireGLObjects(&memObj);
        _queueCL.enqueueNDRangeKernel(
                    _raycastKernel, cl::NullRange, globalThreads, localThreads, nullptr, &ndrEvt);
        _queueCL.enqueueReleaseGLObjects(&memObj);
        _queueCL.finish();    // global sync

        if (_useImgESS)
        {
            // swap hit test buffers
            cl::Image2D tmp = _outputHitMem;
            _outputHitMem = _inputHitMem;
            _inputHitMem = tmp;
        }

        // swap accumulate buffers
        cl::Image2D tmp = _outAccumulate;
        _outAccumulate = _inAccumulate;
        _inAccumulate = tmp;
        _iteration++;

#ifdef CL_QUEUE_PROFILING_ENABLE
        cl_ulong start = 0;
        cl_ulong end = 0;
        ndrEvt.getProfilingInfo(CL_PROFILING_COMMAND_START, &start);
        ndrEvt.getProfilingInfo(CL_PROFILING_COMMAND_END, &end);
        _lastExecTime = static_cast<double>(end - start)*1e-9;
//        std::cout << "Kernel time: " << _lastExecTime << std::endl << std::endl;
#endif
    }
    catch (cl::Error err)
    {
        logCLerror(err);
    }
}


/**
 * @brief VolumeRenderCL::runRaycastNoGL
 * @param width
 * @param height
 * @param t
 * @param output
 */
void VolumeRenderCL::runRaycastNoGL(const size_t width, const size_t height, const size_t t,
                                    std::vector<float> &output)
{
    if (!this->_volLoaded)
        return;
    try // opencl scope
    {
        setMemObjectsRaycast(t);
        cl::NDRange globalThreads(width + (LOCAL_SIZE - width % LOCAL_SIZE),
                                  height + (LOCAL_SIZE - height % LOCAL_SIZE));
        cl::NDRange localThreads(LOCAL_SIZE, LOCAL_SIZE);
        cl::Event ndrEvt;

        _queueCL.enqueueNDRangeKernel(
                    _raycastKernel, cl::NullRange, globalThreads, localThreads, nullptr, &ndrEvt);
        output.resize(width*height*4);
        cl::Event readEvt;
        std::array<size_t, 3> origin = {{0, 0, 0}};
        std::array<size_t, 3> region = {{width, height, 1}};
        _queueCL.enqueueReadImage(_outputMemNoGL,
                                  CL_TRUE,
                                  origin, region, 0, 0,
                                  output.data(),
                                  nullptr, &readEvt);
        _queueCL.flush();    // global sync

#ifdef CL_QUEUE_PROFILING_ENABLE
        cl_ulong start = 0;
        cl_ulong end = 0;
        ndrEvt.getProfilingInfo(CL_PROFILING_COMMAND_START, &start);
        ndrEvt.getProfilingInfo(CL_PROFILING_COMMAND_END, &end);
        _lastExecTime = static_cast<double>(end - start)*1e-9;
//        std::cout << "Kernel time: " << _lastExecTime << std::endl << std::endl;
#endif
    }
    catch (cl::Error err)
    {
        logCLerror(err);
    }
}


/**
 * @brief VolumeRenderCL::generateBricks
 * @param volumeData
 */
void VolumeRenderCL::generateBricks()
{
    if (!_dr.has_data())
        return;
    try
    {
        // calculate brick size
        const unsigned int numBricks = 64u;
        std::array<unsigned int, 3> brickRes = {1u, 1u, 1u};
        brickRes.at(0) = std::max(1u, RoundPow2(_dr.properties().volume_res.at(0)/numBricks));
        brickRes.at(1) = std::max(1u, RoundPow2(_dr.properties().volume_res.at(1)/numBricks));
        brickRes.at(2) = std::max(1u, RoundPow2(_dr.properties().volume_res.at(2)/numBricks));
//std::cout << brickRes.at(2) << std::endl;
        std::array<unsigned int, 3> bricksTexSize = {1u, 1u, 1u};
        bricksTexSize.at(0) = static_cast<unsigned int>(ceil(_dr.properties().volume_res.at(0) /
                                                             static_cast<double>(brickRes.at(0))));
        bricksTexSize.at(1) = static_cast<unsigned int>(ceil(_dr.properties().volume_res.at(1) /
                                                             static_cast<double>(brickRes.at(1))));
        bricksTexSize.at(2) = static_cast<unsigned int>(ceil(_dr.properties().volume_res.at(2) /
                                                             static_cast<double>(brickRes.at(2))));
//std::cout << bricksTexSize.at(2) << std::endl;
        // set memory object
        cl::ImageFormat format;
        format.image_channel_order = CL_RG;  // NOTE: CL_RG for min+max

        if (_dr.properties().format == "UCHAR")
            format.image_channel_data_type = CL_UNORM_INT8;
        else if (_dr.properties().format == "USHORT")
            format.image_channel_data_type = CL_UNORM_INT16;
        else if (_dr.properties().format == "FLOAT")
            format.image_channel_data_type = CL_FLOAT;
        else
            throw std::invalid_argument("Unknown or invalid volume data format.");

        _bricksMem.clear();
        for (size_t i = 0; i < _dr.properties().raw_file_names.size(); ++i)
        {
            _bricksMem.push_back(cl::Image3D(_contextCL,
                                             CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS,
                                             format,
                                             bricksTexSize.at(0),
                                             bricksTexSize.at(1),
                                             bricksTexSize.at(2)));
            // run aggregation kernel
            setMemObjectsBrickGen(i);
            size_t lDim = 4;    // local work group dimension: 4*4*4=64
            cl::NDRange globalThreads(bricksTexSize.at(0) + (lDim - bricksTexSize.at(0) % lDim),
                                      bricksTexSize.at(1) + (lDim - bricksTexSize.at(1) % lDim),
                                      bricksTexSize.at(2) + (lDim - bricksTexSize.at(2) % lDim));
            cl::NDRange localThreads(lDim, lDim, lDim);
//            cl::Event ndrEvt;
            _queueCL.enqueueNDRangeKernel(_genBricksKernel, cl::NullRange,
                                          globalThreads, localThreads); //, NULL, &ndrEvt);
            _queueCL.finish();
//            cl_ulong start = 0;
//            cl_ulong end = 0;
//            ndrEvt.getProfilingInfo(CL_PROFILING_COMMAND_START, &start);
//            ndrEvt.getProfilingInfo(CL_PROFILING_COMMAND_END, &end);
//            double execTime = static_cast<double>(end - start)*1e-9;
//            std::cout << "Build up time: " << execTime << std::endl;
        }
    }
    catch (cl::Error err)
    {
        throw std::runtime_error( "ERROR: " + std::string(err.what()) + "("
                                  + getCLErrorString(err.err()) + ")");
    }
}

/**
 * @brief VolumeRenderCL::volDataToCLmem
 * @param volumeData
 */
void VolumeRenderCL::volDataToCLmem(const std::vector<std::vector<char>> &volumeData)
{
    if (!_dr.has_data())
        return;
    try
    {
        cl::ImageFormat format;
        auto co = _dr.properties().image_channel_order;
        if (co == "R" || co == "")
            format.image_channel_order = CL_R;
        else if (co == "RG")
            format.image_channel_order = CL_RG;
//        else if (co == "RGB")   // This format can only be used if channel data type = CL_UNORM_SHORT_565, CL_UNORM_SHORT_555 or CL_UNORM_INT101010.
//            format.image_channel_order = CL_RGB;
        else if (co == "RGBA")
            format.image_channel_order = CL_RGBA;
        else if (co == "ARGB")
            format.image_channel_order = CL_ARGB;
        else if (co == "BGRA")
            format.image_channel_order = CL_BGRA;

        unsigned int formatMultiplier = sizeof(cl_uchar);
        if (_dr.properties().format == "UCHAR")
            format.image_channel_data_type = CL_UNORM_INT8;
        else if (_dr.properties().format == "USHORT")
        {
            format.image_channel_data_type = CL_UNORM_INT16;
            formatMultiplier = sizeof(cl_ushort);
        }
        else if (_dr.properties().format == "FLOAT")
        {
            format.image_channel_data_type = CL_FLOAT;
            formatMultiplier = sizeof(cl_float);
        }
        else
            throw std::invalid_argument("Unknown or invalid volume data format.");

        _volumesMem.clear();
        for (const auto &v : volumeData)
        {
            if(_dr.properties().volume_res[0] * _dr.properties().volume_res[1] *
                     _dr.properties().volume_res[2] * formatMultiplier > v.size())
            {
                _dr.clearData();
                throw std::runtime_error("Volume size does not match size specified in dat file.");
            }
            _volumesMem.push_back(cl::Image3D(_contextCL,
                                              CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                              format,
                                              _dr.properties().volume_res[0],
                                              _dr.properties().volume_res[1],
                                              _dr.properties().volume_res[2],
                                              0, 0,
                                              const_cast<char*>(v.data())));
        }
    }
    catch (cl::Error err)
    {
        throw std::runtime_error( "ERROR: " + std::string(err.what()) + "("
                                  + getCLErrorString(err.err()) + ")");
    }
}

/**
 * @brief VolumeRenderCL::loadVolumeData
 * @param fileName
 */
size_t VolumeRenderCL::loadVolumeData(const std::string &fileName)
{
    this->_volLoaded = false;
    std::cout << "Loading volume data defined in " << fileName << std::endl;
    try
    {
        _dr.read_files(fileName);
        std::cout << _dr.data().front().size()*_dr.data().size() << " bytes have been read from "
                  << _dr.data().size() << " file(s)." << std::endl;
        std::cout << _dr.properties().to_string() << std::endl;
        volDataToCLmem(_dr.data());
        calcScaling();
    }
    catch (std::invalid_argument e)
    {
        throw std::runtime_error(e.what());
    }

    // initally, set a simple linear transfer function
    std::vector<unsigned char> tff(256*4, 0);
    std::iota(tff.begin() + 3, tff.end(), 0);
    // TODO: testing are there corner cases where this is necessary?
//    setTransferFunction(tff);

    std::vector<unsigned int> prefixSum(256, 0);
#pragma omp for
    for (int i = 0; i < static_cast<int>(prefixSum.size()); ++i)
        prefixSum.at(static_cast<unsigned int>(i)) = static_cast<unsigned int>(i)*4u;

    std::partial_sum(prefixSum.begin(), prefixSum.end(), prefixSum.begin());
    setTffPrefixSum(prefixSum);

    this->_volLoaded = true;
    return _dr.data().size();
}


/**
 * @brief VolumeRenderCL::hasData
 * @return
 */
bool VolumeRenderCL::hasData() const
{
    return this->_volLoaded;
}


/**
 * @brief VolumeRenderCL::getResolution
 * @return
 */
const std::array<unsigned int, 4> VolumeRenderCL::getResolution() const
{
    if (!_dr.has_data())
        return std::array<unsigned int, 4> {{0, 0, 0, 1}};
    return _dr.properties().volume_res;
}

const std::array<double, 256> & VolumeRenderCL::getHistogram(unsigned int timestep)
{
    if (!_dr.has_data())
        throw std::invalid_argument("Invalid timestep for histogram data.");
    return _dr.getHistogram(timestep);
}



/**
 * @brief VolumeRenderCL::setTransferFunction
 * @param tff
 */
void VolumeRenderCL::setTransferFunction(std::vector<unsigned char> &tff)
{
    if (!_dr.has_data())
        return;
    try
    {
        cl::ImageFormat format;
        format.image_channel_order = CL_RGBA;
        format.image_channel_data_type = CL_UNORM_INT8;

        cl_mem_flags flags = CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR;
        // divide size by 4 because of RGBA channels
        _tffMem = cl::Image1D(_contextCL, flags, format, tff.size() / 4, tff.data());
        generateBricks();

        std::vector<unsigned int> prefixSum;
        // copy only alpha values (every fourth element)
        for (size_t i = 3; i < tff.size(); i += 4)
            prefixSum.push_back(static_cast<unsigned int>(tff.at(i)));
        std::partial_sum(prefixSum.begin(), prefixSum.end(), prefixSum.begin());
        setTffPrefixSum(prefixSum);
    }
    catch (cl::Error err)
    {
        logCLerror(err);
    }
}


/**
 * @brief VolumeRenderCL::setTffPrefixSum
 * @param tffPrefixSum
 */
void VolumeRenderCL::setTffPrefixSum(std::vector<unsigned int> &tffPrefixSum)
{
    if (!_dr.has_data())
        return;
    try
    {
        cl::ImageFormat format;
        format.image_channel_order = CL_R;
        format.image_channel_data_type = CL_UNSIGNED_INT32;

        cl_mem_flags flags = CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR;
        _tffPrefixMem = cl::Image1D(_contextCL, flags, format, tffPrefixSum.size(),
                                    tffPrefixSum.data());
    }
    catch (cl::Error err)
    {
        logCLerror(err);
    }
}

/**
 * @brief VolumeRenderCL::setCamOrtho
 * @param setCamOrtho
 */
void VolumeRenderCL::setCamOrtho(bool setCamOrtho)
{
    if (!this->hasData())
        return;

    try {
        _raycastKernel.setArg(ORTHO, static_cast<cl_uint>(setCamOrtho));
    } catch (cl::Error err) { logCLerror(err); }
}

/**
 * @brief VolumeRenderCL::setIllumination
 * @param illum
 */
void VolumeRenderCL::setIllumination(unsigned int illum)
{
    try {
        _raycastKernel.setArg(ILLUMINATION, static_cast<cl_uint>(illum));
    } catch (cl::Error err) { logCLerror(err); }
}


/**
 * @brief VolumeRenderCL::setAmbientOcclusion
 * @param illum
 */
void VolumeRenderCL::setAmbientOcclusion(bool ao)
{
    try {
        _raycastKernel.setArg(AO, static_cast<cl_uint>(ao));
    } catch (cl::Error err) { logCLerror(err); }
}


/**
 * @brief VolumeRenderCL::setShowESS
 * @param showESS
 */
void VolumeRenderCL::setShowESS(bool showESS)
{
    try {
        _raycastKernel.setArg(SHOW_ESS, static_cast<cl_uint>(showESS));
    } catch (cl::Error err) { logCLerror(err); }
}


/**
 * @brief VolumeRenderCL::setLinearSampling
 * @param linearSampling
 */
void VolumeRenderCL::setLinearInterpolation(bool linearSampling)
{
    try {
        _raycastKernel.setArg(LINEAR, static_cast<cl_uint>(linearSampling));
    } catch (cl::Error err) { logCLerror(err); }
}

/**
 * @brief VolumeRenderCL::setContours
 * @param contours
 */
void VolumeRenderCL::setContours(bool contours)
{
    try {
        _raycastKernel.setArg(CONTOURS, static_cast<cl_uint>(contours));
    } catch (cl::Error err) { logCLerror(err); }
}

/**
 * @brief VolumeRenderCL::setAerial
 * @param aerial
 */
void VolumeRenderCL::setAerial(bool aerial)
{
    try {
        _raycastKernel.setArg(AERIAL, static_cast<cl_uint>(aerial));
    } catch (cl::Error err) { logCLerror(err); }
}

/**
 * @brief VolumeRenderCL::setImgEss
 * @param useEss
 */
void VolumeRenderCL::setImgEss(bool useEss)
{
    try {
        _raycastKernel.setArg(IMG_ESS,  static_cast<cl_uint>(useEss));
        _useImgESS = useEss;
    } catch (cl::Error err) { logCLerror(err); }
}

/**
 * @brief VolumeRenderCL::setImgEss
 * @param useEss
 */
void VolumeRenderCL::setObjEss(bool useEss)
{
    std::string ess = useEss ? "-DESS" : "";
#ifdef _WIN32
    initKernel("kernels//volumeraycast.cl", "-DCL_STD=CL1.2 " + ess);
#else
    initKernel("kernels/volumeraycast.cl", "-DCL_STD=CL1.2 " + ess);
#endif // _WIN32
    // upload volume data if already loaded
    if (_dr.has_data())
    {
        volDataToCLmem(_dr.data());
    }
}

/**
 * @brief VolumeRenderCL::setBackground
 * @param color
 */
void VolumeRenderCL::setBackground(std::array<float, 4> color)
{
    cl_float3 bgColor = {{color[0], color[1], color[2], color[3]}};
    try {
        _raycastKernel.setArg(BACKGROUND, bgColor);
    } catch (cl::Error err) { logCLerror(err); }
}


/**
 * @brief VolumeRenderCL::getLastExecTime
 * @return
 */
double VolumeRenderCL::getLastExecTime()
{
    return _lastExecTime;
}


/**
 * @brief VolumeRenderCL::getPlatformNames
 * @return
 */
const std::vector<std::string> VolumeRenderCL::getPlatformNames()
{
    std::vector<std::string> names;
    try
    {
        std::vector<cl::Platform> platforms;

        cl::Platform::get(&platforms);
        for(unsigned int i = 0; i < platforms.size(); ++i)
            names.push_back(platforms[i].getInfo<CL_PLATFORM_NAME>());
    }
    catch (cl::Error err) {
        logCLerror(err);
    }
    return names;
}

/**
 * @brief VolumeRenderCL::getDeviceNames
 * @param platformId
 * @param type
 * @return
 */
const std::vector<std::string> VolumeRenderCL::getDeviceNames(size_t platformId,
                                                              const std::string &type)
{
    std::vector<std::string> names;
    cl_device_type t = CL_DEVICE_TYPE_ALL;
    if (type == "GPU") t = CL_DEVICE_TYPE_GPU;
    else if (type == "CPU") t = CL_DEVICE_TYPE_CPU;
    try
    {
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);
        std::vector<cl::Device> devices;
        platforms[platformId].getDevices(t, &devices);

        for(unsigned int i = 0; i < devices.size(); ++i)
            names.push_back(devices[i].getInfo<CL_DEVICE_NAME>());
    }
    catch (cl::Error err) {
        logCLerror(err);
    }

    return names;
}

/**
 * @brief VolumeRenderCL::getCurrentDeviceName
 * @return
 */
const std::string VolumeRenderCL::getCurrentDeviceName()
{
    return _currentDevice;
}
