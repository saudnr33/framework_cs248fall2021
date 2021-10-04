#include "webgpu.h"
#include <string.h>
#include <cmath>
#include <vector>
#include <iostream>
using namespace std;

#include "ray_tracing/Vec3.h"
#include "ray_tracing/Matrix4.h"
#include "ray_tracing/Light.h"
#include "ray_tracing/Color.h"
#include "ray_tracing/Ray.h"
#include "ray_tracing/OrthographicCamera.h"
#include "ray_tracing/PerspectiveCamera.h"
#include "ray_tracing/Sphere.h"
#include "ray_tracing/Plane.h"
#include "ray_tracing/Ellipsoid.h"
//#include "ray_tracing/Cone.h"
//#include "ray_tracing/Cylinder.h"


//---------- Camera -----------------------------------------------
Vec3 viewPos(0.0f, 0.0f, 1.0f);
Vec3 viewDir(0.0f, 0.0f, -1.0f);
Vec3 viewUp(0.0f, 1.0f, 0.0f);
OrthographicCamera orthoCam(viewPos, viewDir, viewUp);
PerspectiveCamera perspectiveCam(viewPos, viewDir, viewUp);

const int viewWidth = 640, viewHeight = 480;
const double viewLeft = -2.0f, viewRight = 2.0f, viewBottom = -1.5f, viewTop = 1.5f;
const double INF = std::numeric_limits<double>::infinity();

//----------- Light ------------------------------------------------
double ka = 0.2, kd = 0.6, ks = 0.2, Phongexponent = 250;
vector<Light*> lights;
Light light1(Vec3(0.0f, 5.0f, 0.0f), Color(1.0f));
Light light2(Vec3(5.0f, 5.0f, 0.0f), Color(1.0f));


//------------ Object ----------------------------------------------
vector<GeometricObject*> gObjects;
Sphere sphere1(Vec3(0.0f, 0.0f, -1.0f), 0.5f, Color(1.0f, 0.0f, 0.0f));
Plane plane1(0.0f, 1.0f, 0.0f, 1.4f, Color(0.5f, 0.5f, 0.5f));
Ellipsoid ellipsoid1(Vec3(3.25f, 0.0f, -2.0f), 1.0f, 1.0f, 1.0f, Color(0.0f, 1.0f, 0.0f));
//Cone cone1(Vec3(0.25f, 0.1f, -0.25f), 0.25f, 0.5f, Color(0.0f, 0.0f, 1.0f));
//Cylinder cylinder1(Vec3(0.0f, 0.5f, -0.1f), 0.25f, 0.5f, Color(1.0f, 0.0f, 1.0f));

//----------- Projection method --------------------------------------
char projMethod = 'P';
void rayTracingPerspective(PerspectiveCamera, vector<Light*>, double, double, double, double, vector<GeometricObject*>, unsigned char*);
void rayTracingOrthographic(OrthographicCamera, vector<Light*>, double, double, double, double, vector<GeometricObject*>, unsigned char*);

//----------- Result image ---------------------------------------
unsigned char* img = new unsigned char[viewWidth * viewHeight * 4];

//----------- Function for update scene after transformation ---------
void updateScene();

//----------- Function for handling user interaction -----------------
void mouseClickHandler(int, int, int, int);
void keyPressHandler(int, int);

//----------- WEBGPU variables ---------------------------------------

WGPUDevice device;
WGPUQueue queue;
WGPUSwapChain swapchain;

WGPURenderPipeline pipeline;
WGPUBuffer vertBuf; // vertex buffer with triangle position and colours
WGPUBuffer indxBuf; // index buffer
WGPUBuffer uRotBuf; // uniform buffer (containing the rotation angle)
WGPUBindGroup bindGroup;


WGPUTexture tex; // Texture
WGPUSampler samplerTex;
WGPUTextureView texView;
WGPUExtent3D texSize = {};
WGPUTextureDescriptor texDesc = {};
WGPUTextureDataLayout texDataLayout = {};
WGPUImageCopyTexture texCopy = {};


