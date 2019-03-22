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
#pragma once

#define CL_QUEUE_PROFILING_ENABLE
#define CL_HPP_ENABLE_EXCEPTIONS
#include "src/oclutil/openclglutilities.h"
#include "src/oclutil/openclutilities.h"

#include "src/io/datrawreader.h"

#include <valarray>
#include <random>

/**
 * @brief The volume renderer class based on OpenCL.
 */
class VolumeRenderCL
{
public:
    /**
     * @brief The OpenCL kernel argument enum.
     */
    enum kernel_arg
    {
          VOLUME     = 0 // volume data set                         image3d_t
        , BRICKS     = 1 // low resolution brick volume             image3d_t
        , TFF        = 2 // transfer function array                 image1d_t
        , OUTPUT         // output image                            image2d_t
        , SAMPLING_RATE  // step size factor                        cl_float
        , VIEW           // view matrix                             float16
        , ORTHO          // use orthographic camera                 cl_uint (bool)
        , ILLUMINATION   // use illumination                        cl_uint
        , SHOW_ESS       // visualize empty space skipping          cl_uint (bool)
        , LINEAR         // use linear interpolation, not nearest   cl_uint (bool)
        , BACKGROUND     // background color RGBA                   cl_float4
        , TFF_PREFIX     // prefix sum of transfer function         image1d_t
        , AO             // use ambient occlusion                   cl_uint (bool)
        , MODEL_SCALE    // model scaling factor                    cl_float3
        , CONTOURS       // show contour lines                      cl_uint (bool)
        , AERIAL         // use aerial perspective                  cl_uint (bool)
        , IN_HIT_IMG     // input image for image order ESS         image2d_t (UINT)
        , OUT_HIT_IMG    // output image for image order ESS        image2d_t (UINT)
        , IMG_ESS        // image order empty space skipping        cl_uint (bool)
        , RNG_SEED
        , IN_ACCUMULATE
        , OUT_ACCUMULATE
        , ITERATION
    };

    // mipmap down-scaling metric
    enum scaling_metric
    {
        MIN = 0,
        MAX,
        AVG,
        DENSITY,
    };

    /**
     * @brief Ctor
     */
    VolumeRenderCL();
    /**
     * @brief Dtor
     */
    ~VolumeRenderCL();

    /**
     * @brief Initialize the volume raycaster, i.e. the OpenCL context, queue and kernel.
     * @param useGL Use OpenGL context sharing.
     * @param useCPU Use CPU device instead of GPU device.
     * @param vendor OpenCL platform vendor.
     * @param deviceName Name of the OpenCL device to use.
     */
    void initialize(bool useGL = false, bool useCPU = false, cl_vendor vendor = VENDOR_ANY,
                    const std::string deviceName = "", const int platformId = -1);

    /**
     * @brief Update the view matrix argument for the raycasting kernel.
     * @param viewMat the 4x4 transposed view matrix.
     */
    void updateView(const std::array<float, 16> viewMat);

    /**
     * @brief Update the integration step size factor kernel argument for the volume raycast.
     * @param samplingRate the sampling rate relative per voxel.
     */
    void updateSamplingRate(const double samplingRate);

    /**
     * @brief Update the output image kernel argument and vector size.
     * @param width The image width in pixels.
     * @param height The image height in pixels.
     */
    void updateOutputImg(const size_t width, const size_t height, cl_GLuint texId);

    /**
     * @brief Run the actual OpenCL volume raycasting kernel.
     * @param width The image width in pixels, used as one dimension of the global thread size.
     * @param height The image height in pixels, used as one dimension of the global thread size.
     * @param t time series id, defaults to 0 if no time series
     */
     void runRaycast(const size_t width, const size_t height, const size_t t = 0);

