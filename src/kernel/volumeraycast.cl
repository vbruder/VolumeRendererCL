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

#pragma OPENCL EXTENSION cl_khr_3d_image_writes : enable

#include "kernels/random.cl"


#define ERT_THRESHOLD 0.98

constant sampler_t linearSmp = CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_CLAMP_TO_EDGE |
                               CLK_FILTER_LINEAR;
constant sampler_t nearestSmp = CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_CLAMP |
                                CLK_FILTER_NEAREST;
constant sampler_t nearestIntSmp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP |
                                   CLK_FILTER_NEAREST;

// init random number generator (hybrid ui_randworthy)
uint4 initRNG(uint seed)
{
    uint4 ui_rand;
    ui_rand.x = 128U + (uint)(get_global_size(0));
    ui_rand.y = 128U + (uint)(get_global_size(1));
    ui_rand.z = 128U + (uint)(seed);
    ui_rand.w = 128U + (uint)(get_global_size(0) * get_global_size(1) * seed);
    return ui_rand;
}

// S1, S2, S3, and M are all constants, and z is part of the
// private per-thread generator state.
uint ui_randStep(uint4 *ui_rand, uint p, int s1, int s2, int s3, uint m)
{
    uint4 loc = *ui_rand;
    uint locZ;
    if (p == 0) locZ = loc.x;
    if (p == 1) locZ = loc.y;
    if (p == 2) locZ = loc.z;
    uint b = (((locZ << (uint)(s1)) ^ locZ) >> (uint)(s2));
    locZ = (((locZ & m) << (uint)(s3)) ^ b);
    if (p == 0) *ui_rand = (uint4)(locZ, loc.yzw);
    if (p == 1) *ui_rand = (uint4)(loc.x, locZ, loc.zw);
    if (p == 2) *ui_rand = (uint4)(loc.xy, locZ, loc.w);
    return locZ;
}

// A and C are constants
uint lcgStep(uint4 *ui_rand, uint a, uint c)
{
    uint4 loc = *ui_rand;
    *ui_rand = (uint4)(loc.xyz, a*loc.w + c);
    return loc.w;
}

float hybridui_rand(uint4 *ui_rand)
{
    // Combined period is lcm(p1,p2,p3,p4)~ 2^121
    return 2.3283064365387e-10 * (float)(ui_randStep(ui_rand, 0, 13, 19, 12, 4294967294U) ^  // p1=2^31-1
                                         ui_randStep(ui_rand, 1, 2, 25, 4, 4294967288U)   ^  // p2=2^30-1
                                         ui_randStep(ui_rand, 2, 3, 11, 17, 4294967280U)  ^  // p3=2^28-1
                                         lcgStep(ui_rand, 1664525U, 1013904223U));        // p4=2^32
}

// comparison of floats
static bool approxEq(const float a, const float b)
{
    return fabs(a - b) < FLT_EPSILON;
}
static uint3 approxEq3(const float3 a, const float3 b)
{
    return (uint3)(approxEq(a.x, b.x), approxEq(a.y, b.y), approxEq(a.z, b.z));
}

// check if inside unit cube
bool in_volume(const float3 pos)
{
    return max(fabs(pos.x), max(fabs(pos.y), fabs(pos.z))) < 1.f;
}

// intersect ray with the 'unit-box': (-1,-1,-1) to (1,1,1)
// http://www.siggraph.org/education/materials/HyperGraph/raytrace/rtinter3.htm
int intersectBox(float3 rayOrig, float3 rayDir, float *tnear, float *tfar)
{
    // compute intersection of ray with all six bbox planes
    float3 invRay = native_divide((float3)(1.0f), rayDir);
    float3 tBot = invRay * ((float3)(-1.0f) - rayOrig);
    float3 tTop = invRay * ((float3)(1.0f) - rayOrig);

    // re-order intersections to find smallest and largest on each axis
    float3 tMin = min(tTop, tBot);
    float3 tMax = max(tTop, tBot);

    // find the largest tMin and the smallest tMax
    float maxTmin = max(max(tMin.x, tMin.y), max(tMin.x, tMin.z));
    float minTmax = min(min(tMax.x, tMax.y), min(tMax.x, tMax.z));

    *tnear = maxTmin;
    *tfar = minTmax;

    return (int)(minTmax > maxTmin);
}

// intersect a ray with a defined box
int intersectBBox(float3 rayOrig, float3 rayDir, float3 lower, float3 upper,
                  float *tnear, float *tfar)
{
    // compute intersection of ray with all six bbox planes
    float3 invRay = native_divide((float3)(1.0f), rayDir);
    float3 tBot = invRay * (lower - rayOrig);
    float3 tTop = invRay * (upper - rayOrig);

    // re-order intersections to find smallest and largest on each axis
    float3 tMin = min(tTop, tBot);
    float3 tMax = max(tTop, tBot);

    // find the largest tMin and the smallest tMax
    float maxTmin = max(max(tMin.x, tMin.y), max(tMin.x, tMin.z));
    float minTmax = min(min(tMax.x, tMax.y), min(tMax.x, tMax.z));

    *tnear = maxTmin;
    *tfar = minTmax;

    return (int)(minTmax > maxTmin);
}

