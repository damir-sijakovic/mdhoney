//
// NanoVGX gradient example (OpenGL ES 3 via GLFW).
//
//   example_gradients [--png out.png]
//
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define GLFW_INCLUDE_ES3
#define GLFW_INCLUDE_GLEXT
#include <GLFW/glfw3.h>
#include "nanovgx.h"
#define NANOVG_GLES3_IMPLEMENTATION
#include "nanovgx_gl.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static NVGcolorStop stop(float offset, NVGcolor color)
{
	NVGcolorStop s;
	memset(&s, 0, sizeof(s));
	s.offset = offset;
	s.color = color;
	return s;
}

static void label(NVGcontext* vg, float x, float y, const char* text)
{
	nvgFontSize(vg, 15.0f);
	nvgFontFace(vg, "sans");
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFillColor(vg, nvgRGBA(40, 40, 50, 255));
	nvgText(vg, x, y, text, NULL);
}

static void draw(NVGcontext* vg, float t)
{
	NVGcolorStop rainbow[7] = {
		stop(0.0f/6, nvgRGB(255, 0, 0)), stop(1.0f/6, nvgRGB(255, 165, 0)), stop(2.0f/6, nvgRGB(255, 255, 0)),
		stop(3.0f/6, nvgRGB(0, 128, 0)), stop(4.0f/6, nvgRGB(0, 0, 255)), stop(5.0f/6, nvgRGB(75, 0, 130)),
		stop(6.0f/6, nvgRGB(238, 130, 238)),
	};
	NVGcolorStop wheel[5] = {
		stop(0.0f, nvgRGB(224, 49, 49)), stop(0.25f, nvgRGB(240, 140, 0)), stop(0.5f, nvgRGB(47, 158, 68)),
		stop(0.75f, nvgRGB(25, 113, 194)), stop(1.0f, nvgRGB(224, 49, 49)),
	};
	NVGcolorStop sun[3] = {
		stop(0.0f, nvgRGB(255, 224, 102)), stop(0.5f, nvgRGB(247, 103, 7)), stop(1.0f, nvgRGBA(95, 61, 196, 0)),
	};
	NVGcolorStop hinted[2] = { stop(0.0f, nvgRGB(25, 113, 194)), stop(1.0f, nvgRGB(255, 255, 255)) };
	NVGcolorStop stripes[4] = {
		stop(0.0f, nvgRGB(52, 58, 64)), stop(0.5f, nvgRGB(52, 58, 64)),
		stop(0.5f, nvgRGB(255, 212, 59)), stop(1.0f, nvgRGB(255, 212, 59)),
	};
	NVGcolorStop pie[6] = {
		stop(0.0f, nvgRGB(51, 154, 240)), stop(0.35f, nvgRGB(51, 154, 240)),
		stop(0.35f, nvgRGB(81, 207, 102)), stop(0.6f, nvgRGB(81, 207, 102)),
		stop(0.6f, nvgRGB(255, 107, 107)), stop(1.0f, nvgRGB(255, 107, 107)),
	};
	NVGcolorStop many[48];
	NVGcolorStop text[3] = { stop(0.0f, nvgRGB(59, 91, 219)), stop(0.5f, nvgRGB(112, 72, 232)), stop(1.0f, nvgRGB(194, 37, 92)) };
	int i;

	hinted[0].hint = 0.15f;	// 50% mix point at 15%

	for (i = 0; i < 48; i++) {
		// 48 stops: more than NVG_MAX_GRADIENT_STOPS, so this goes through a ramp texture.
		float h = (float)i / 47.0f;
		many[i] = stop(h, nvgHSL(h, 0.8f, (i % 2) ? 0.45f : 0.65f));
	}

	// Row 1: fills
	label(vg, 20, 16, "linear, 7 stops, rotated");
	nvgSave(vg);
	nvgTranslate(vg, 130, 110);
	nvgRotate(vg, t * 0.3f);
	nvgBeginPath(vg);
	nvgRoundedRect(vg, -100, -45, 200, 90, 14);
	nvgFillPaint(vg, nvgLinearGradientStops(vg, -100, 0, 100, 0, rainbow, 7, 0));
	nvgFill(vg);
	nvgRestore(vg);

	label(vg, 270, 16, "radial, elliptical");
	nvgBeginPath(vg);
	nvgRect(vg, 270, 45, 220, 130);
	nvgFillPaint(vg, nvgRadialGradientStops(vg, 340, 90, 160, 100, sun, 3, 0));
	nvgFill(vg);

	label(vg, 520, 16, "conic");
	nvgBeginPath(vg);
	nvgCircle(vg, 590, 110, 65);
	nvgFillPaint(vg, nvgConicGradient(vg, 590, 110, -NVG_PI/2 + t, wheel, 5, 0));
	nvgFill(vg);

	label(vg, 690, 16, "conic, hard stops");
	nvgBeginPath(vg);
	nvgCircle(vg, 760, 110, 65);
	nvgFillPaint(vg, nvgConicGradient(vg, 760, 110, -NVG_PI/2, pie, 6, 0));
	nvgFill(vg);

	// Row 2
	label(vg, 20, 206, "repeat flag (stripes)");
	nvgBeginPath(vg);
	nvgRoundedRect(vg, 20, 235, 220, 120, 10);
	nvgFillPaint(vg, nvgLinearGradientStops(vg, 20, 235, 34, 249, stripes, 4, NVG_GRADIENT_REPEAT));
	nvgFill(vg);

	label(vg, 270, 206, "48 stops (ramp texture)");
	nvgBeginPath(vg);
	nvgRect(vg, 270, 235, 220, 120);
	nvgFillPaint(vg, nvgLinearGradientStops(vg, 270, 0, 490, 0, many, 48, 0));
	nvgFill(vg);

	label(vg, 520, 206, "no hint / hint at 15%");
	hinted[0].hint = 0.0f;
	nvgBeginPath(vg);
	nvgRect(vg, 520, 235, 300, 55);
	nvgFillPaint(vg, nvgLinearGradientStops(vg, 520, 0, 820, 0, hinted, 2, 0));
	nvgFill(vg);
	hinted[0].hint = 0.15f;
	nvgBeginPath(vg);
	nvgRect(vg, 520, 300, 300, 55);
	nvgFillPaint(vg, nvgLinearGradientStops(vg, 520, 0, 820, 0, hinted, 2, 0));
	nvgFill(vg);

	// Row 3: strokes and text
	label(vg, 20, 386, "stroke");
	nvgBeginPath(vg);
	nvgCircle(vg, 130, 490, 70);
	nvgStrokeWidth(vg, 18);
	nvgStrokePaint(vg, nvgConicGradient(vg, 130, 490, t, wheel, 5, 0));
	nvgStroke(vg);

	label(vg, 270, 386, "text + global alpha 0.6");
	nvgSave(vg);
	nvgGlobalAlpha(vg, 0.6f);
	nvgFontSize(vg, 64.0f);
	nvgFontFace(vg, "bold");
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFillPaint(vg, nvgLinearGradientStops(vg, 270, 0, 600, 0, text, 3, 0));
	nvgText(vg, 270, 420, "NanoVGX", NULL);
	nvgRestore(vg);
	nvgFontSize(vg, 64.0f);
	nvgFontFace(vg, "bold");
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
	nvgFillPaint(vg, nvgLinearGradientStops(vg, 270, 0, 600, 0, rainbow, 7, 0));
	nvgText(vg, 270, 490, "Gradients", NULL);
}