/**
 * Current rotation angle (in degrees, updated per frame).
 */
float rotDeg = 0.0f;

/**
 * WGSL equivalent of \c triangle_vert_spirv.
 */
static char const triangle_vert_wgsl[] = R"(
	let PI : f32 = 3.141592653589793;
	fn radians(degs : f32) -> f32 {
		return (degs * PI) / 180.0;
	}
	[[block]]
	struct VertexIn {
		[[location(0)]] aPos : vec2<f32>;
		[[location(1)]] aTex : vec2<f32>;
	};
	struct VertexOut {
		[[location(0)]] vTex : vec2<f32>;
		[[builtin(position)]] Position : vec4<f32>;
	};
	[[block]]
	struct Rotation {
		[[location(0)]] degs : f32;
	};
	[[group(0), binding(0)]] var<uniform> uRot : Rotation;
	[[stage(vertex)]]
	fn main(input : VertexIn) -> VertexOut {
		var rads : f32 = radians(uRot.degs);
		var cosA : f32 = cos(rads);
		var sinA : f32 = sin(rads);
		var rot : mat3x3<f32> = mat3x3<f32>(
			vec3<f32>( cosA, sinA, 0.0),
			vec3<f32>(-sinA, cosA, 0.0),
			vec3<f32>( 0.0,  0.0,  1.0));
		var output : VertexOut;
		output.Position = vec4<f32>(rot * vec3<f32>(input.aPos, 1.0), 1.0);
		output.vTex = input.aTex;
		return output;
	}
)";


/**
 * WGSL equivalent of \c triangle_frag_spirv.
 */
static char const triangle_frag_wgsl[] = R"(
	[[group(0), binding(1)]]
	var tex: texture_2d<f32>;
	[[group(0), binding(2)]]
	var sam: sampler;

	[[stage(fragment)]]
	fn main([[location(0)]] vTex : vec2<f32>) -> [[location(0)]] vec4<f32> {
		return textureSample(tex, sam, vTex);
	}
)";

/**
 * Helper to create a shader from WGSL source.
 *
 * \param[in] code WGSL shader source
 * \param[in] label optional shader name
 */
static WGPUShaderModule createShader(const char* const code, const char* label = nullptr) {
	WGPUShaderModuleWGSLDescriptor wgsl = {};
	wgsl.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
	wgsl.source = code;
	WGPUShaderModuleDescriptor desc = {};
	desc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgsl);
	desc.label = label;
	return wgpuDeviceCreateShaderModule(device, &desc);
}

/**
 * Helper to create a buffer.
 *
 * \param[in] data pointer to the start of the raw data
 * \param[in] size number of bytes in \a data
 * \param[in] usage type of buffer
 */
static WGPUBuffer createBuffer(const void* data, size_t size, WGPUBufferUsage usage) {
	WGPUBufferDescriptor desc = {};
	desc.usage = WGPUBufferUsage_CopyDst | usage;
	desc.size  = size;
	WGPUBuffer buffer = wgpuDeviceCreateBuffer(device, &desc);
	wgpuQueueWriteBuffer(queue, buffer, 0, data, size);
	return buffer;
}


static WGPUTexture createTexture(unsigned char* data, unsigned int w, unsigned int h) {


	texSize.depthOrArrayLayers = 1;
	texSize.height = h;
	texSize.width = w;


	texDesc.sampleCount = 1;
	texDesc.mipLevelCount = 1;
	texDesc.dimension = WGPUTextureDimension_2D;
	texDesc.size = texSize;
	texDesc.usage = WGPUTextureUsage_Sampled | WGPUTextureUsage_CopyDst;
	texDesc.format = WGPUTextureFormat_RGBA8Unorm;


	texDataLayout.offset = 0;
	texDataLayout.bytesPerRow = 4 * w;
	texDataLayout.rowsPerImage = h;


	texCopy.texture = wgpuDeviceCreateTexture(device, &texDesc);

	wgpuQueueWriteTexture(queue, &texCopy, data, w * h * 4, &texDataLayout, &texSize);
	return texCopy.texture;
}