// ray-plane intersection
int intersectPlane(const float3 rayOrigin, const float3 rayDir,
                   const float3 planeNormal, const float3 planePos, float *t)
{
    float denom = dot(rayDir, planeNormal);
    if (denom > 1e-6)   // check if parallel
    {
        float3 origin2point = planePos - rayOrigin;
        *t = dot(origin2point, planeNormal) / denom;
        return (t >= 0);
    }
    return false;
}

// Compute gradient using central difference: f' = ( f(x+h)-f(x-h) )
float4 gradientCentralDiff(read_only image3d_t vol, const float4 pos)
{
    float3 volResf = convert_float3(get_image_dim(vol).xyz);
    float3 offset = native_divide((float3)(1.0f), volResf);
    float3 s1;
    float3 s2;
    s1.x = read_imagef(vol, linearSmp, pos + (float4)(-offset.x, 0, 0, 0)).x;
    s1.y = read_imagef(vol, linearSmp, pos + (float4)(0, -offset.y, 0, 0)).x;
    s1.z = read_imagef(vol, linearSmp, pos + (float4)(0, 0, -offset.z, 0)).x;

    s2.x = read_imagef(vol, linearSmp, pos + (float4)(+offset.x, 0, 0, 0)).x;
    s2.y = read_imagef(vol, linearSmp, pos + (float4)(0, +offset.y, 0, 0)).x;
    s2.z = read_imagef(vol, linearSmp, pos + (float4)(0, 0, +offset.z, 0)).x;

    float3 normal = fast_normalize(s2 - s1).xyz;
    if (length(normal) == 0.0f) // TODO: zero correct
        normal = (float3)(0.57735f);

    return (float4)(normal, fast_length(s2 - s1));
}

// Compute gradient using central difference: f' = ( f(x+h)-f(x-h) ) and the transfer funciton
float4 gradientCentralDiffTff(read_only image3d_t vol, const float4 pos, read_only image1d_t tff)
{
    float3 volResf = convert_float3(get_image_dim(vol).xyz);
    float3 offset = native_divide((float3)(1.0f), volResf);
    float3 s1;
    float3 s2;
    s1.x = read_imagef(tff, linearSmp,
                       read_imagef(vol, linearSmp, pos + (float4)(-offset.x, 0, 0, 0)).x).w;
    s1.y = read_imagef(tff, linearSmp,
                       read_imagef(vol, linearSmp, pos + (float4)(0, -offset.y, 0, 0)).x).w;
    s1.z = read_imagef(tff, linearSmp,
                       read_imagef(vol, linearSmp, pos + (float4)(0, 0, -offset.z, 0)).x).w;

    s2.x = read_imagef(tff, linearSmp,
                       read_imagef(vol, linearSmp, pos + (float4)(+offset.x, 0, 0, 0)).x).w;
    s2.y = read_imagef(tff, linearSmp,
                       read_imagef(vol, linearSmp, pos + (float4)(0, +offset.y, 0, 0)).x).w;
    s2.z = read_imagef(tff, linearSmp,
                       read_imagef(vol, linearSmp, pos + (float4)(0, 0, +offset.z, 0)).x).w;

    float3 normal = fast_normalize(s2 - s1).xyz;
    if (length(normal) == 0.0f) // TODO: zero correct
        normal = (float3)(0.57735f);

    return (float4)(normal, fast_length(s2 - s1));
}

float getf4(float4 v, int id)
{
    if (id == 0) return v.x;
    if (id == 1) return v.y;
    if (id == 2) return v.z;
    if (id == 3) return v.w;
}

// Compute gradient using a sobel filter (1,2,4)
float4 gradientSobel(read_only image3d_t vol, const float4 pos)
{
    float sobelWeights[3][3][3][3] = {
            {{{-1, -2, -1},
              {-2, -4, -2},
              {-1, -2, -1}},
             {{ 0,  0,  0},
              { 0,  0,  0},
              { 0,  0,  0}},
             {{ 1,  2,  1},
              { 2,  4,  2},
              { 1,  2,  1}}},
            {{{-1, -2, -1},
              { 0,  0,  0},
              { 1,  2,  1}},
             {{-2, -4, -2},
              { 0,  0,  0},
              { 2,  4,  2}},
             {{-1, -2, -1},
              { 0,  0,  0},
              { 1,  2,  1}}},
            {{{-1,  0,  1},
              {-2,  0,  2},
              {-1,  0,  1}},
             {{-2,  0,  2},
              {-4,  0,  4},
              {-2,  0,  2}},
             {{-1,  0,  1},
              {-2,  0,  2},
              {-1,  0,  1}}}
    };
    float3 volResf = convert_float3(get_image_dim(vol).xyz);
    float3 offset = native_divide((float3)(1.0f), volResf);

    float4 gradient = (float4)(0.f);
    for (int dir = 0; dir < 3; dir++)   // partial derivatives in xyz directions
    {
        for (int i = -1; i < 2; i++)
        {
            for (int j = -1; j < 2; j++)
            {
                for (int k = -1; k < 2; k++)
                {
                    float4 samplePos = pos + (float4)(offset*(float3)(i,j,k), 0);
                    float weight = sobelWeights[dir][i + 1][j + 1][k + 1]
                                        * read_imagef(vol, linearSmp, samplePos).x;
                    if (dir == 0) gradient.x += weight;
                    else if (dir == 1) gradient.y += weight;
                    else if (dir == 2) gradient.z += weight;
                }
            }
        }
    }
    gradient.xyz /= 27.f;
    gradient.w = fast_length(gradient.xyz);
    if (gradient.w == 0)    // FIXME: normal in length 0 case?
        gradient.xyz = (float3)(1.f);
    gradient.xyz = fast_normalize(gradient.xyz);

    return gradient;
}