int main(int argc, char** argv)
{
	GLFWwindow* window;
	NVGcontext* vg;
	const char* png = NULL;
	int i;

	for (i = 1; i < argc; i++)
		if (strcmp(argv[i], "--png") == 0 && i+1 < argc) png = argv[++i];

	if (!glfwInit()) return 1;
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	window = glfwCreateWindow(860, 600, "NanoVGX gradients", NULL, NULL);
	if (!window) {
		glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
		window = glfwCreateWindow(860, 600, "NanoVGX gradients", NULL, NULL);
	}
	if (!window) { glfwTerminate(); return 1; }
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	vg = nvgCreateGLES3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
	if (vg == NULL) { printf("Could not init nanovgx.\n"); return 1; }
	nvgCreateFont(vg, "sans", EXAMPLE_DIR "/Roboto-Regular.ttf");
	nvgCreateFont(vg, "bold", EXAMPLE_DIR "/Roboto-Bold.ttf");

	while (!glfwWindowShouldClose(window)) {
		int winWidth, winHeight, fbWidth, fbHeight;
		float t = png ? 0.0f : (float)glfwGetTime();
		glfwGetWindowSize(window, &winWidth, &winHeight);
		glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

		glViewport(0, 0, fbWidth, fbHeight);
		glClearColor(0.95f, 0.95f, 0.97f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		nvgBeginFrame(vg, winWidth, winHeight, (float)fbWidth / (float)winWidth);
		draw(vg, t);
		nvgEndFrame(vg);

		if (png) {
			unsigned char* px = (unsigned char*)malloc((size_t)fbWidth * fbHeight * 4);
			glReadPixels(0, 0, fbWidth, fbHeight, GL_RGBA, GL_UNSIGNED_BYTE, px);
			// GL rows are bottom-up
			stbi_write_png(png, fbWidth, fbHeight, 4, px + (size_t)(fbHeight-1) * fbWidth * 4, -fbWidth * 4);
			free(px);
			printf("saved %s\n", png);
			break;
		}

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	nvgDeleteGLES3(vg);
	glfwTerminate();
	return 0;
}