/**
 * Bare minimum pipeline to draw a triangle using the above shaders.
 */
static void createPipelineAndBuffers(unsigned char* data, unsigned int w, unsigned int h) {
	// compile shaders
	// NOTE: these are now the WGSL shaders (tested with Dawn and Chrome Canary)
	WGPUShaderModule vertMod = createShader(triangle_vert_wgsl);
	WGPUShaderModule fragMod = createShader(triangle_frag_wgsl);
	
	WGPUBufferBindingLayout buf = {};
	buf.type = WGPUBufferBindingType_Uniform;

	// bind group layout (used by both the pipeline layout and uniform bind group, released at the end of this function)
	WGPUBindGroupLayoutEntry bglEntry = {};
	bglEntry.binding = 0;
	bglEntry.visibility = WGPUShaderStage_Vertex;
	bglEntry.buffer = buf;
	bglEntry.sampler = { 0 };

	//===================================================================

	tex = createTexture(data, w, h);

	WGPUTextureViewDescriptor texViewDesc = {};
	texViewDesc.dimension = WGPUTextureViewDimension_2D;
	texViewDesc.format = WGPUTextureFormat_RGBA8Unorm;

	texView = wgpuTextureCreateView(tex, &texViewDesc);


	WGPUSamplerDescriptor samplerDesc = {};
	samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
	samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
	samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
	samplerDesc.magFilter = WGPUFilterMode_Linear;
	samplerDesc.minFilter = WGPUFilterMode_Nearest;
	samplerDesc.mipmapFilter = WGPUFilterMode_Nearest;
	samplerDesc.lodMaxClamp = 32;
	samplerDesc.lodMinClamp = 0;
	samplerDesc.compare = WGPUCompareFunction_Undefined;
	samplerDesc.maxAnisotropy = 1;
	
	samplerTex = wgpuDeviceCreateSampler(device, &samplerDesc);

	WGPUSamplerBindingLayout samplerLayout = {};
	samplerLayout.type = WGPUSamplerBindingType_Filtering;

	WGPUTextureBindingLayout texLayout = {};
	texLayout.sampleType = WGPUTextureSampleType_Float;
	texLayout.viewDimension = WGPUTextureViewDimension_2D;
	texLayout.multisampled = false;

	WGPUBindGroupLayoutEntry bglTexEntry = {};
	bglTexEntry.binding = 1;
	bglTexEntry.visibility = WGPUShaderStage_Fragment;
	bglTexEntry.texture = texLayout;

	WGPUBindGroupLayoutEntry bglSamplerEntry = {};
	bglSamplerEntry.binding = 2;
	bglSamplerEntry.visibility = WGPUShaderStage_Fragment;
	bglSamplerEntry.sampler = samplerLayout;

	WGPUBindGroupLayoutEntry* allBgLayoutEntries = new WGPUBindGroupLayoutEntry[3];
	allBgLayoutEntries[0] = bglEntry;
	allBgLayoutEntries[1] = bglTexEntry;
	allBgLayoutEntries[2] = bglSamplerEntry;

	//=======================================================================

	WGPUBindGroupLayoutDescriptor bglDesc = {};
	bglDesc.entryCount = 3;
	bglDesc.entries = allBgLayoutEntries;
	WGPUBindGroupLayout bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

	// pipeline layout (used by the render pipeline, released after its creation)
	WGPUPipelineLayoutDescriptor layoutDesc = {};
	layoutDesc.bindGroupLayoutCount = 1;
	layoutDesc.bindGroupLayouts = &bindGroupLayout;
	WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

	// describe buffer layouts
	WGPUVertexAttribute vertAttrs[2] = {};
	vertAttrs[0].format = WGPUVertexFormat_Float32x2;
	vertAttrs[0].offset = 0;
	vertAttrs[0].shaderLocation = 0;
	vertAttrs[1].format = WGPUVertexFormat_Float32x2;
	vertAttrs[1].offset = 2 * sizeof(float);
	vertAttrs[1].shaderLocation = 1;
	WGPUVertexBufferLayout vertexBufferLayout = {};
	vertexBufferLayout.arrayStride = 4 * sizeof(float);
	vertexBufferLayout.attributeCount = 2;
	vertexBufferLayout.attributes = vertAttrs;

	// Fragment state
	WGPUBlendState blend = {};
	blend.color.operation = WGPUBlendOperation_Add;
	blend.color.srcFactor = WGPUBlendFactor_One;
	blend.color.dstFactor = WGPUBlendFactor_One;
	blend.alpha.operation = WGPUBlendOperation_Add;
	blend.alpha.srcFactor = WGPUBlendFactor_One;
	blend.alpha.dstFactor = WGPUBlendFactor_One;

	WGPUColorTargetState colorTarget = {};
	colorTarget.format = webgpu::getSwapChainFormat(device);
	colorTarget.blend = &blend;
	colorTarget.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState fragment = {};
	fragment.module = fragMod;
	fragment.entryPoint = "main";
	fragment.targetCount = 1;
	fragment.targets = &colorTarget;

#ifdef __EMSCRIPTEN__
	WGPURenderPipelineDescriptor desc = {};
#else
	WGPURenderPipelineDescriptor desc = {};
#endif
	desc.fragment = &fragment;

	// Other state
	desc.layout = pipelineLayout;
	desc.depthStencil = nullptr;

	desc.vertex.module = vertMod;
	desc.vertex.entryPoint = "main";
	desc.vertex.bufferCount = 1;//0;
	desc.vertex.buffers = &vertexBufferLayout;

	desc.multisample.count = 1;
	desc.multisample.mask = 0xFFFFFFFF;
	desc.multisample.alphaToCoverageEnabled = false;

	desc.primitive.frontFace = WGPUFrontFace_CCW;
	desc.primitive.cullMode = WGPUCullMode_None;
	desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	desc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;

#ifdef __EMSCRIPTEN__
	pipeline = wgpuDeviceCreateRenderPipeline (device, &desc);
#else
	pipeline = wgpuDeviceCreateRenderPipeline (device, &desc);
#endif

	// partial clean-up (just move to the end, no?)
	wgpuPipelineLayoutRelease(pipelineLayout);

	wgpuShaderModuleRelease(fragMod);
	wgpuShaderModuleRelease(vertMod);
	
	// create the buffers (position[2], tex_coords[2])
	float const vertData[] = {
		-1.0f, -1.0f, 0.0f, 0.0f, 
		 1.0f, -1.0f, 1.0f, 0.0f, 
		-1.0f,  1.0f, 0.0f, 1.0f, 
		 1.0f,  1.0f, 1.0f, 1.0f, 
	};
	
	// indices buffer
	uint16_t const indxData[] = {
		0, 1, 2,
		1, 3, 2, 
	};

	vertBuf = createBuffer(vertData, sizeof(vertData), WGPUBufferUsage_Vertex);
	indxBuf = createBuffer(indxData, sizeof(indxData), WGPUBufferUsage_Index);

	// create the uniform bind group (note 'rotDeg' is copied here, not bound in any way)
	uRotBuf = createBuffer(&rotDeg, sizeof(rotDeg), WGPUBufferUsage_Uniform);

	WGPUBindGroupEntry bgEntry = {};
	bgEntry.binding = 0;
	bgEntry.buffer = uRotBuf;
	bgEntry.offset = 0;
	bgEntry.size = sizeof(rotDeg);

	WGPUBindGroupEntry bgTexEntry = {};
	bgTexEntry.binding = 1;
	bgTexEntry.textureView = texView;

	WGPUBindGroupEntry bgSamplerEntry = {};
	bgSamplerEntry.binding = 2;
	bgSamplerEntry.sampler = samplerTex;


	WGPUBindGroupEntry* allBgEntries = new WGPUBindGroupEntry[3];
	allBgEntries[0] = bgEntry;
	allBgEntries[1] = bgTexEntry;
	allBgEntries[2] = bgSamplerEntry;

	WGPUBindGroupDescriptor bgDesc = {};
	bgDesc.layout = bindGroupLayout;
	bgDesc.entryCount = 3;
	bgDesc.entries = allBgEntries;


	bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);

	// last bit of clean-up
	wgpuBindGroupLayoutRelease(bindGroupLayout);
}