// specular part of blinn-phong shading model
float3 specularBlinnPhong(float3 lightColor, float specularExp, float3 materialColor,
                          float3 normal, float3 toLightDir, float3 toCameraDir)
{
    float3 h = toCameraDir + toLightDir;
    // check for special case where the light source is exactly opposite
    // to the view direction, i.e. the length of the halfway vector is zero
    if (dot(h, h) < 1.e-6f) // check for squared length
        return (float3)(0.0f);

    h = normalize(h);
    return materialColor * lightColor * native_powr(max(dot(normal, h), 0.f), specularExp);
}

// blinn phong illumination based gradients evaluating density values
float3 illumination(const float4 pos, float3 color, float3 toLightDir, float3 n)
{
    float3 l = fast_normalize(toLightDir.xyz);

    float3 amb = color * 0.15f;
    float3 diff = color * max(0.f, dot(n, l)) * 0.7f;
    float3 spec = specularBlinnPhong((float3)(1.f), 40.f, (float3)(1.f), n, l, toLightDir) * 0.15f;

    return (amb + diff + spec);
}

// cel shading (aka toon shading)
float3 celShading(float3 color, float3 toLightDir, float3 n)
{
    float3 celColor = color*0.2f;
    float3 l = fast_normalize(toLightDir.xyz);
    float intensity = max(0.f, dot(n, l));

    if (intensity > 0.95f)
        celColor = color;
    else if (intensity > 0.5f)
        celColor = color*0.6f;
    else if (intensity > 0.25f)
        celColor = color*0.4f;
    return celColor;
}


// check for border edges
bool checkBoundingBox(float3 pos, float3 voxLen, float2 bound)
{
    if (
        (pos.x < voxLen.x     && pos.z < bound.x + voxLen.z) ||
        (pos.x < voxLen.x     && pos.y < voxLen.y) ||
        (pos.y < voxLen.y     && pos.z < bound.x + voxLen.z) ||
        (pos.x > 1.f-voxLen.x && pos.z < bound.x + voxLen.z) ||
        (pos.y > 1.f-voxLen.y && pos.z < bound.x + voxLen.z) ||
        (pos.x > 1.f-voxLen.x && pos.z > bound.y - voxLen.z) ||
        (pos.y > 1.f-voxLen.y && pos.z > bound.y - voxLen.z) ||
        (pos.x < voxLen.x     && pos.z > bound.y - voxLen.z) ||
        (pos.y < voxLen.y     && pos.z > bound.y - voxLen.z) ||
        (pos.x > 1.f-voxLen.x && pos.y < voxLen.y) ||
        (pos.x > 1.f-voxLen.x && pos.y > 1.f -voxLen.y) ||
        (pos.x < voxLen.x     && pos.y > 1.f -voxLen.y)
       )
        return true;
    else
        return false;
}

bool checkBoundingCell(int3 cell, int3 volRes, int size)
{
    if (any(cell > volRes - 1) || any(cell < size))
        return true;
    else
        return false;
}

// Uniform Sampling of the hemisphere above the normal n
float3 getUniformRandomSampleDirectionUpper(const float3 n, uint4 *ui_rand)
{
    float z = (hybridui_rand(ui_rand) * 2.f) - 1.f;
    float phi = hybridui_rand(ui_rand) * 2.f * M_PI_F;

    float3 sampleDirection = (float3)(sqrt(1.f - z*z) * sin(phi), sqrt(1.f - z*z) * cos(phi), z);
    float cosTheta = dot(n, sampleDirection);

    if (cosTheta < 0)
        sampleDirection *= -1;
    return sampleDirection;
}


// Calculate object space ambient occlusion factor with monte carlo sampling
float calcAO(float3 n, uint4 *ui_rand, image3d_t volData, float3 pos, float stepSize, float r,
             image1d_t tff)
{
    float ao = 0.f;
    int rays = 16;
    // rays
    for (int i = 0; i < rays; ++i)
    {
        float3 dir = getUniformRandomSampleDirectionUpper(n, ui_rand);
        float sample = 0.f;
        int cnt = 0;
        while (cnt*stepSize < r)
        {
            ++cnt;
            float density = read_imagef(volData, linearSmp, (float4)(pos + dir*cnt*stepSize, 1.f)).x;
            sample += read_imagef(tff, linearSmp, density).w;
            if (sample > 0.98f)
                break;
        }
        sample /= cnt;
        ao += sample;
    }
    ao /= (float)(rays);
    return ao;
}



