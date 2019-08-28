#ifndef _EGL_GW_WINDOW_H_
#define _EGL_GW_WINDOW_H_

#include <osgViewer/Viewer>
#include <EGL/egl.h>

#define CASE_STR( value ) case value: return #value; 
const char* eglGetErrorString(EGLint error)
{
	switch(error)
	{
		CASE_STR(EGL_SUCCESS)
			CASE_STR(EGL_NOT_INITIALIZED)
			CASE_STR(EGL_BAD_ACCESS)
			CASE_STR(EGL_BAD_ALLOC)
			CASE_STR(EGL_BAD_ATTRIBUTE)
			CASE_STR(EGL_BAD_CONTEXT)
			CASE_STR(EGL_BAD_CONFIG)
			CASE_STR(EGL_BAD_CURRENT_SURFACE)
			CASE_STR(EGL_BAD_DISPLAY)
			CASE_STR(EGL_BAD_SURFACE)
			CASE_STR(EGL_BAD_MATCH)
			CASE_STR(EGL_BAD_PARAMETER)
			CASE_STR(EGL_BAD_NATIVE_PIXMAP)
			CASE_STR(EGL_BAD_NATIVE_WINDOW)
			CASE_STR(EGL_CONTEXT_LOST)
	default: return "Unknown";
	}
}
#undef CASE_STR

class EGLGraphicsWindowEmbedded : public osgViewer::GraphicsWindowEmbedded
{
protected:
	EGLDisplay  eglDpy;
	EGLint      major, minor;
	EGLint      numConfigs;
	EGLConfig   eglCfg;
	EGLSurface  eglSurf;
	EGLContext  eglCtx;
public:

	EGLGraphicsWindowEmbedded()
	{
		EGLint eglOpenglBit = EGL_OPENGL_ES3_BIT, eglContextClientVersion = 3;
		EGLenum eglApi = EGL_OPENGL_ES_API;

		const EGLint configAttribs[] = {
			  EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
			  EGL_BLUE_SIZE, 8,
			  EGL_GREEN_SIZE, 8,
			  EGL_RED_SIZE, 8,
			  EGL_ALPHA_SIZE, 8,
			  EGL_DEPTH_SIZE, 16,
			  EGL_RENDERABLE_TYPE, eglOpenglBit,
			  EGL_NONE
		};

		// EGL context attributes
		const EGLint ctxAttr[] = {
			EGL_CONTEXT_CLIENT_VERSION, eglContextClientVersion, 
			EGL_NONE
		};

		static const int pbufferWidth = 256;
		static const int pbufferHeight = 256;

		const EGLint pbufferAttribs[] = {
			EGL_WIDTH, pbufferWidth,
			EGL_HEIGHT, pbufferHeight,
	 	    EGL_NONE
		};

		// 1. Initialize EGL
		eglDpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

		eglInitialize(eglDpy, &major, &minor);

		printf("EGL Version: \"%s\"\n", eglQueryString(eglDpy, EGL_VERSION));
		printf("EGL Vendor: \"%s\"\n", eglQueryString(eglDpy, EGL_VENDOR));
		printf("EGL Extensions: \"%s\"\n", eglQueryString(eglDpy, EGL_EXTENSIONS));

		if(!eglChooseConfig(eglDpy, configAttribs, NULL, 0, &numConfigs))
		{
			return;
		}
		// 2. Select an appropriate configuration
		EGLBoolean err = eglChooseConfig(eglDpy, configAttribs, &eglCfg, 1, &numConfigs);
		if(!err) {
			const char* err_str = eglGetErrorString(err);
			std::cout << err_str;
			return;
		}

		// 3. Create a surface
		eglSurf = eglCreatePbufferSurface(eglDpy, eglCfg, pbufferAttribs);
		if(eglSurf == EGL_NO_SURFACE) {
			return;
		}

		// 4. Bind the API
		eglBindAPI(eglApi);

		// 5. Create a context and make it current
		eglCtx = eglCreateContext(eglDpy, eglCfg, EGL_NO_CONTEXT, ctxAttr);

		const char *version_egl;
		version_egl = eglQueryString(eglDpy, EGL_VERSION);

		const char* versionString = (const char*)glGetString(GL_VERSION);
		

		init();

	}
	virtual ~EGLGraphicsWindowEmbedded()
	{
		// 6. Terminate EGL when finished
		eglTerminate(eglDpy);
	}

	virtual bool isSameKindAs(const Object* object) const { return dynamic_cast<const EGLGraphicsWindowEmbedded*>(object) != 0; }
	virtual const char* libraryName() const { return "osgViewer"; }
	virtual const char* className() const { return "EGLGraphicsWindowEmbedded"; }

	void init()
	{
		if(valid())
		{
			setState(new osg::State);
			getState()->setGraphicsContext(this);

			getState()->setContextID(osg::GraphicsContext::createNewContextID());
		}
	}

	// dummy implementations, assume that graphics context is *always* current and valid.
	virtual bool valid() const { return true; }
	virtual bool realizeImplementation() { return true; }
	virtual bool isRealizedImplementation() const { return true; }
	virtual void closeImplementation() {}
	virtual bool makeCurrentImplementation() {
		eglMakeCurrent(eglDpy, eglSurf, eglSurf, eglCtx);
		return true;
	}
	virtual bool releaseContextImplementation() { return true; }
	virtual void swapBuffersImplementation() {}
	virtual void grabFocus() {}
	virtual void grabFocusIfPointerInWindow() {}
	virtual void raiseWindow() {}
};

#endif