/**
 * Draws using the above pipeline and buffers.
 */
static bool redraw() {
	WGPUTextureView backBufView = wgpuSwapChainGetCurrentTextureView(swapchain);			// create textureView

	WGPURenderPassColorAttachment colorDesc = {};
	colorDesc.view    = backBufView;
	colorDesc.loadOp  = WGPULoadOp_Clear;
	colorDesc.storeOp = WGPUStoreOp_Store;
	colorDesc.clearColor.r = 0.0f;
	colorDesc.clearColor.g = 0.0f;
	colorDesc.clearColor.b = 0.0f;
	colorDesc.clearColor.a = 1.0f;

	WGPURenderPassDescriptor renderPass = {};
	renderPass.colorAttachmentCount = 1;
	renderPass.colorAttachments = &colorDesc;

	WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);			// create encoder
	WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPass);	// create pass

	// update texture before draw
	updateScene();
	wgpuQueueWriteTexture(queue, &texCopy, img, viewWidth * viewHeight * 4, &texDataLayout, &texSize);
	// end update texture

	// update the rotation
	/*rotDeg += 0.1f;
	wgpuQueueWriteBuffer(queue, uRotBuf, 0, &rotDeg, sizeof(rotDeg));*/

	// draw the triangle (comment these five lines to simply clear the screen)
	wgpuRenderPassEncoderSetPipeline(pass, pipeline);
	wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, 0);
	wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertBuf, 0, 0);
	wgpuRenderPassEncoderSetIndexBuffer(pass, indxBuf, WGPUIndexFormat_Uint16, 0, 0);
	wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, 0, 0, 0);

	wgpuRenderPassEncoderEndPass(pass);
	wgpuRenderPassEncoderRelease(pass);														// release pass
	WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);				// create commands
	wgpuCommandEncoderRelease(encoder);														// release encoder

	wgpuQueueSubmit(queue, 1, &commands);
	wgpuCommandBufferRelease(commands);														// release commands

	