     /**
      * @brief Run the actual OpenCL volume raycasting kernel without OpenGL context shring.
      * @param width The image width in pixels, used as one dimension of the global thread size.
      * @param height The image height in pixels, used as one dimension of the global thread size.
      * @param t time series id, defaults to 0 if no time series
      * @param pixel color output data of the frame
      */
     void runRaycastNoGL(const size_t width, const size_t height, const size_t t,
                         std::vector<float> &output);

    /**
     * @brief Load volume data from a given .dat file name.
     * @param fileName The full path to the volume data file.
     * @return number of loaded volume time steps
     */
    size_t loadVolumeData(const std::string &fileName);

    /**
     * @brief Answers if volume data has been loaded.
     * @return true, if volume data has been loaded, false otherwise.
     */
    bool hasData() const;

    /**
     * @brief Return spacial and temporal resolution of loaded volume data set.
     * @return 4D array containing the resolution in x,y,z direction and number of time steps.
     */
    const std::array<unsigned int, 4> getResolution() const;

    /**
     * @brief Set the transfer function for the volume raycast as a kernel argument.
     * @param tff a vector of the RGBA transfer function values.
     * @param rangeMin clamp range to minimum
     * @param rangeMax clamp range to maximum
     */
    void setTransferFunction(std::vector<unsigned char> &tff);

    /**
     * @brief Set the prefix sum of the transfer function.
     * @param tffPrefixSum The prefix sum as vector of unsigned ints.
     */
    void setTffPrefixSum(std::vector<unsigned int> &tffPrefixSum);

    /**
     * @brief VolumeRenderCL::scaleVolume
     * @param scale
     */
    void scaleVolume(std::valarray<float> scale);

    /**
     * @brief buildScaledVol
     * @param factor
     * @param metric
     * @param useTff
     * @return
     */
    cl::Image3D buildScaledVol(std::array<unsigned int, 3> factor,
                               scaling_metric metric, bool useTff, cl::Image3D volData);

    /**
     * @brief Set ortographic camera kernel parameter.
     * @param setCamOrtho
     */
    void setCamOrtho(bool setCamOrtho);
    /**
     * @brief Set show illumination kernel paramter.
     * @param illum
     */
    void setIllumination(unsigned int illum);
    /**
     * @brief Set show empty space skipping kernel parameter.
     * @param boundingBox
     */
    void setShowESS(bool showESS);
    /**
     * @brief Set linear sampling kernel parameter.
     * @param linearSampling
     */
    void setLinearInterpolation(bool linearSampling);
    /**
     * @brief Set show contours kernel paramter.
     * @param contours
     */
    void setContours(bool contours);
    /**
     * @brief Set aerial perspective kernel parameter.
     * @param aerial
     */
    void setAerial(bool aerial);
    /**
     * @brief Set image order empty space skipping kernel parameter.
     * @param useEss
     */
    void setImgEss(bool useEss);
    /**
     * @brief Set object order empty space skipping kernel parameter.
     * @param useEss
     */
    void setObjEss(bool useEss);
    /**
     * @brief Set background color kernel parameter.
     * @param color
     */
    void setBackground(std::array<float, 4> color);

    /**
     * @brief Get the execution time of the last kernel run.
     * @return The kernel runtime in seconds.
     */
    double getLastExecTime();

    /**
     * @brief getPlatformNames
     * @return platform names
     */
    const std::vector<std::string> getPlatformNames();

    /**
     * @brief getDeviceNames
     * @param platformId OpenCL platform id
     * @param type string describing the OpenCL device type ('GPU' or 'CPU')
     * @return device names
     */
    const std::vector<std::string> getDeviceNames(size_t platformId, const std::string &type);

    /**
     * @brief Get the OpenCL device that is currently in use.
     * @return The name of the current OpenCL device in use.
     *         Returns an empty string if no device is currently used.
     */
    const std::string getCurrentDeviceName();

    /**
     * @brief setAmbientOcclusion
     * @param ao
     */
    void setAmbientOcclusion(bool ao);

