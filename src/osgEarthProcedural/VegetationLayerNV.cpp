/* -*-c++-*- */
/* osgEarth - Geospatial SDK for OpenSceneGraph
* Copyright 2008-2012 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#include "VegetationLayerNV"
#include "ProceduralShaders"
#include "NoiseTextureFactory"

#include <osgEarth/VirtualProgram>
#include <osgEarth/CameraUtils>
#include <osgEarth/Shaders>
#include <osgEarth/LineDrawable>
#include <osgEarth/NodeUtils>
#include <osgEarth/Registry>
#include <osgEarth/Capabilities>
#include <osgEarth/Math>
#include <osgEarth/TerrainEngineNode>
#include <osgEarth/Metrics>
#include <osgEarth/GLUtils>
#include <osgEarth/rtree.h>

#include <osg/BlendFunc>
#include <osg/Multisample>
#include <osg/Texture2D>
#include <osg/Version>
#include <osg/ComputeBoundsVisitor>
#include <osg/MatrixTransform>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgDB/FileNameUtils>
#include <osgUtil/CullVisitor>
#include <osgUtil/Optimizer>
#include <osgUtil/SmoothingVisitor>

#include <cstdlib> // getenv
#include <random>

#define LC "[VegetationLayerNV] " << getName() << ": "

#define OE_DEVEL OE_DEBUG

//#define ATLAS_SAMPLER "oe_veg_atlas"
#define NOISE_SAMPLER "oe_veg_noiseTex"

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

#ifndef GL_SAMPLE_ALPHA_TO_COVERAGE_ARB
#define GL_SAMPLE_ALPHA_TO_COVERAGE_ARB   0x809E
#endif

using namespace osgEarth;
using namespace osgEarth::Procedural;

REGISTER_OSGEARTH_LAYER(vegetationnv, VegetationLayerNV);

// TODO LIST
//

//  - BUG: Close/Open VegLayer doesn't work

//  - TODO: separate the cloud's LUTs and TextureArena into different statesets, since they
//    are never used together. This will skip binding all the LUTS when only rendering, and
//    will skip binding the TA when only computing.
//  - Move normal map conversion code out of here, and move towards a "MaterialTextureSet"
//    kind of setup that will support N material textures at once.
//  - FEATURE: automatically generate billboards? Imposters? Other?
//  - [IDEA] programmable SSE for models?
//  - Idea: include a "model range" or "max SSE" in the InstanceBuffer...?
//  - [PERF] thin out distant instances automatically in large tiles
//  - [PERF] cull by "horizon" .. e.g., the lower you are, the fewer distant trees...?
//  - Figure out exactly where some of this functionality goes -- VL or IC? For example the
//    TileManager stuff seems like it would go in IC?
//  - variable spacing or clumping by asset...?
//  - make the noise texture bindless as well? Stick it in the arena? Why not.

//  - (DONE) [BUG] biomelayer from cache will NOT generate biome update notifications 6/25/21
//  - (DONE) fix the random asset select with weighting...just not really working well.
//  - (DONE) Allow us to store key, slot, etc data in the actual TileContext coming from the 
//    PatchLayer. It is silly to have to do TileKey lookups and not be able to simple
//    iterate over the TileBatch.
//  - (DONE) Lighting: do something about billboard lighting. We might have to generate normal 
//    maps when we make the imposter billboards (if we make them).
//  - (DONE) FEATURE: FADE in 3D models from billboards
//  - (DONE) BUG: multiple biomes, same asset in each biome; billboard sizes are messed up.
//  - (DONE - using indexes instead of copies) Reduce the size of the RenderBuffer structure
//  - (DONE) Fix model culling. The "radius" isn't quite sufficient since the origin is not at the center,
//    AND because rotation changes the profile. Calculate it differently.
//  - (DONE) [OPT] reduce the SIZE of the instance buffer by re-using variables (instanceID, drawID, assetID)
//  - (DONE) Figure out how to pre-compile/ICO the TextureArena; it takes quite a while.
//  - (DONE) [OPT] on Generate, store the instance count per tile in an array somewhere. Also store the 
//    TOTAL instance count in the DI buffer. THen use this to dispatchComputeIndirect for the
//    MERGE. That way we don't have to dispatch to "all possible locations" during a merge,
//    hopefully making it much faster and avoiding frame breaks.
//  - (DONE) FIX the multi-GC case. A resident handle can't cross GCs, so we will need
//    to upgrade TextureArena (Texture) to store one object/handle per GC.
//    We will also need to replace the sampler IDs in teh LUT shader with a mapping
//    to a UBO (or something) that holds the actual resident handles. Blah..
//  - (DONE) Properly delete all GL memory .. use the Releaser on the GC thread?
//  - (DONE) Can we remove the normal matrix?
//  - (DONE) [OPT] combine multiple object lists into the GLObjectReleaser
//  - (DONE) FIX to work with SHADOW camera (separate culling...maybe?) (reference viewpoint?)
//  - (DONE) FIX the multi-CAMERA case.
//  - (DONE) Do NOT regenerate every tile every time the tilebatch changes!
//  - (DONE .. had to call glBindBufferRange each frame) Two GC layers at the same time doesn't work! (grass + trees)
//  - (DONE) FIX: IC's atlas is hard-coded to texture image unit 11. Allocate it dynamically.
//  - (DONE .. the lighting shader was executing in the wrong order) Lighting
//  - (DONE .. was good!!) read back the instance count to reduce the dispatch #?
//  - (DONE .. was a bad oe_veg_Assets setup in the LUT shader) Fix teh "flashing" bug :(
//  - (DONE .. merged at layer level) BUGFIX: we're merging the geometrycloud's stateset into the layer's stateset. Make sure that's kosher...?
//  - (DONE) Fix the GRASS LAYER
//  - (DONE) Rotation for models
//  - (DONE) Scaling of the 3D models to match the height/width....? or not? set from loaded model?

//........................................................................


namespace
{
    static const std::string s_assetGroupName[2] = {
        "trees",
        "undergrowth"
    };

    const char* vs_model = R"(

#version 460
#extension GL_ARB_gpu_shader_int64 : enable
#pragma import_defines(OE_USE_ALPHA_TO_COVERAGE)

layout(binding=1, std430) buffer TextureArena {
    uint64_t textures[];
};
struct Instance
{
    mat4 xform;
    vec2 local_uv;
    float fade;
    float visibility[4];
    uint first_variant_cmd_index;
};
layout(binding=0, std430) buffer Instances {
    Instance instances[];
};

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec4 color;
layout(location=3) in vec2 uv;
layout(location=4) in vec3 flex;
layout(location=5) in int albedo; // todo: material LUT index
layout(location=6) in int normalmap; // todo: material LUT index

// out to fragment shader
out vec3 vp_Normal;
out vec4 vp_Color;
out vec2 oe_tex_uv;
out float oe_fade;
flat out uint64_t oe_albedo_handle;
flat out uint64_t oe_normalmap_handle;

// stage globals
mat3 vec3xform;

void vegetation_vs_model(inout vec4 vertex)
{
    int i = gl_BaseInstance + gl_InstanceID;
    mat4 xform = instances[i].xform;
    vertex = xform * vec4(position, 1);
    vp_Color = color;
    vec3xform = mat3(xform);
    vp_Normal = vec3xform * normal;
    oe_tex_uv = uv;
    oe_fade = instances[i].fade;
    oe_albedo_handle = albedo >= 0 ? textures[albedo] : 0;
    oe_normalmap_handle = normalmap >= 0 ? textures[normalmap] : 0;
};

    )";

    const char* vs_view = R"(

#version 460
#extension GL_ARB_gpu_shader_int64 : enable
#pragma import_defines(OE_WIND_TEX)
#pragma import_defines(OE_WIND_TEX_MATRIX)

struct Instance
{
    mat4 xform;
    vec2 local_uv;
    float fade;
    float visibility[4];
    uint first_variant_cmd_index;
};
layout(binding=0, std430) buffer Instances {
    Instance instances[];
};

layout(location=4) in vec3 flex;

// outputs
out vec3 vp_Normal;
out vec3 oe_tangent;
out vec3 oe_pos3_view;
flat out uint64_t oe_normalmap_handle;

// stage globals
mat3 vec3xform; // set in model function

#ifdef OE_WIND_TEX
uniform sampler3D OE_WIND_TEX ;
uniform mat4 OE_WIND_TEX_MATRIX ;
uniform float osg_FrameTime;
uniform sampler2D oe_veg_noise;

uniform float wind_power = 1.0;

#define remap(X, LO, HI) (LO + X * (HI - LO))

void apply_wind(inout vec4 vertex, in vec2 local_uv)
{
    float flexibility = length(flex);
    if (flexibility > 0.0) {
        vec4 wind = textureProj(OE_WIND_TEX, (OE_WIND_TEX_MATRIX * vertex));
        vec3 wind_dir = normalize(wind.rgb * 2 - 1); // view space
        const float rate = 0.01;
        vec4 noise_moving = textureLod(oe_veg_noise, local_uv + osg_FrameTime * rate, 0);
        float speed_var = remap(noise_moving[3], -0.2, 1.4);
        float speed = wind.a * speed_var;
        vec3 bend_vec = wind_dir * speed;
        vec3 flex_dir = normalize(gl_NormalMatrix * vec3xform * flex);
        float flex_planar = abs(dot(wind_dir, flex_dir));
        flex_planar = 1.0 - (flex_planar*flex_planar);
        vertex.xyz += bend_vec * flex_planar * flexibility * wind_power;
    }
}
#endif

void vegetation_vs_view(inout vec4 vertex)
{
    int i = gl_BaseInstance + gl_InstanceID;

    oe_pos3_view = vertex.xyz;

    if (oe_normalmap_handle > 0)
    {
        vec3 ZAXIS = gl_NormalMatrix * vec3(0,0,1);
        if (dot(ZAXIS, vp_Normal) > 0.95)
            oe_tangent = gl_NormalMatrix * (vec3xform * vec3(1,0,0));
        else
            oe_tangent = cross(ZAXIS, vp_Normal);
    }

#ifdef OE_WIND_TEX
    apply_wind(vertex, instances[i].local_uv);
#endif
}

)";

    const char* fs = R"(

#version 430
#extension GL_ARB_gpu_shader_int64 : enable
#pragma import_defines(OE_USE_ALPHA_TO_COVERAGE)

in vec2 oe_tex_uv;
in vec3 oe_pos3_view;
in float oe_fade;
in vec3 vp_Normal;
in vec3 oe_tangent;
flat in uint64_t oe_albedo_handle;
flat in uint64_t oe_normalmap_handle;

void vegetation_fs(inout vec4 color)
{
    if (oe_albedo_handle > 0)
    {
        vec4 texel = texture(sampler2D(oe_albedo_handle), oe_tex_uv);
        color *= texel;
    }

    if (oe_normalmap_handle > 0)
    {
        vec4 n = texture(sampler2D(oe_normalmap_handle), oe_tex_uv);

#ifdef COMPRESSED_NORMAL
        n.xyz = n.xyz*2.0 - 1.0;
        n.z = 1.0 - abs(n.x) - abs(n.y);
        float t = clamp(-n.z, 0, 1);
        n.x += (n.x > 0) ? -t : t;
        n.y += (n.y > 0) ? -t : t;
#else
        n.xyz = normalize(n.xyz*2.0-1.0);
#endif

        // construct the TBN and apply to normal,
        // reflecting the normal on back-facing polys
        mat3 tbn = mat3(
            normalize(oe_tangent),
            normalize(cross(vp_Normal, oe_tangent)),
            gl_FrontFacing ? vp_Normal : -vp_Normal);
        
        vp_Normal = normalize(tbn * n.xyz);
    }

    // cull fading for LOD transitions:
    color.a *= oe_fade;

    // alpha-down faces that are orthogonal to the view vector:
    vec3 face_normal = normalize(cross(dFdx(oe_pos3_view), dFdy(oe_pos3_view)));
    float d = clamp(1.2*abs(dot(face_normal, normalize(oe_pos3_view))),0.0,1.0);
    color.a *= d;

#ifdef OE_USE_ALPHA_TO_COVERAGE
    // mitigate the screen-door effect of A2C in the distance
    // https://tinyurl.com/y7bbbpl9
    //const float threshold = 0.15;
    //float a = (color.a - threshold) / max(fwidth(color.a), 0.0001) + 0.5;
    //color.a = mix(color.a, a, unit_distance_to_vert);

    // adjust the alpha based on the calculated mipmap level:
    // better, but a bit more expensive than the above method? Benchmark?
    // https://tinyurl.com/fhu4zdxz
    if (oe_albedo_handle > 0UL)
    {
        ivec2 tsize = textureSize(sampler2D(oe_albedo_handle), 0);
        vec2 cf = vec2(float(tsize.x)*oe_tex_uv.s, float(tsize.y)*oe_tex_uv.t);
        vec2 dx_vtc = dFdx(cf);
        vec2 dy_vtc = dFdy(cf);
        float delta_max_sqr = max(dot(dx_vtc, dx_vtc), dot(dy_vtc, dy_vtc));
        float mml = max(0, 0.5 * log2(delta_max_sqr));
        color.a *= (1.0 + mml * 0.25);
    }
#else
    // force alpha to 0 or 1 and threshold it.
    const float threshold = 0.15;
    color.a = step(threshold, color.a);
    if (color.a < threshold)
        discard;
#endif
}

    )";
}

//........................................................................

Config
VegetationLayerNV::Options::getConfig() const
{
    Config conf = PatchLayer::Options::getConfig();
    colorLayer().set(conf, "color_layer");
    biomeLayer().set(conf, "biomes_layer");

    conf.set("color_min_saturation", colorMinSaturation());
    conf.set("alpha_to_coverage", alphaToCoverage());
    conf.set("max_sse", maxSSE());

    //TODO: groups

    return conf;
}

void
VegetationLayerNV::Options::fromConfig(const Config& conf)
{
    // defaults:
    alphaToCoverage().setDefault(true);
    maxSSE().setDefault(150.0f);

    colorLayer().get(conf, "color_layer");
    biomeLayer().get(conf, "biomes_layer");

    conf.get("color_min_saturation", colorMinSaturation());
    conf.get("alpha_to_coverage", alphaToCoverage());
    conf.get("max_sse", maxSSE());

    // defaults for groups:
    groups().resize(NUM_ASSET_GROUPS);

    if (AssetGroup::TREES < NUM_ASSET_GROUPS)
    {
        groups()[AssetGroup::TREES].enabled() = true;
        groups()[AssetGroup::TREES].castShadows() = true;
        groups()[AssetGroup::TREES].maxRange() = 2500.0f;
        groups()[AssetGroup::TREES].lod() = 14;
        groups()[AssetGroup::TREES].spacing() = Distance(15.0f, Units::METERS);
        groups()[AssetGroup::TREES].maxAlpha() = 0.15f;
    }

    if (AssetGroup::UNDERGROWTH < NUM_ASSET_GROUPS)
    {
        groups()[AssetGroup::UNDERGROWTH].enabled() = true;
        groups()[AssetGroup::UNDERGROWTH].castShadows() = false;
        groups()[AssetGroup::UNDERGROWTH].maxRange() = 75.0f;
        groups()[AssetGroup::UNDERGROWTH].lod() = 19;
        groups()[AssetGroup::UNDERGROWTH].spacing() = Distance(1.0f, Units::METERS);
        groups()[AssetGroup::UNDERGROWTH].maxAlpha() = 0.75f;
    }

    ConfigSet groups_c = conf.child("groups").children();
    for (auto& group_c : groups_c)
    {
        int g = -1;
        std::string name;
        group_c.get("name", name);
        if (name == AssetGroup::name(AssetGroup::TREES))
            g = AssetGroup::TREES;
        else if (name == AssetGroup::name(AssetGroup::UNDERGROWTH)) 
            g = AssetGroup::UNDERGROWTH;

        if (g >= 0 && g < NUM_ASSET_GROUPS)
        {
            Group& group = groups()[g];
            group_c.get("enabled", group.enabled());
            group_c.get("spacing", group.spacing());
            group_c.get("max_range", group.maxRange());
            group_c.get("lod", group.lod());
            group_c.get("cast_shadows", group.castShadows());
            group_c.get("max_alpha", group.maxAlpha());
        }
    }
}

//........................................................................

bool
VegetationLayerNV::LayerAcceptor::acceptLayer(osg::NodeVisitor& nv, const osg::Camera* camera) const
{
    // if this is a shadow camera and the layer is configured to cast shadows, accept it.
    if (CameraUtils::isShadowCamera(camera))
    {        
        return _layer->getCastShadows();
    }

    // if this is a depth-pass camera (and not a shadow cam), reject it.
    if (CameraUtils::isDepthCamera(camera))
    {
        return false;
    }

    // otherwise accept the layer.
    return true;
}

bool
VegetationLayerNV::LayerAcceptor::acceptKey(const TileKey& key) const
{
     return _layer->hasGroupAtLOD(key.getLOD());
}

//........................................................................

void VegetationLayerNV::setMaxSSE(float value)
{
    if (value != options().maxSSE().get())
    {
        options().maxSSE() = value;
        if (_sseU.valid())
            _sseU->set(value);
    }
}

float VegetationLayerNV::getMaxSSE() const
{
    return options().maxSSE().get();
}

//........................................................................

void
VegetationLayerNV::init()
{
    PatchLayer::init();

    setAcceptCallback(new LayerAcceptor(this));
}

VegetationLayerNV::~VegetationLayerNV()
{
    close();
}

Status
VegetationLayerNV::openImplementation()
{
    // GL version requirement
    if (Registry::capabilities().getGLSLVersion() < 4.6f)
    {
        return Status(Status::ResourceUnavailable, "Requires GL 4.6+");
    }

    // Clamp the layer's max visible range the maximum range of the farthest
    // asset group. This will minimize the number of tiles sent to the
    // renderer and improve performance. Doing this here (in open) so the
    // user can close a layer, adjust parameters, and re-open if desired
    float max_range = 0.0f;
    for (auto& group : options().groups())
    {
        max_range = std::max(max_range, group.maxRange().get());
    }
    max_range = std::min(max_range, getMaxVisibleRange());
    setMaxVisibleRange(max_range);

    return PatchLayer::openImplementation();
}

Status
VegetationLayerNV::closeImplementation()
{
    releaseGLObjects(nullptr);
    return PatchLayer::closeImplementation();
}

void
VegetationLayerNV::update(osg::NodeVisitor& nv)
{
    // this code will release memory after the layer's not been
    // used for a while.
    if (isOpen())
    {
        int df = nv.getFrameStamp()->getFrameNumber() - _lastVisit.getFrameNumber();
        double dt = nv.getFrameStamp()->getReferenceTime() - _lastVisit.getReferenceTime();

        if (dt > 5.0 && df > 60)
        {
            releaseGLObjects(nullptr);

            reset();

            if (getBiomeLayer())
                getBiomeLayer()->getBiomeManager().reset();

            OE_INFO << LC << "timed out for inactivity." << std::endl;
        }

        checkForNewAssets();

        Assets newAssets = _newAssets.release();
        if (!newAssets.empty())
        {
            _assets = std::move(newAssets);
        }
    }
}

void
VegetationLayerNV::setBiomeLayer(BiomeLayer* layer)
{
    _biomeLayer.setLayer(layer);

    if (layer)
    {
        buildStateSets();
    }
}

BiomeLayer*
VegetationLayerNV::getBiomeLayer() const
{
    return _biomeLayer.getLayer();
}

void
VegetationLayerNV::setLifeMapLayer(LifeMapLayer* layer)
{
    _lifeMapLayer.setLayer(layer);
    if (layer)
    {
        buildStateSets();
    }
}

LifeMapLayer*
VegetationLayerNV::getLifeMapLayer() const
{
    return _lifeMapLayer.getLayer();
}

void
VegetationLayerNV::setColorLayer(ImageLayer* value)
{
    options().colorLayer().setLayer(value);
    if (value)
    {
        buildStateSets();
    }
}

ImageLayer*
VegetationLayerNV::getColorLayer() const
{
    return options().colorLayer().getLayer();
}

void
VegetationLayerNV::setUseAlphaToCoverage(bool value)
{
    options().alphaToCoverage() = value;
}

bool
VegetationLayerNV::getUseAlphaToCoverage() const
{
    return options().alphaToCoverage().get();
}

bool
VegetationLayerNV::getCastShadows() const
{
    for (int i = 0; i < NUM_ASSET_GROUPS; ++i)
    {
        if (options().groups()[i].castShadows() == true)
            return true;
    }
    return false;
}

bool
VegetationLayerNV::hasGroupAtLOD(unsigned lod) const
{
    for (int i = 0; i < NUM_ASSET_GROUPS; ++i)
    {
        if (options().groups()[i].lod() == lod)
            return true;
    }
    return false;
}

AssetGroup::Type
VegetationLayerNV::getGroupAtLOD(unsigned lod) const
{
    for (int i = 0; i < NUM_ASSET_GROUPS; ++i)
    {
        if (options().groups()[i].lod() == lod)
            return (AssetGroup::Type)i;
    }
    return AssetGroup::UNDEFINED;
}

unsigned
VegetationLayerNV::getGroupLOD(AssetGroup::Type group) const
{
    if (group > 0 && group < NUM_ASSET_GROUPS)
        return options().group(group).lod().get();
    else
        return 0;
}

void
VegetationLayerNV::setMaxRange(AssetGroup::Type type, float value)
{
    OE_HARD_ASSERT(type < NUM_ASSET_GROUPS);

    auto& group = options().group(type);
    group.maxRange() = value;
}

float
VegetationLayerNV::getMaxRange(AssetGroup::Type type) const
{
    OE_HARD_ASSERT(type < NUM_ASSET_GROUPS);
    return options().group(type).maxRange().get();
}

void
VegetationLayerNV::setEnabled(AssetGroup::Type type, bool value)
{
    OE_HARD_ASSERT(type < NUM_ASSET_GROUPS);

    auto& group = options().group(type);
    group.enabled() = value;
}

bool
VegetationLayerNV::getEnabled(AssetGroup::Type type) const
{
    OE_HARD_ASSERT(type < NUM_ASSET_GROUPS);
    return options().group(type).enabled().get();
}

void
VegetationLayerNV::addedToMap(const Map* map)
{
    PatchLayer::addedToMap(map);

    if (!getLifeMapLayer())
        setLifeMapLayer(map->getLayer<LifeMapLayer>());

    if (!getBiomeLayer())
        setBiomeLayer(map->getLayer<BiomeLayer>());

    options().colorLayer().addedToMap(map);

    if (getColorLayer())
    {
        OE_INFO << LC << "Color modulation layer is \"" << getColorLayer()->getName() << "\"" << std::endl;
        if (getColorLayer()->isShared() == false)
        {
            OE_WARN << LC << "Color modulation is not shared and is therefore being disabled." << std::endl;
            options().colorLayer().removedFromMap(map);
        }
    }

    _map = map;

    _mapProfile = map->getProfile();

    if (getBiomeLayer() == nullptr)
    {
        setStatus(Status::ResourceUnavailable, "No Biomes layer available in the Map");
        return;
    }

    if (getLifeMapLayer() == nullptr)
    {
        setStatus(Status::ResourceUnavailable, "No LifeMap available in the Map");
        return;
    }
}

void
VegetationLayerNV::removedFromMap(const Map* map)
{
    PatchLayer::removedFromMap(map);

    options().colorLayer().removedFromMap(map);
}

void
VegetationLayerNV::prepareForRendering(TerrainEngine* engine)
{
    PatchLayer::prepareForRendering(engine);

    // Holds all vegetation textures:
    _textures = new TextureArena();
    _textures->setBindingPoint(1);

    // make a 4-channel noise texture to use
    NoiseTextureFactory noise;
    _noiseTex = noise.create(256u, 4u);

    TerrainResources* res = engine->getResources();
    if (res)
    {
        if (_noiseBinding.valid() == false)
        {
            if (res->reserveTextureImageUnitForLayer(_noiseBinding, this, "VegLayerNV noise sampler") == false)
            {
                setStatus(Status::ResourceUnavailable, "No texture unit available for noise sampler");
                return;
            }
        }

        // Compute LOD for each asset group if necessary.
        for (int g = 0; g < options().groups().size(); ++g)
        {
            Options::Group& group = options().group((AssetGroup::Type)g);
            if (!group.lod().isSet())
            {
                unsigned bestLOD = 0;
                for (unsigned lod = 1; lod <= 99; ++lod)
                {
                    bestLOD = lod - 1;
                    float lodRange = res->getVisibilityRangeHint(lod);
                    if (group.maxRange() > lodRange || lodRange == FLT_MAX)
                    {
                        break;
                    }
                }
                group.lod() = bestLOD;

                OE_INFO << LC 
                    << "Rendering asset group" << s_assetGroupName[g] 
                    << " at terrain level " << bestLOD <<  std::endl;
            }
        }
    }

    buildStateSets();
}

namespace
{
    // adds the defines for a shared image layer
    void bind(
        ImageLayer* layer,
        const std::string& sampler,
        const std::string& matrix,
        osg::StateSet* stateset )
    {
        if (layer) {
            stateset->setDefine(sampler, layer->getSharedTextureUniformName());
            stateset->setDefine(matrix, layer->getSharedTextureMatrixUniformName());
        }
        else {
            stateset->removeDefine(sampler);
            stateset->removeDefine(matrix);
        }
    }
}

void
VegetationLayerNV::buildStateSets()
{
    if (!getBiomeLayer()) {
        OE_DEBUG << LC << "buildStateSets deferred.. biome layer not available" << std::endl;
        return;
    }
    if (!getLifeMapLayer()) {
        OE_DEBUG << LC << "buildStateSets deferred.. lifemap layer not available" << std::endl;
        return;
    }

    // NEXT assemble the asset group statesets.
    if (AssetGroup::TREES < NUM_ASSET_GROUPS)
        configureTrees();

    if (AssetGroup::UNDERGROWTH < NUM_ASSET_GROUPS)
        configureGrass();

    osg::StateSet* ss = getOrCreateStateSet();

    // Install the texture arena:
    ss->setAttribute(_textures, 1);

    VirtualProgram* vp = VirtualProgram::getOrCreate(ss);
    vp->addGLSLExtension("GL_ARB_gpu_shader_int64");
    vp->setFunction("vegetation_vs_model", vs_model, ShaderComp::LOCATION_VERTEX_MODEL);
    vp->setFunction("vegetation_vs_view", vs_view, ShaderComp::LOCATION_VERTEX_VIEW);
    vp->setFunction("vegetation_fs", fs, ShaderComp::LOCATION_FRAGMENT_COLORING);

    // bind the noise sampler.
    ss->setTextureAttribute(_noiseBinding.unit(), _noiseTex.get(), 1);
    ss->addUniform(new osg::Uniform("oe_veg_noise", _noiseBinding.unit()));

    // If multisampling is on, use alpha to coverage.
    if (osg::DisplaySettings::instance()->getNumMultiSamples() > 1)
    {
        ss->setDefine("OE_USE_ALPHA_TO_COVERAGE");
        ss->setMode(GL_MULTISAMPLE, 1);
        ss->setMode(GL_BLEND, 0);
        ss->setMode(GL_SAMPLE_ALPHA_TO_COVERAGE_ARB, 1);
    }
}

void
VegetationLayerNV::configureTrees()
{
    auto& trees = options().group(AssetGroup::TREES);

    // functor for generating cross hatch geometry for trees:
    trees._createImposter = [](
        const osg::BoundingBox& b,
        std::vector<osg::Texture*>& textures)
    {
        osg::Group* group = new osg::Group();
        osg::Geometry* geom[2];

        // one part if we only have side textures;
        // two parts if we also have top textures
        int parts = textures.size() > 2 ? 2 : 1;

        float xmin = std::min(b.xMin(), b.yMin());
        float ymin = xmin;
        float xmax = std::max(b.xMax(), b.yMax());
        float ymax = xmax;

        for (int i = 0; i < parts; ++i)
        {
            geom[i] = new osg::Geometry();
            geom[i]->setUseVertexBufferObjects(true);
            geom[i]->setUseDisplayList(false);

            osg::StateSet* ss = geom[i]->getOrCreateStateSet();

            const osg::Vec4f colors[1] = {
                {1,1,1,1}
            };

            if (i == 0)
            {
                static const GLushort indices[12] = {
                    0,1,2,  2,3,0,
                    4,5,6,  6,7,4 };

                const osg::Vec3f verts[8] = {
                    { xmin, 0, b.zMin() },
                    { xmax, 0, b.zMin() },
                    { xmax, 0, b.zMax() },
                    { xmin, 0, b.zMax() },
                    { 0, ymin, b.zMin() },
                    { 0, ymax, b.zMin() },
                    { 0, ymax, b.zMax() },
                    { 0, ymin, b.zMax() }
                };

                const osg::Vec3f normals[8] = {
                    {0,-1,0}, {0,-1,0}, {0,-1,0}, {0,-1,0},
                    {1,0,0}, {1,0,0}, {1,0,0}, {1,0,0}
                };

                const osg::Vec2f uvs[8] = {
                    {0,0},{1,0},{1,1},{0,1},
                    {0,0},{1,0},{1,1},{0,1}
                };

                geom[i]->addPrimitiveSet(new osg::DrawElementsUShort(GL_TRIANGLES, 12, &indices[0]));
                geom[i]->setVertexArray(new osg::Vec3Array(8, verts));
                geom[i]->setNormalArray(new osg::Vec3Array(8, normals));
                geom[i]->setColorArray(new osg::Vec4Array(1, colors), osg::Array::BIND_OVERALL);
                geom[i]->setTexCoordArray(0, new osg::Vec2Array(8, uvs));
                geom[i]->setTexCoordArray(3, new osg::Vec3Array(8)); // flexors

                if (textures.size() > 0)
                    ss->setTextureAttribute(0, textures[0], 1); // side albedo
                if (textures.size() > 1)
                    ss->setTextureAttribute(1, textures[1], 1); // side normal
            }
            else if (i == 1)
            {
                float zmid = 0.33f*(b.zMax() - b.zMin());

                static const GLushort indices[6] = {
                    0,1,2,  2,3,0
                };
                const osg::Vec3f verts[4] = {
                    {xmin, ymin, zmid},
                    {xmax, ymin, zmid},
                    {xmax, ymax, zmid},
                    {xmin, ymax, zmid}
                };
                const osg::Vec3f normals[4] = {
                    {0,0,1}, {0,0,1}, {0,0,1}, {0,0,1}
                };
                const osg::Vec2f uvs[4] = {
                    {0,0}, {1,0}, {1,1}, {0,1}
                };

                geom[i]->addPrimitiveSet(new osg::DrawElementsUShort(GL_TRIANGLES, 6, &indices[0]));
                geom[i]->setVertexArray(new osg::Vec3Array(4, verts));
                geom[i]->setNormalArray(new osg::Vec3Array(4, normals));
                geom[i]->setColorArray(new osg::Vec4Array(1, colors), osg::Array::BIND_OVERALL);
                geom[i]->setTexCoordArray(0, new osg::Vec2Array(4, uvs));
                geom[i]->setTexCoordArray(3, new osg::Vec3Array(4)); // flexors

                if (textures.size() > 2)
                    ss->setTextureAttribute(0, textures[2], 1); // top albedo
                if (textures.size() > 3)
                    ss->setTextureAttribute(1, textures[3], 1); // top normal
            }
            group->addChild(geom[i]);
        }
        return group;
    };

    getBiomeLayer()->getBiomeManager().setCreateFunction(
        AssetGroup::TREES,
        trees._createImposter);
}

void
VegetationLayerNV::configureGrass()
{
    auto& grass = options().group(AssetGroup::UNDERGROWTH);

    // functor for generating billboard geometry for grass:
    grass._createImposter = [](
        const osg::BoundingBox& bbox,
        std::vector<osg::Texture*>& textures)
    {
        constexpr unsigned vertsPerInstance = 16;
        constexpr unsigned indiciesPerInstance = 54;

        osg::Geometry* out_geom = new osg::Geometry();
        out_geom->setUseVertexBufferObjects(true);
        out_geom->setUseDisplayList(false);

        static const GLushort indices[indiciesPerInstance] = {
            0,1,4, 4,1,5, 1,2,5, 5,2,6, 2,3,6, 6,3,7,
            4,5,8, 8,5,9, 5,6,9, 9,6,10, 6,7,10, 10,7,11,
            8,9,12, 12,9,13, 9,10,13, 13,10,14, 10,11,14, 14,11,15
        };
        out_geom->addPrimitiveSet(new osg::DrawElementsUShort(GL_TRIANGLES, indiciesPerInstance, &indices[0]));

        osg::Vec3Array* verts = new osg::Vec3Array(osg::Array::BIND_PER_VERTEX);
        verts->reserve(vertsPerInstance);
        out_geom->setVertexArray(verts);

        const float th = 1.0f / 3.0f;
        const float x0 = -0.5f;
        for (int z = 0; z < 4; ++z)
        {
            verts->push_back({ x0+0.0f,    th, float(z)*th });
            verts->push_back({ x0+th,    0.0f, float(z)*th });
            verts->push_back({ x0+th+th, 0.0f, float(z)*th });
            verts->push_back({ x0+1.0f,    th, float(z)*th });
        }

        osg::Vec2Array* uvs = new osg::Vec2Array(osg::Array::BIND_PER_VERTEX);
        uvs->reserve(vertsPerInstance);
        out_geom->setTexCoordArray(0, uvs);
        for (int z = 0; z < 4; ++z)
        {
            uvs->push_back({ 0.0f,    float(z)*th });
            uvs->push_back({ th,      float(z)*th });
            uvs->push_back({ th + th, float(z)*th });
            uvs->push_back({ 1.0f,    float(z)*th });
        }

        const osg::Vec4f colors[1] = {
            {1,1,1,1}
        };
        out_geom->setColorArray(new osg::Vec4Array(1, colors), osg::Array::BIND_OVERALL);

        osg::Vec3f up(0, 0, 1);

        osg::Vec3Array* normals = new osg::Vec3Array(osg::Array::BIND_PER_VERTEX, 16);
        out_geom->setNormalArray(normals);

        osg::Vec3Array* flex = new osg::Vec3Array(osg::Array::BIND_PER_VERTEX, 16);
        out_geom->setTexCoordArray(3, flex);

        const osg::Vec3f face_vec(0, -1, 0);
        const float gravity = 0.025;

        for (int i = 0; i < 16; ++i)
        {
            float bend_power = pow(3.0f*(*uvs)[i].y() + 0.8f, 2.0f);
            osg::Vec3f bend_vec = face_vec * gravity * bend_power;
            float bend_len = bend_vec.length();
            if (bend_len > (*verts)[i].z())
                bend_vec = (bend_vec/bend_len) * (*verts)[i].z();

            (*verts)[i] += bend_vec; // initial gravity bend :)
            if (i < 4) {
                (*normals)[i] = up;
                (*flex)[i].set(0, 0, 0); // no flex
            }
            else {
                (*normals)[i] = up + ((*verts)[i] - (*verts)[i % 4]);
                (*normals)[i].normalize();
                (*flex)[i] = ((*verts)[i] - (*verts)[i - 4]);
                (*flex)[i].normalize();
                (*flex)[i] *= (*uvs)[i].y();
            }
        }

        osg::StateSet* ss = out_geom->getOrCreateStateSet();
        if (textures.size() > 0)
            ss->setTextureAttribute(0, textures[0], 1);
        if (textures.size() > 1)
            ss->setTextureAttribute(1, textures[1], 1);

        return out_geom;
    };

    getBiomeLayer()->getBiomeManager().setCreateFunction(
        AssetGroup::UNDERGROWTH,
        grass._createImposter);
}

unsigned
VegetationLayerNV::getNumTilesRendered() const
{
    return _lastTileBatchSize;
}

void
VegetationLayerNV::checkForNewAssets()
{
    OE_SOFT_ASSERT_AND_RETURN(getBiomeLayer() != nullptr, void());

    BiomeManager& biomeMan = getBiomeLayer()->getBiomeManager();

    // if the revision has not changed, bail out.
    if (_biomeRevision.exchange(biomeMan.getRevision()) == biomeMan.getRevision())
        return;

    // revision changed -- start loading new assets.

    osg::observer_ptr<VegetationLayerNV> layer_weakptr(this);

    auto load = [layer_weakptr](Cancelable* c) -> Assets
    {
        Assets result;
        result.resize(NUM_ASSET_GROUPS);

        osg::ref_ptr< VegetationLayerNV> layer;
        if (layer_weakptr.lock(layer))
        {
            ChonkFactory factory(layer->_textures.get());

            BiomeManager::ResidentBiomes biomes = layer->getBiomeLayer()->getBiomeManager().getResidentBiomes(
                factory,
                layer->getReadOptions());

            // re-organize the data into a form we can readily use.
            for (auto iter : biomes)
            {
                const Biome* biome = iter.first;
                auto& instances = iter.second;

                for (int group = 0; group < NUM_ASSET_GROUPS; ++group)
                {
                    float total_weight = 0.0f;
                    float smallest_weight = FLT_MAX;

                    for (auto& instance : instances[group])
                    {
                        total_weight += instance._weight;
                        smallest_weight = std::min(smallest_weight, instance._weight);
                    }

                    // calculate the weight multiplier
                    float weight_scale = 1.0f / smallest_weight;
                    total_weight *= weight_scale;

                    for (auto& instance : instances[group])
                    {
                        unsigned num = std::max(1u, (unsigned)(instance._weight * weight_scale));
                        for(unsigned i=0; i<num; ++i)
                            result[group][biome].push_back(instance);
                    }
                }
            }
        }
        return result;
    };
    _newAssets = Job().dispatch<Assets>(load);
}

//........................................................................

void
VegetationLayerNV::reset()
{
    _lastVisit.setReferenceTime(DBL_MAX);
    _lastVisit.setFrameNumber(~0U);
    _lastTileBatchSize = 0u;
    _cameraState.clear();

    OE_SOFT_ASSERT_AND_RETURN(getBiomeLayer(), void());

    BiomeManager& biomeMan = getBiomeLayer()->getBiomeManager();
    _biomeRevision = biomeMan.getRevision();
}

ChonkDrawable*
VegetationLayerNV::createDrawable(
    const TileKey& key,
    const AssetGroup::Type& group,
    const osg::BoundingBox& tile_bbox) const
{
#define N_SMOOTH   0
#define N_RANDOM   1
#define N_RANDOM_2 2
#define N_CLUMPY   3

    //TODO:
    // - port stuff from old CS shader:
    // - use a noise texture instead of RNG
    // - use "asset fill" parameter
    // - use "asset size variation" parameter
    // - biome selection
    // - lush variation
    // - fill threshold
    // - think about using BLEND2D to rasterize a collision map!
    //   - would need to be in a background thread I'm sure
    // - caching
    // etc.

    OE_PROFILING_ZONE;

    auto& groupAssets = _assets[group];
    if (groupAssets.empty())
        return nullptr;

    // TEMP
    auto& assetInstances = groupAssets.begin()->second;

    auto d = new ChonkDrawable();

    GeoImage lifemap;
    osg::Matrix lifemap_sb;
    if (getLifeMapLayer())
    {
        TileKey query_key = key.createAncestorKey(14u);
        //TODO:
        // This causes a frame stall. Options...
        // - Bakground job?
        // - only use if already in memory?
        // - Wait until if completes to return a drawable? 
        // - Return a placeholder drawable and regen later?
        lifemap = getLifeMapLayer()->createImage(query_key);
        key.getExtent().createScaleBias(query_key.getExtent(), lifemap_sb);
    }

    osg::Vec4f noise;
    ImageUtils::PixelReader readNoise(_noiseTex->getImage(0));
    readNoise.setSampleAsRepeatingTexture(true);
    
    double tile_width = (tile_bbox.xMax() - tile_bbox.xMin());
    double tile_height = (tile_bbox.yMax() - tile_bbox.yMin());

    constexpr int max_instances = 4096;

    using Index = RTree<double, double, 2>;
    Index index;

    std::default_random_engine gen(key.hash());
    std::uniform_int_distribution<unsigned> rand_uint(0, assetInstances.size()-1);
    std::uniform_real_distribution<float> rand_float(0.0f, 1.0f);

    std::vector<osg::Vec3d> map_points;
    std::vector<osg::Vec3f> local_points;
    std::vector<osg::Vec2f> uvs;
    std::vector<osg::Vec3f> scales;
    std::vector<ResidentModelAsset::Ptr> assets;

    map_points.reserve(max_instances);
    local_points.reserve(max_instances);
    uvs.reserve(max_instances);
    scales.reserve(max_instances);
    assets.reserve(max_instances);

    const GeoExtent& e = key.getExtent();

    osg::Matrixf xform;
    osg::Vec4f lifemap_value;
    double local[2];
    std::vector<double> hits;
    const double s2inv = 1.0 / sqrt(2.0);
    osg::BoundingBox box;

    for (int i = 0; i < max_instances; ++i)
    {
        auto& instance = assetInstances[rand_uint(gen)];
        auto& asset = instance._residentAsset;

        if (asset->_chonk == nullptr)
            continue;

        osg::Vec3f scale(1, 1, 1);
        box = asset->_chonk->getBound();
        
        bool isGrass = asset->_assetDef->width().isSet();
        if (isGrass)
        {
            scale.set(
                asset->_assetDef->width().get(),
                asset->_assetDef->width().get(),
                asset->_assetDef->height().get());
        }

        if (asset->_assetDef->sizeVariation().isSet())
        {
            scale *= 1.0 + (asset->_assetDef->sizeVariation().get() *
                (noise[N_RANDOM_2] * 2.0f - 1.0f));
        }

        // allow overlap for "billboard" models (i.e. grasses).
        bool allow_overlap = (!asset->_assetDef->modelURI().isSet());

        float u = rand_float(gen);
        float v = rand_float(gen);

        // sample the noise texture at this u,v:
        readNoise(noise, u, v);

        float density = 1.0f;
        float lush = 1.0f;
        if (lifemap.valid())
        {
            float uu = u * lifemap_sb(0, 0) + lifemap_sb(3, 0);
            float vv = v * lifemap_sb(1, 1) + lifemap_sb(3, 1);
            lifemap.getReader()(lifemap_value, uu, vv);
            density = lifemap_value[LIFEMAP_DENSE];
            lush = lifemap_value[LIFEMAP_LUSH];
        }
        if (density < 0.01f)
            continue;

        density *= instance._fill;

        if (noise[N_SMOOTH] > density)
            continue;
        else
            noise[N_SMOOTH] /= density;

        if (isGrass)
        {
            float edge_scale = 1.0f;
            const float edge_threshold = 0.5f;
            if (noise[N_SMOOTH] > edge_threshold)
                edge_scale = 1.0f - ((noise[N_SMOOTH] - edge_threshold) / (1.0f - edge_threshold));
            scale *= edge_scale;
        }

        box._min = osg::componentMultiply(box._min, scale);
        box._max = osg::componentMultiply(box._max, scale);

        local[0] = tile_bbox.xMin() + u * tile_width;
        local[1] = tile_bbox.yMin() + v * tile_height;

        bool pass = true;

        if (!allow_overlap)
        {
            pass = false;

            float radius = 0.5f*(box.xMax() - box.xMin());

            // adjust collision radius based on density:
            float search_radius = mix(radius*3.0f, radius*0.25f, density);

            if (index.KNNSearch(local, &hits, nullptr, 1, search_radius) == 0)
            {
                double r = radius * s2inv;
                double a_min[2] = { local[0] - r, local[1] - r };
                double a_max[2] = { local[0] + r, local[1] + r };
                index.Insert(a_min, a_max, radius);
                pass = true;
            }
        }

        if (pass)
        {
            float rotation = rand_float(gen) * 3.1415927 * 2.0;

            assets.emplace_back(asset);
            local_points.emplace_back(local[0], local[1], rotation);
            map_points.emplace_back(e.xMin() + u * e.width(), e.yMin() + v * e.height(), 0);
            uvs.emplace_back(u, v);
            scales.push_back(scale);
        }
    }

    // clamp them to the terrain
    _map->getElevationPool()->sampleMapCoords(
        map_points,
        Distance(),
        nullptr,
        nullptr);

    const osg::Vec3f ZAXIS(0, 0, 1);

    for (unsigned i = 0; i < map_points.size(); ++i)
    {
        xform.makeTranslate(local_points[i].x(), local_points[i].y(), map_points[i].z());
        xform.preMultRotate(osg::Quat(local_points[i].z(), ZAXIS));
        xform.preMultScale(scales[i]);

        d->add(assets[i]->_chonk, xform, uvs[i]);
    }

    return d;
}

void
VegetationLayerNV::cull(
    const TileBatch& batch,
    osg::NodeVisitor& nv) const
{
    if (_assets.empty())
        return;

    osgUtil::CullVisitor* cv = static_cast<osgUtil::CullVisitor*>(&nv);
    
    // todo: mutex
    _lastVisit = *cv->getFrameStamp();

    CameraState& cs = _cameraState[cv->getCurrentCamera()];

    CameraState::TileCache new_tiles;
    ChonkDrawable::Vector tile_drawables;

    for (auto& batch_entry : batch.tiles())
    {
        const TileKey& key = batch_entry->getKey();
        Tile& tile = cs._tiles[key];

        AssetGroup::Type group = getGroupAtLOD(key.getLOD());
        OE_HARD_ASSERT(group != AssetGroup::UNDEFINED);

        // create if necessary:
        if (tile._drawable.valid() == false ||
            tile._revision != batch_entry->getRevision())
        {
            tile._drawable = createDrawable(
                key,
                group,
                batch_entry->getBBox());

            tile._revision = batch_entry->getRevision();
        }

        if (tile._drawable.valid())
        {
            // update the MVM for this frame:
            tile._drawable->setModelViewMatrix(batch_entry->getModelViewMatrix());

            new_tiles[key] = tile;

            tile_drawables.push_back(tile._drawable.get());
        }
    }

    if (!tile_drawables.empty())
    {
        // intiailize the super-drawable:
        if (cs._superDrawable == nullptr)
        {
            cs._superDrawable = new ChonkDrawable();
            cs._superDrawable->setName("VegetationNV");
            cs._superDrawable->setDrawStateSet(getStateSet());
        }

        // assign the new set of tiles:
        cs._superDrawable->setChildren(tile_drawables);

        // finally, traverse it so OSG will draw it.
        cs._superDrawable->accept(nv);
    }

    // Purge old tiles.
    cs._tiles.swap(new_tiles);
}

void
VegetationLayerNV::resizeGLObjectBuffers(unsigned maxSize)
{
    //todo
    PatchLayer::resizeGLObjectBuffers(maxSize);
}

void
VegetationLayerNV::releaseGLObjects(osg::State* state) const
{
    //todo
    PatchLayer::releaseGLObjects(state);
}