#ifndef __EMSCRIPTEN__
	wgpuSwapChainPresent(swapchain);
#endif
	wgpuTextureViewRelease(backBufView);													// release textureView
	
	return true;
}

// #TODO: Using these two functions for tasks in the assignment
/**
 * Mouse handling function.
 */
void mouseClickHandler(int button, int action, int x, int y)
{
	printf("button:%d action:%d x:%d y:%d\n", button, action, x, y);
}

/**
 * Keyboard handling function.
 */
void keyPressHandler(int button, int action)
{
	printf("key:%d action:%d\n", button, action);
}

// #TODO: Using this function to update/draw scene after transformation
void updateScene()
{
	if (projMethod == 'P' || projMethod == 'p')
		rayTracingPerspective(perspectiveCam, lights, ka, kd, ks, Phongexponent, gObjects, img);
	else if (projMethod == 'O' || projMethod == 'o')
		rayTracingOrthographic(orthoCam, lights, ka, kd, ks, Phongexponent, gObjects, img);
}

// #TODO: You can change two functions below if needed
void rayTracingPerspective(PerspectiveCamera camera, vector<Light*> lights, double ka, double kd, double ks, double Phongexponent, vector<GeometricObject*> gObjects, unsigned char* data)
{
	int idx = 0;
	Ray viewRay;
	for (int j = 0; j < viewHeight; j++) { // foreach pixel
		for (int i = 0; i < viewWidth; i++) {

			double ui = (double)viewLeft + (viewRight - viewLeft) * (i + 0.5f) / viewWidth;
			double vj = (double)viewBottom + (viewTop - viewBottom) * (j + 0.5f) / viewHeight;

			bool hit = false;
			double t_near = INF;
			int hitObjectIndex = -1;
			Vec3 hitNorm;
			Vec3 hitPoint;
			Color finalColor;

			camera.getRay(viewRay, ui, vj);

			for (int k = 0; k < gObjects.size(); k++) {
				double t = gObjects[k]->testIntersection(viewRay);
				if (t > 0 && t < t_near) {
					hit = true;
					t_near = t;
					hitObjectIndex = k;
					hitPoint = gObjects[k]->computeIntersectionPoint(viewRay, t);
					hitNorm = gObjects[k]->computeNormalIntersection(viewRay, t);
				}
			}

			if (hit) {

				Color diffuse = Color(0.0f, 0.0f, 0.0f);
				Color specular = Color(0.0f, 0.0f, 0.0f);
				Vec3 n = hitNorm.normalize();

				for (int index = 0; index < lights.size(); index++)
				{
					Vec3 l = (lights[index]->position - hitPoint).normalize();
					bool isShadow = false;
					double epsilon = 1e-6;

					Ray shadowRay(hitPoint, l);

					for (int k = 0; k < gObjects.size(); k++) {
						if (gObjects[k]->testIntersection(shadowRay) > epsilon)
						{
							isShadow = true;
							break;
						}
					}

					if (!isShadow)
					{
						diffuse = diffuse + (lights[index]->intensity * kd * (max(0.0, (n.dotProduct(l)))));
						Vec3 v = viewPos - hitPoint;
						Vec3 h = (v + l).normalize();
						specular = specular + (lights[index]->intensity * ks * pow(max(0.0, (n.dotProduct(h))), Phongexponent));

					}

				}
				// FinalColor
				Color ambient = gObjects[hitObjectIndex]->color * ka;
				finalColor = ambient + diffuse + specular;

				// Clamp color if the color calculated from many lights bigger than 1.
				if (finalColor.red > 1.0 || finalColor.blue > 1.0 || finalColor.green > 1.0)
				{

					if (finalColor.red > 1.0)
						finalColor.red = 1.0;

					if (finalColor.blue > 1.0)
						finalColor.blue = 1.0;

					if (finalColor.green > 1.0)
						finalColor.green = 1.0;
				}

			}

			else {
				Color backgroundColor(0.0f, 0.0f, 0.0f);
				finalColor = backgroundColor;
			}

			idx = ((j * viewWidth) + i) * 4;
			data[idx] = floor(finalColor.red * 255);
			data[idx + 1] = floor(finalColor.green * 255);
			data[idx + 2] = floor(finalColor.blue * 255);
			data[idx + 3] = 255;
		}
	}
}