/**
 * volume tracing
 */
float get_extinction(const float3 pos,
                     read_only image3d_t vol)
{
    float4 samplePos = (float4)(pos * 0.5f + 0.5f, 1.f);
    return read_imagef(vol, linearSmp, samplePos).x;
}

//
bool sample_interaction(uint rand,
                        float3 *ray_pos,
                        const float3 ray_dir,
                        const float max_extinction,
                        read_only image3d_t vol,
                        read_only image1d_t tff,
                        float4 *colorOut)
{
    float t = 0.f;
    float3 pos;
    float4 color = *colorOut;
    uint cnt = 0;
    float sample = 0.f;
    do
    {
        ++cnt;
        uint rand2 = ParallelRNG(rand);
        t -= log(1.f - mapUintFloat(rand2)) / max_extinction;
        pos = *ray_pos + ray_dir * t;
        if (!in_volume(pos))
            return false;
        sample = get_extinction(pos, vol);
        color = read_imagef(tff, linearSmp, sample);
        if (cnt > 512)  // TODO: variable or based on data set resolution
            return false;
    } while (color.w < mapUintFloat(rand));

    *colorOut = color;
    *ray_pos = pos;
    return true;
}

// phong illumination
float3 surface_scatter(float3 ray_pos,
                       float3 ray_dir,
                       read_only image3d_t vol,
                       read_only image1d_t tff,
                       float4 color)
{
    float4 samplePos = (float4)(ray_pos * 0.5f + 0.5f, 1.f);
    float4 gradient = -gradientCentralDiffTff(vol, samplePos, tff);
    return illumination(samplePos, color.xyz, -ray_dir + (float3)(0.5f, 0.5f, 0.f), gradient.xyz);
//    float p = color.w *(1.f - exp(-gradient));
}