    /**
     * @brief Generate a downsampling of the currently loaded volume file.
     * @param t Timestep to be downsampled
     * @param factor downsampling factor, uniform for all 3 dimensions
     */
    const std::string volumeDownsampling(const size_t t, const int factor);

    /**
     * @brief getHistogram
     * @param timestep
     * @return
     */
    const std::array<double, 256> & getHistogram(unsigned int timestep = 0);

private:
    /**
     * @brief Generate coarse grained volume bricks that can be used for ESS.
     * @param volumeData
     */
    void generateBricks();

    /**
     * @brief Calculate the scaling vector for the volume data.
     */
    void calcScaling();

    /**
     * @brief volDataToCLmem
     * @param volumeData
     */
    void volDataToCLmem(const std::vector<std::vector<char> > &volumeData);

    /**
     * @brief Convert volume data to UCHAR format and generate OpenCL image textue memory object.
     * @param volumeData The raw volume data.
     */
    template<class T>
    void volDataToCLmem(const std::vector<char> &volumeData)
    {
        // reinterpret raw data (char) to input format
        auto s = reinterpret_cast<const T *>(volumeData.data());
        auto e = reinterpret_cast<const T *>(volumeData.data() + volumeData.size());
        // convert imput vector to the desired output precision
        std::vector<unsigned char> convertedData(s, e);
        try
        {
            cl::ImageFormat format;
            format.image_channel_order = CL_R;
            format.image_channel_data_type = CL_UNORM_INT8;

            _volumesMem.push_back(cl::Image3D(_contextCL,
                                              CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                              format,
                                              _dr.properties().volume_res[0],
                                              _dr.properties().volume_res[1],
                                              _dr.properties().volume_res[2],
                                              0, 0,
                                              const_cast<unsigned char*>(convertedData.data())));
        }
        catch (cl::Error err)
        {
            throw std::runtime_error( "ERROR: " + std::string(err.what()) + "("
                                      + getCLErrorString(err.err()) + ")");
        }
    }

    /**
     * @brief Set OpenCL memory objects for volume raycast kernel.
     * @param t number of volume timesteps.
     */
    void setMemObjectsRaycast(const size_t t);

    /**
     * @brief Set OpenCL memory objects for brick generation kernel.
     * @param t number of volume timesteps.
     * @throws runtime_error when the number of volume timesteps
     *         is not equal to number of brick timesteps
     */
    void setMemObjectsBrickGen(const size_t t);

    /**
     * @brief Initialize OpenCL kernel with default paramters.
     * @param fileName File name of the kernel source file.
     * @param buildFlags Comiler flags for building the kernel.
     */
    void initKernel(const std::string fileName, const std::string buildFlags = "");

    /**
     * @brief Log a OpenCL error message.
     * @param err The OpenCL error object to be logged.
     * @throws Runtime error
     **/
    [[ noreturn ]] void logCLerror(cl::Error err);

    // *** member variables ***
    cl::Context _contextCL;
    cl::CommandQueue _queueCL;
    cl::Kernel _raycastKernel;
    cl::Kernel _genBricksKernel;
    cl::Kernel _downsamplingKernel;

    std::vector<cl::Image3D> _volumesMem;
    std::vector<cl::Image3D> _bricksMem;
    cl::ImageGL _outputMem;
    cl::ImageGL _overlayMem;
    cl::Image1D _tffMem;
    cl::Image1D _tffPrefixMem;
    cl::Image2D _outputMemNoGL;
    cl::Image2D _outputHitMem;
    cl::Image2D _inputHitMem;
    cl::Image2D _inAccumulate;
    cl::Image2D _outAccumulate;

    bool _volLoaded;
    double _lastExecTime;
    std::valarray<float> _modelScale;
    bool _useGL;
    bool _useImgESS;
    std::string _currentDevice;
    uint _iteration = 0;
    // rng
    std::mt19937 _generator;

    DatRawReader _dr;
};