void rayTracingOrthographic(OrthographicCamera camera, vector<Light*> lights, double ka, double kd, double ks, double Phongexponent, vector<GeometricObject*> gObjects, unsigned char* data)
{
	int idx = 0;
	Ray viewRay;
	for (int j = 0; j < viewHeight; j++) { // foreach pixel
		for (int i = 0; i < viewWidth; i++) {

			double ui = (double)viewLeft + (viewRight - viewLeft) * (i + 0.5f) / viewWidth;
			double vj = (double)viewBottom + (viewTop - viewBottom) * (j + 0.5f) / viewHeight;

			bool hit = false;
			double t_near = INF;
			int hitObjectIndex = -1;
			Vec3 hitNorm;
			Vec3 hitPoint;
			Color finalColor;

			camera.getRay(viewRay, ui, vj);

			for (int k = 0; k < gObjects.size(); k++) {
				double t = gObjects[k]->testIntersection(viewRay);
				if (t > 0 && t < t_near) {
					hit = true;
					t_near = t;
					hitObjectIndex = k;
					hitPoint = gObjects[k]->computeIntersectionPoint(viewRay, t);
					hitNorm = gObjects[k]->computeNormalIntersection(viewRay, t);
				}
			}

			if (hit) {

				Color diffuse = Color(0.0f, 0.0f, 0.0f);
				Color specular = Color(0.0f, 0.0f, 0.0f);
				Vec3 n = hitNorm.normalize();

				for (int index = 0; index < lights.size(); index++)
				{
					Vec3 l = (lights[index]->position - hitPoint).normalize();
					bool isShadow = false;
					double epsilon = 1e-6;

					Ray shadowRay(hitPoint, l);
					for (int k = 0; k < gObjects.size(); k++) {
						if (gObjects[k]->testIntersection(shadowRay) > epsilon)
						{
							isShadow = true;
							break;
						}
					}

					if (!isShadow)
					{
						diffuse = diffuse + (lights[index]->intensity * kd * (max(0.0, (n.dotProduct(l)))));
						Vec3 v = viewPos - hitPoint;
						Vec3 h = (v + l).normalize();
						specular = specular + (lights[index]->intensity * ks * pow(max(0.0, (n.dotProduct(h))), Phongexponent));
					}

				}
				// FinalColor
				Color ambient = gObjects[hitObjectIndex]->color * ka;
				finalColor = ambient + diffuse + specular;

				if (finalColor.red > 1.0 || finalColor.blue > 1.0 || finalColor.green > 1.0)
				{

					if (finalColor.red > 1.0)
						finalColor.red = 1.0;

					if (finalColor.blue > 1.0)
						finalColor.blue = 1.0;

					if (finalColor.green > 1.0)
						finalColor.green = 1.0;
				}

			}

			else {
				Color backgroundColor(0.0f, 0.0f, 0.0f);
				finalColor = backgroundColor;
			}

			idx = ((j * viewWidth) + i) * 4;
			data[idx] = floor(finalColor.red * 255);
			data[idx + 1] = floor(finalColor.green * 255);
			data[idx + 2] = floor(finalColor.blue * 255);
			data[idx + 3] = 255;
		}
	}
}