// Get random direction from isotropic phase function (~ Henyey–Greenstein)
float3 get_dir_phase_function(uint rand)
{
    uint rand2 = ParallelRNG(rand);
    const float phi = (float)(2.0 * M_PI_F) * mapUintFloat(rand2);
    const float cos_theta = 1.0f - 2.0f * mapUintFloat(ParallelRNG(rand2));
    const float sin_theta = sqrt(1.0f - cos_theta * cos_theta);
    return (float3)(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

// volume path tracer ("exposure renderer"-style)
float3 trace_volume(uint rand,
                    float3 ray_pos,
                    float3 ray_dir,
                    float t0,
                    const float max_extinction,
                    read_only image3d_t vol,
                    read_only image1d_t tff,
                    float4 backgroundColor)
{
    float w = 1.f;
    ray_pos += ray_dir * t0;
    unsigned int num_interactions = 0;
    float4 color = backgroundColor;
    bool isInteraction = false;
    isInteraction = sample_interaction(rand, &ray_pos, ray_dir, max_extinction, vol, tff, &color);
    if (isInteraction) // scatter event
    {
        float3 light_dir = -ray_dir + (float3)(0.5f, 0.5f, 0.f);
        // surface scattering (phong based)
        float4 samplePos = (float4)(ray_pos * 0.5f + 0.5f, 1.f);
        float4 gradient = -gradientCentralDiffTff(vol, samplePos, tff);
        if (length(gradient) > 0.5f)    // high gradient -> phong illumination
        {
            color.xyz = illumination(samplePos, color.xyz, light_dir, gradient.xyz);
        }
        else    // low gradient -> second scatter ray
        {
            float3 ray_dir_scatter = get_dir_phase_function(rand);
            float3 ray_pos_scatter = ray_pos;
            float4 scatterColor = backgroundColor;
            sample_interaction(rand, &ray_pos_scatter, ray_dir_scatter, max_extinction, vol, tff, &scatterColor);
            color = mix(color, scatterColor, 0.5f);
        }
        // shadow ray towards point light
        float4 colorShadow = backgroundColor;
        isInteraction = sample_interaction(rand, &ray_pos, light_dir, max_extinction, vol, tff, &colorShadow);
        if (isInteraction)
            w = 0.6f;
    }
    return color.xyz * w;
}

// get environtment map texture coordinates from ray direction
float2 get_environment_coords(const float3 rayDir)
{
    return (float2)(atan2(rayDir.z, rayDir.x) * (float)(0.5 / M_PI) + 0.5f,
                    acos(max(min(rayDir.y, 1.0f), -1.0f)) * (float)(1.0 / M_PI));
}

//
uint4 getLastHit(__read_only image2d_t inHitImg, const int2 groupId)
{
    uint4 lastHit = read_imageui(inHitImg, groupId);
    lastHit += read_imageui(inHitImg, (int2)(groupId.x+1, groupId.y  ));
    lastHit += read_imageui(inHitImg, (int2)(groupId.x-1, groupId.y  ));
    lastHit += read_imageui(inHitImg, (int2)(groupId.x  , groupId.y+1));
    lastHit += read_imageui(inHitImg, (int2)(groupId.x  , groupId.y-1));
    lastHit += read_imageui(inHitImg, (int2)(groupId.x+1, groupId.y+1));
    lastHit += read_imageui(inHitImg, (int2)(groupId.x-1, groupId.y-1));
    lastHit += read_imageui(inHitImg, (int2)(groupId.x-1, groupId.y+1));
    lastHit += read_imageui(inHitImg, (int2)(groupId.x+1, groupId.y-1));
    return lastHit;
}

// transform vector using 3x3 matrix
float3 transformVec3(const float16 mat, const float3 vec)
{
    float3 result;
    result.x = dot(mat.s012, vec);
    result.y = dot(mat.s456, vec);
    result.z = dot(mat.s89a, vec);
    return result;
}

// structs
//
// HACK: Windows only works with packed camera struct
#ifdef WIN32
    typedef struct __attribute__ ((packed)) tag_camera_params
#else
    typedef struct tag_camera_params
#endif // WIN32
{
    float16 viewMat;
    float3 bbox_bl;     // bounding box bottom left
    float3 bbox_tr;     // bounding box top right
    uint ortho;         // bool
} camera_params;

typedef struct tag_rendering_params
{
    float4 backgroundColor;

    float3 modelScale;
    uint illumType;     // 0-off, 1-central diff, 2-central diff+tff, 3-sobel, 4-gradient mag, 5-cel shading

    uint imgEss;        // bool
    uint showEss;       // bool
    uint useLinear;     // bool
    uint useGradient;   // bool

    uint technique;     // raycast (0) or pathtracing (1)
    uint seed;
    uint iteration;
} rendering_params;

typedef struct tag_raycast_params
{
    float samplingRate;
    uint useAO;         // bool
    uint contours;      // bool
    uint aerial;        // bool

    float3 brickRes;
} raycast_params;

typedef struct tag_pathtrace_params
{
    float max_extinction;
} pathtrace_params;

/**
 * ===============================
 * direct volume raycasting kernel
 * ===============================
 */
__kernel void volumeRender(  __read_only image3d_t volData
                           , __read_only image3d_t volBrickData
                           , __read_only image1d_t tffData     // constant transfer function values
                           , __write_only image2d_t outImg
                           , __read_only image1d_t tffPrefix
                           , __read_only image2d_t inAccumulate
                           , __write_only image2d_t outAccumulate
                           , __read_only image2d_t inHitImg
                           , __write_only image2d_t outHitImg
                           , __read_only image2d_t environment
                           , const camera_params camera
                           , const rendering_params render
                           , const raycast_params raycast
                           , const pathtrace_params pathtrace
                           )
{
    int2 globalId = (int2)(get_global_id(0), get_global_id(1));
    if(any(globalId >= get_image_dim(outImg)))
        return;
    int2 texCoords = globalId;

    // pseudo random number [0,1] for ray offsets to avoid moire patterns
    uint4 ui_rand = ParallelRNG3(globalId.x, globalId.y, render.seed); //initRNG(1);
    float rand = (float)(ParallelRNG3(globalId.x, globalId.y, render.seed)) / (float)(UINT_MAX);

    float aspectRatio = native_divide((float)get_global_size(1), (float)(get_global_size(0)));
    aspectRatio = min(aspectRatio, native_divide((float)get_global_size(0), (float)(get_global_size(1))));
    int maxImgSize = max(get_global_size(0), get_global_size(1));
    float2 imgCoords;
    imgCoords.x = native_divide((globalId.x), convert_float(maxImgSize)) * 2.f;
    imgCoords.y = native_divide((globalId.y), convert_float(maxImgSize)) * 2.f;
    // calculate correct offset based on aspect ratio
    imgCoords -= get_global_size(0) > get_global_size(1) ?
                        (float2)(1.0f, aspectRatio) : (float2)(aspectRatio, 1.0);
    imgCoords.y *= -1.f;   // flip y coord

    // jitter ray starting position inside pixel
    float2 pixelSize = 2.f / (float2)(get_global_size(0), get_global_size(1));
    float rand2 = (float)(ParallelRNG3(globalId.y, globalId.x, 2*render.seed)) / (float)(UINT_MAX);
    imgCoords += (float2)(rand2, -rand)*pixelSize;

    // z position of view plane is -1.0 to fit the cube to the screen quad when axes are aligned,
    // zoom is -1 and the data set is uniform in each dimension
    // (with FoV of 90° and near plane in range [-1,+1]).
    float3 nearPlanePos = (float3)(imgCoords, -1.0f);
    // transform nearPlane from view space to world space
    float3 rayDir = transformVec3(camera.viewMat, nearPlanePos);
    // camera position in world space (ray origin) is translation vector of view matrix
    float3 camPos = camera.viewMat.s37b*render.modelScale;

    if (camera.ortho)
    {
        camPos = (float3)(camera.viewMat.s37b);
        float3 viewPlane_x = camera.viewMat.s048;
        float3 viewPlane_y = camera.viewMat.s159;
        float3 viewPlane_z = camera.viewMat.s26a;
        rayDir = -viewPlane_z;
        nearPlanePos = camPos + imgCoords.x*viewPlane_x + imgCoords.y*viewPlane_y;
        nearPlanePos *= length(camPos);
        camPos = nearPlanePos * render.modelScale;
    }
    rayDir = fast_normalize(rayDir*render.modelScale);

    // sample environment map as background color if set
    float4 envirCol = render.backgroundColor;
    envirCol *= render.useGradient ? (float4)(0.7f + 0.5f * rayDir.y) : 1.f;
    if (get_image_dim(environment).x > 1)
        envirCol = read_imagef(environment, linearSmp, get_environment_coords(rayDir));

    // image order ess
    local uint hits;
    if (render.imgEss)
    {
        hits = 0;
        uint4 lastHit = getLastHit(inHitImg, (int2)(get_group_id(0), get_group_id(1)));
        if (!lastHit.x)
        {
            write_imagef(outImg, texCoords, render.showEss ? (float4)(1.f) - envirCol : envirCol);
            write_imageui(outHitImg, (int2)(get_group_id(0), get_group_id(1)), (uint4)(0u));
            return;
        }
    }

    float tnear = FLT_MIN;
    float tfar = FLT_MAX;
    int hit = 0;
    // uniform bbox from (-1,-1,-1) to (+1,+1,+1)
    hit = intersectBBox(camPos, rayDir, camera.bbox_bl, camera.bbox_tr, &tnear, &tfar);
    if (!hit || tfar < 0)
    {
        write_imagef(outImg, texCoords, envirCol);
        if (render.imgEss)
            write_imageui(outHitImg, (int2)(get_group_id(0), get_group_id(1)), (uint4)(0u));
        return;
    }

    // ---- path tracing ----
    if (render.technique == 1)
    {
        uint random = ParallelRNG3(texCoords.x, texCoords.y, render.seed);
        float3 col = trace_volume(random, camPos, rayDir, tnear, pathtrace.max_extinction,
                                  volData, tffData, envirCol);
        // Accumulation
        if (render.iteration == 0)
        {
            write_imagef(outAccumulate, texCoords, (float4)(col, 1.f));
        }
        else
        {
            float3 prevCol = read_imagef(inAccumulate, nearestIntSmp, texCoords).xyz;
            col = prevCol + (col - prevCol) / (float3)(render.iteration + 1);
            write_imagef(outAccumulate, texCoords, (float4)(col, 1.f));
        }
        //col *= (1.f + col*0.1f) / (1.f + col);
        //col = min(pow(max(col, 0.0f), (float3)(1.f / 2.2f)), (float3)(1.0f));
        write_imagef(outImg, texCoords, (float4)(col, 1.f));
        return;
    }

    // ---- ray casting ----
    float sampleDist = tfar - tnear;
    if (sampleDist <= 0.f)
        return;
    int3 volRes = get_image_dim(volData).xyz;
    float stepSize = min(sampleDist, sampleDist /
                          (raycast.samplingRate*length(sampleDist*rayDir*convert_float3(volRes))));
    float samples = ceil(sampleDist/stepSize);
    stepSize = sampleDist/samples;

    // raycast parameters
    tnear = max(0.f, tnear);    // clamp to near plane
    float4 result = envirCol;
    float alpha = 0.f;
    float3 pos = (float3)(0);
    float density = 0.f;
    float4 tfColor = (float4)(0);
    float opacity = 0.f;
    float t = tnear;

    float3 voxLen = (float3)(1.f) / convert_float3(volRes);
    float refSamplingInterval = 1.f / raycast.samplingRate;
    float t_exit = tfar;

    // offset by random distance to avoid moiré pattern and interpolation issues
    float offset = length(voxLen)*rand*2.0f;

#ifdef ESS
    // 3D DDA initialization
    int3 bricksRes = get_image_dim(volBrickData).xyz;
//        if ((bricksRes.x & 1) != 0) bricksRes.x -= 1;
//        if ((bricksRes.y & 1) != 0) bricksRes.y -= 1;
//        if ((bricksRes.z & 1) != 0) bricksRes.z -= 1;
    float3 brickLen = (float3)(1.f) / raycast.brickRes;    // actual fractal brick resolution
    float3 invRay = 1.f/rayDir;
    int3 step = select(convert_int3(sign(rayDir)), (int3)(1), approxEq3(rayDir, (float3)(0)));
    invRay = select(invRay, (float3)(FLT_MAX), approxEq3(rayDir, (float3)(0.f)));

    float3 deltaT = convert_float3(step)*(brickLen*2.f*invRay);
    float3 voxIncr = (float3)0;

    // convert ray starting point to cell coordinates
    float3 rayOrigCell = (camPos + rayDir * tnear) - (float3)(-1.f);
    int3 cell = clamp(convert_int3(floor(rayOrigCell / (2.f*brickLen))),
                        (int3)(0), convert_int3(bricksRes.xyz) - 1);

    // add +1 to cells if ray dir component is negative: rayDir >= 0 ? (-1) : 0
    float3 tv = tnear + (convert_float3(cell - isgreaterequal(rayDir, (float3)(0)))
                            * (2.f*brickLen) - rayOrigCell) * invRay;
    int3 exit = step * bricksRes.xyz;
    exit = select(exit, (int3)(-1), exit < (int3)(0));
    // length of diagonal of a brick => longest distance through brick
    float brickDia = length(brickLen)*2.f;

    // 3D DDA loop over low-res grid for image order empty space skipping
    while (t < tfar)
    {
        float2 minMaxDensity = read_imagef(volBrickData, (int4)(cell, 0)).xy;
        // increment to next brick
        voxIncr.x = (tv.x <= tv.y) && (tv.x <= tv.z) ? 1 : 0;
        voxIncr.y = (tv.y <= tv.x) && (tv.y <= tv.z) ? 1 : 0;
        voxIncr.z = (tv.z <= tv.x) && (tv.z <= tv.y) ? 1 : 0;
        cell += convert_int3(voxIncr) * step;    // [0; res-1]

        t_exit = dot((float3)(1), tv * voxIncr);
        t_exit = clamp(t_exit, t+stepSize, t+brickDia);
        tv += voxIncr*deltaT;

        // skip bricks that contain only fully transparent voxels
        float alphaMax = read_imagef(tffData, linearSmp, minMaxDensity.y).w;
        if (alphaMax < 1e-6f)
        {
            uint prefixMin = read_imageui(tffPrefix, nearestSmp, minMaxDensity.x).x;
            uint prefixMax = read_imageui(tffPrefix, nearestSmp, minMaxDensity.y).x;
            if (prefixMin == prefixMax)
            {
                t = t_exit;
                continue;
            }
        }
#endif  // ESS
        // standard raycasting loop
        while (t < t_exit)
        {
            pos = camPos + (t-offset)*rayDir;
            pos = pos * 0.5f + 0.5f;    // normalize to [0,1]

            float4 gradient = (float4)(0.f);
            if (render.illumType == 4)   // gradient magnitude based shading
            {
                gradient = -gradientCentralDiff(volData, (float4)(pos, 1.f));
                tfColor = read_imagef(tffData, linearSmp, -gradient.w);
            }
            else    // density based shading and optional illumination
            {
                if (get_image_channel_order(volData) == CLK_R)
                {
                    density = render.useLinear ? read_imagef(volData,  linearSmp, (float4)(pos, 1.f)).x
                                               : read_imagef(volData, nearestSmp, (float4)(pos, 1.f)).x;
//                    density /= 10.f;  // TODO: normalization with max density
                    tfColor = read_imagef(tffData, linearSmp, density);  // map density to color
                    if (tfColor.w > 0.1f && render.illumType)
                    {
                        switch (render.illumType)
                        {
                        case 1:     // central diff
                            gradient = -gradientCentralDiff(volData, (float4)(pos, 1.f));
                            break;
                        case 2:     // central diff & transfer function
                            gradient = -gradientCentralDiffTff(volData, (float4)(pos, 1.f), tffData);
                            break;
                        case 3:     // sobel filter
                            gradient = -gradientSobel(volData, (float4)(pos, 1.f));
                        default:
                            break;
                        }
                        if (render.illumType == 5)
                        {
                            gradient = -gradientCentralDiff(volData, (float4)(pos, 1.f));
                            tfColor.xyz = celShading(tfColor.xyz, -rayDir, gradient.xyz);
                        }
                        else
                            tfColor.xyz = illumination((float4)(pos, 1.f), tfColor.xyz, -rayDir, gradient.xyz);
                    }
                    if (tfColor.w > 0.1f && raycast.contours) // edge enhancement
                    {
                        if (!render.illumType) // no illumination
                            gradient = -gradientCentralDiff(volData, (float4)(pos, 1.f));
                        tfColor.xyz *= fabs(dot(rayDir, gradient.xyz));
                    }
                }
                // RGBA: use values directly
                else if (get_image_channel_order(volData) == CLK_RGBA)
                {
                    tfColor = render.useLinear ? read_imagef(volData,  linearSmp, (float4)(pos, 1.f))
                                               : read_imagef(volData, nearestSmp, (float4)(pos, 1.f));
                }
                // RG: 2D vector, map magnitude to alpha
                else if (get_image_channel_order(volData) == CLK_RG)
                {
                    tfColor = render.useLinear ? read_imagef(volData,  linearSmp, (float4)(pos, 1.f))
                                               : read_imagef(volData, nearestSmp, (float4)(pos, 1.f));
                    //tfColor.xyz = read_imagef(tffData, linearSmp, tfColor.x).xyz;
                    density = length(tfColor.y / 1.f);
                    tfColor.y = 0.f;
                    tfColor.w = read_imagef(tffData, linearSmp, density).w;
                }
            }
            tfColor.xyz = envirCol.xyz - tfColor.xyz;
            if (raycast.aerial) // depth cue as aerial perspective
            {
                float depthCue = 1.f - (t - tnear)/sampleDist; // [0..1]
                tfColor.w *= depthCue;
            }

            // Taylor expansion approximation
            opacity = 1.f - native_powr(1.f - tfColor.w, refSamplingInterval);
            result.xyz = result.xyz - tfColor.xyz * opacity * (1.f - alpha);
            alpha = alpha + opacity * (1.f - alpha);

            if (t >= tfar) break;
            if (alpha > ERT_THRESHOLD)   // early ray termination check
            {
                if (raycast.useAO)  // ambient occlusion only on solid surfaces
                {
                    float3 n = -gradientCentralDiff(volData, (float4)(pos, 1.f)).xyz;
                    float ao = calcAO(n, &ui_rand, volData, pos, length(voxLen)*0.9f, length(voxLen)*5.f, tffData);
                    result.xyz *= 1.f - 0.5f*ao;
                }
                break;
            }
            t += stepSize;
        }
#ifdef ESS
        if (t >= tfar || alpha > ERT_THRESHOLD) break;
        if (any(cell == exit)) break;
        t = t_exit;
    }
#endif  // ESS

    // visualize empty space skipping
    if (render.showEss)
    {
        if (checkBoundingBox(pos, voxLen, (float2)(0.f, 1.f)))
        {
            result.xyz = fabs((float3)(1.f) - render.backgroundColor.xyz);
            alpha = 1.f;
        }
    }
    // write final image
    result.w = alpha;
    if (render.iteration == 0)
    {
        write_imagef(outAccumulate, texCoords, result);
    }
    else
    {
        float3 prevCol = read_imagef(inAccumulate, nearestIntSmp, texCoords).xyz;
        result.xyz = prevCol + (result.xyz - prevCol) / (float3)(render.iteration + 1);
        write_imagef(outAccumulate, texCoords, result);
    }
    write_imagef(outImg, texCoords, result);

    // image order empty space skipping
    if (render.imgEss)
    {
        barrier(CLK_LOCAL_MEM_FENCE);
        if (any(result.xyz != envirCol.xyz))
            ++hits;
        barrier(CLK_LOCAL_MEM_FENCE);
        if (get_local_id(0) + get_local_id(1) == 0)
        {
            if (hits == 0)
                write_imageui(outHitImg, (int2)(get_group_id(0), get_group_id(1)), (uint4)(0u));
            else
                write_imageui(outHitImg, (int2)(get_group_id(0), get_group_id(1)), (uint4)(1u));
        }
    }
}

#pragma OPENCL EXTENSION cl_khr_3d_image_writes : enable

//************************** Generate brick volume ***************************

__kernel void generateBricks(  __read_only image3d_t volData
                             , __write_only image3d_t volBrickData
                            )
{
    int3 coord = (int3)(get_global_id(0), get_global_id(1), get_global_id(2));
    if(any(coord >= get_image_dim(volBrickData).xyz))
        return;

    int3 voxPerCell = convert_int3(ceil(convert_float4(get_image_dim(volData))/
                                          convert_float4(get_image_dim(volBrickData))).xyz);
    int3 volCoordLower = voxPerCell * coord;
    int3 volCoordUpper = clamp(volCoordLower + voxPerCell, (int3)(0), get_image_dim(volData).xyz-1);

    float maxVal = 0.f;
    float minVal = 1.f;
    float value = 0.f;
    for (int k = volCoordLower.z; k < volCoordUpper.z; ++k)
    {
        for (int j = volCoordLower.y; j < volCoordUpper.y; ++j)
        {
            for (int i = volCoordLower.x; i < volCoordUpper.x; ++i)
            {
                value = read_imagef(volData, nearestIntSmp, (int4)(i, j, k, 0)).x;  // [0; 1]
                minVal = min(minVal, value);
                maxVal = max(maxVal, value);
            }
        }
    }
    write_imagef(volBrickData, (int4)(coord, 0), (float4)(minVal, maxVal, 0, 1.f));
}


//************************** Downsample volume ***************************

__kernel void downsampling(  __read_only image3d_t volData
                           , __write_only image3d_t volDataLowRes
                          )
{
    int3 coord = (int3)(get_global_id(0), get_global_id(1), get_global_id(2));
    if(any(coord >= get_image_dim(volDataLowRes).xyz))
        return;

    int3 voxPerCell = convert_int3(ceil(convert_float4(get_image_dim(volData))/
                                          convert_float4(get_image_dim(volDataLowRes))).xyz);
    int3 volCoordLower = voxPerCell * coord;
    int3 volCoordUpper = clamp(volCoordLower + voxPerCell, (int3)(0), get_image_dim(volData).xyz);

    float value = 0.f;

    for (int k = volCoordLower.z; k < volCoordUpper.z; ++k)
    {
        for (int j = volCoordLower.y; j < volCoordUpper.y; ++j)
        {
            for (int i = volCoordLower.x; i < volCoordUpper.x; ++i)
            {
                value += read_imagef(volData, nearestIntSmp, (int4)(i, j, k, 0)).x;  // [0; 1]
            }
        }
    }
    value /= (float)(voxPerCell.x * voxPerCell.y * voxPerCell.z);

    write_imagef(volDataLowRes, (int4)(coord, 0), (float4)(value));
}
