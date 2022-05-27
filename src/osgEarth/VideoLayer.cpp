/* -*-c++-*- */
/* osgEarth - Geospatial SDK for OpenSceneGraph
* Copyright 2020 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#include <osgEarth/VideoLayer>
#include <osg/ImageStream>
#include <osgEarth/Registry>
#include <osgEarth/LayerRegistration>

using namespace osgEarth;

//.......................................................................

Config
VideoLayer::Options::getConfig() const
{
    Config conf = ImageLayer::Options::getConfig();
    conf.set("url", _url);
    return conf;
}

void
VideoLayer::Options::fromConfig( const Config& conf )
{
    conf.get("url", _url );
}

//-------------------------------------------------------------

REGISTER_OSGEARTH_LAYER(video, VideoLayer);

OE_LAYER_PROPERTY_IMPL(VideoLayer, URI, URL, url);

void
VideoLayer::init()
{
    ImageLayer::init();
    
    // Configure the layer to use createTexture() to return data
    setUseCreateTexture();
}

Status
VideoLayer::openImplementation()
{
    if (!isOpen())
    {
        Status parent = ImageLayer::openImplementation();
        if (parent.isError())
            return parent;

        if (!options().url().isSet())
        {
            return Status(Status::ConfigurationError, "Missing required url");
        }

        osg::ref_ptr< osg::Image > image = options().url()->readImage().getImage();
        if (image.valid())
        {             
            osg::ImageStream* is = dynamic_cast< osg::ImageStream*>( image.get() );
            if (is)
            {
                is->setLoopingMode(osg::ImageStream::LOOPING);
                is->play();                 
            }

            _texture = new osg::Texture2D( image );
            _texture->setResizeNonPowerOfTwoHint( false );
            _texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture2D::LINEAR);
            _texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture2D::LINEAR);
            _texture->setWrap( osg::Texture::WRAP_S, osg::Texture::REPEAT );
            _texture->setWrap( osg::Texture::WRAP_T, osg::Texture::REPEAT );
            _texture->setUnRefImageDataAfterApply(false);
        }
        else
        {
            std::stringstream buf;
            buf << "Failed to load " << options().url()->full();
            return Status(Status::ServiceUnavailable, buf.str());
        }

        setProfile(Profile::create(Profile::GLOBAL_GEODETIC));
    }

    return getStatus();
}

TextureWindow
VideoLayer::createTexture(const TileKey& key, ProgressCallback* progress) const
{   
    osg::Matrix textureMatrix;
    bool flip = _texture->getImage()->getOrigin() == osg::Image::TOP_LEFT;    
    key.getExtent().createScaleBias(key.getProfile()->getExtent(), textureMatrix);
    if (flip)
    {
        textureMatrix *= osg::Matrixf::scale(1.0, flip ? -1.0 : 1.0, 1.0);
    }  
    return TextureWindow(_texture.get(), textureMatrix);
}