extern "C" int __main__(int /*argc*/, char* /*argv*/[]) {

	//------------ Set camera frame ----------------------
	orthoCam.setCameraFrame();
	cout << orthoCam;

	perspectiveCam.setCameraFrame();
	cout << perspectiveCam;
	
	
	//----------- Add lights -----------------------------
	lights.push_back(&light1);
	lights.push_back(&light2);

	//----------- Add objects ----------------------------
	gObjects.push_back(&plane1);
	gObjects.push_back(&sphere1);
	gObjects.push_back(&ellipsoid1);
	/*gObjects.push_back(&cone1);
	gObjects.push_back(&cylinder1);*/

	//----------- Choose projection method ---------------
	do {
		cout << "Choose the projection method (O/P): (O = orthographic), (P = perspective):";
		cin >> projMethod;	
	} while (projMethod != 'P' && projMethod != 'p' && projMethod != 'O' && projMethod != 'o');
	
	//----------- Draw windows and update scene ------------
	if (window::Handle wHnd = window::create(viewWidth, viewHeight, "Hello CS248")) {
		if ((device = webgpu::create(wHnd))) {

			queue = wgpuDeviceGetQueue(device);
			swapchain = webgpu::createSwapChain(device);
			createPipelineAndBuffers(img, viewWidth, viewHeight);

			// bind the user interaction
			window::mouseClicked(mouseClickHandler);
			window::keyPressed(keyPressHandler);

			window::show(wHnd);
			window::loop(wHnd, redraw);


#ifndef __EMSCRIPTEN__
			wgpuBindGroupRelease(bindGroup);
			wgpuBufferRelease(uRotBuf);
			wgpuBufferRelease(indxBuf);
			wgpuBufferRelease(vertBuf);
			wgpuSamplerRelease(samplerTex);
			wgpuTextureViewRelease(texView);
			wgpuRenderPipelineRelease(pipeline);
			wgpuSwapChainRelease(swapchain);
			wgpuQueueRelease(queue);
			wgpuDeviceRelease(device);
#endif
		}
#ifndef __EMSCRIPTEN__
		window::destroy(wHnd);
#endif
	}


	return 0;
}
