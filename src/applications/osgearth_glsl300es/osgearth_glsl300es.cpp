/* -*-c++-*- */
/* osgEarth - Geospatial SDK for OpenSceneGraph
* Copyright 2018 Pelican Mapping
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

/**
 * This is a set of unit tests for osgEarth's shader composition framework.
 *
 * By default, osgEarth uses GL shaders to render the terrain. Shader composition is a
 * mechanism by which you can inject custom shader code into osgEarth's shader program
 * pipeline. This gets around the problem of having to duplicate shader code in order 
 * to add functionality.
 */
#include <osg/Notify>
#include <osg/CullFace>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgGA/StateSetManipulator>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osgEarth/EarthManipulator>
#include <osgEarth/VirtualProgram>
#include <osgEarth/Registry>
#include <osgEarth/Capabilities>
#include <osgEarth/ShaderUtils>
#include <osgEarth/FileUtils>
#include <osgEarth/GLUtils>
#include <osgEarth/Controls>

#include "egl_gw.h"

using namespace osgEarth;
using namespace osgEarth::Util::Controls;


//Utility:
osg::Geode* makeGeom(float v)
{
	osg::Geode* geode = new osg::Geode();
	osg::Geometry* geom = new osg::Geometry();
	osg::Vec3Array* verts = new osg::Vec3Array();
	verts->push_back(osg::Vec3(v - 1, 0, 0));
	verts->push_back(osg::Vec3(v + 1, 0, 0));
	verts->push_back(osg::Vec3(v, 0, 2));
	geom->setVertexArray(verts);
	geom->setUseDisplayList(false);
	geom->setUseVertexBufferObjects(true);
	osg::Vec4Array* colors = new osg::Vec4Array(osg::Array::BIND_OVERALL);
	colors->push_back(osg::Vec4(0, 0, 1, 1));
	geom->setColorArray(colors);
	geom->addPrimitiveSet(new osg::DrawArrays(GL_TRIANGLES, 0, 3));
	geode->addDrawable(geom);
	return geode;
}


int usage( const std::string& msg )
{    

    return -1;
}

namespace TEST_1
{
    osg::Node* run()
    {
		return 0L;
    }
}

namespace TEST_2
{
	osg::Node* run()
	{
		osg::Geode* geode = makeGeom(1.0);
		osg::StateSet* set = geode->getOrCreateStateSet();

		static const char* vertSource =
			"#version " GLSL_VERSION_STR "\n"
			GLSL_DEFAULT_PRECISION_FLOAT "\n"
			"void oe_test(inout vec4 vertexClip)\n"
			"{\n"
			"    ;\n"
			"}\n";

		static const char* fragSource =
			"#version " GLSL_VERSION_STR "\n"
			GLSL_DEFAULT_PRECISION_FLOAT "\n"
			"layout(location=0) out vec4 gcolor; \n"
			"void oe_test_fragment(inout vec4 color)\n"
			"{\n"
			"    gcolor = vec4(1.0,0.0,0.0,1.0); \n"
			"}\n";

		VirtualProgram* vp = VirtualProgram::getOrCreate(set);
		vp->setFunction("oe_test", vertSource, ShaderComp::LOCATION_VERTEX_CLIP);
		vp->setFunction("oe_test_fragment", fragSource, ShaderComp::LOCATION_FRAGMENT_OUTPUT);

		set->setAttributeAndModes(vp, osg::StateAttribute::ON | osg::StateAttribute::PROTECTED);

		return geode;
	}
}

//-------------------------------------------------------------------------

int main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc,argv);
    osgViewer::Viewer viewer(arguments);
	//viewer.getCamera()->setGraphicsContext(new EGLGraphicsWindowEmbedded);
	//viewer.getCamera()->setViewport(new osg::Viewport(0, 0, 200, 200));

	osg::Group* root = new osg::Group();
	viewer.setSceneData(root);

    bool test1 = arguments.read("--test1");
	bool test2 = arguments.read("--test2");
	bool ok    = test1 || test2 ;

    if ( !ok )
    {
        return usage("");
    }


    if ( test1 )
    {
		root->addChild(TEST_1::run());
    }
	else if (test2)
	{
		root->addChild(TEST_2::run());
	}

	viewer.addEventHandler(new osgViewer::StatsHandler());
	viewer.addEventHandler(new osgViewer::WindowSizeHandler());
	viewer.addEventHandler(new osgViewer::ThreadingHandler());
	viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));
	viewer.setThreadingModel(viewer.SingleThreaded);
	
	viewer.realize();
	while(!viewer.done())
	{
		viewer.advance();
		viewer.eventTraversal();
		viewer.updateTraversal();
		viewer.renderingTraversals();
	}

}
