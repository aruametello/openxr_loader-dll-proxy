#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define LAYER_EXPORT __declspec(dllexport)
#else
#define LAYER_EXPORT
#endif

static PFN_xrGetInstanceProcAddr g_next_get_instance_proc_addr = NULL;
static PFN_xrCreateApiLayerInstance g_next_create_api_layer_instance = NULL;
static PFN_xrLocateSpace g_next_locate_space = NULL;

static char g_app_name[XR_MAX_APPLICATION_NAME_SIZE] = {0};
static uint32_t g_app_version = 0;
static char g_engine_name[XR_MAX_ENGINE_NAME_SIZE] = {0};
static uint32_t g_engine_version = 0;

static unsigned long g_pid = 0;
static char g_exe_path[1024] = {0};
static char g_exe_name[256] = {0};

LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
	XrInstance instance,
	const char *name,
	PFN_xrVoidFunction *function);

LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrCreateApiLayerInstance(
	const XrInstanceCreateInfo *info,
	const XrApiLayerCreateInfo *apiLayerInfo,
	XrInstance *instance);

LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
	const XrNegotiateLoaderInfo *loaderInfo,
	const char *apiLayerName,
	XrNegotiateApiLayerRequest *apiLayerRequest);

static void capture_process_identity(void)
{
#if defined(_WIN32)
	DWORD path_len;
	const char *last_slash;

	g_pid = (unsigned long)GetCurrentProcessId();

	path_len = GetModuleFileNameA(NULL, g_exe_path, (DWORD)sizeof(g_exe_path));
	if (path_len == 0 || path_len >= sizeof(g_exe_path)) {
		g_exe_path[0] = 0;
		g_exe_name[0] = 0;
		return;
	}

	last_slash = strrchr(g_exe_path, '\\');
	if (!last_slash) {
		last_slash = strrchr(g_exe_path, '/');
	}

	if (last_slash && *(last_slash + 1) != 0) {
		snprintf(g_exe_name, sizeof(g_exe_name), "%s", last_slash + 1);
	} else {
		size_t name_len = strnlen(g_exe_path, sizeof(g_exe_name) - 1);
		memcpy(g_exe_name, g_exe_path, name_len);
		g_exe_name[name_len] = 0;
	}
#endif
}

static void log_runtime_identity(void)
{
	printf("[layer_demo] app='%s' app_ver=%u engine='%s' engine_ver=%u pid=%lu exe='%s' path='%s'\n",
		   g_app_name[0] ? g_app_name : "<unknown>",
		   g_app_version,
		   g_engine_name[0] ? g_engine_name : "<unknown>",
		   g_engine_version,
		   g_pid,
		   g_exe_name[0] ? g_exe_name : "<unknown>",
		   g_exe_path[0] ? g_exe_path : "<unknown>");
	fflush(stdout);
}

XRAPI_ATTR XrResult XRAPI_CALL demo_xrLocateSpace(
	XrSpace space,
	XrSpace baseSpace,
	XrTime time,
	XrSpaceLocation *location)
{
	XrResult result;

	if (!g_next_locate_space) {
		return XR_ERROR_HANDLE_INVALID;
	}

	result = g_next_locate_space(space, baseSpace, time, location);

	if (result == XR_SUCCESS && location &&
		location->type == XR_TYPE_SPACE_LOCATION &&
		(location->locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
		 printf("[layer_demo] app='%s' exe='%s' pid=%lu space=%p pos=(%.3f %.3f %.3f)\n",
			 g_app_name[0] ? g_app_name : "<unknown>",
			 g_exe_name[0] ? g_exe_name : "<unknown>",
			 g_pid,
			   (void *)space,
			   location->pose.position.x,
			   location->pose.position.y,
			   location->pose.position.z);
		fflush(stdout);
	}

	return result;
}

LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
	XrInstance instance,
	const char *name,
	PFN_xrVoidFunction *function)
{
	if (!name || !function) {
		return XR_ERROR_VALIDATION_FAILURE;
	}

	if (strcmp(name, "xrGetInstanceProcAddr") == 0) {
		*function = (PFN_xrVoidFunction)xrGetInstanceProcAddr;
		return XR_SUCCESS;
	}

	if (strcmp(name, "xrCreateApiLayerInstance") == 0) {
		*function = (PFN_xrVoidFunction)xrCreateApiLayerInstance;
		return XR_SUCCESS;
	}

	if (strcmp(name, "xrLocateSpace") == 0) {
		*function = (PFN_xrVoidFunction)demo_xrLocateSpace;
		return XR_SUCCESS;
	}

	if (!g_next_get_instance_proc_addr) {
		return XR_ERROR_INITIALIZATION_FAILED;
	}

	return g_next_get_instance_proc_addr(instance, name, function);
}

LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrCreateApiLayerInstance(
	const XrInstanceCreateInfo *info,
	const XrApiLayerCreateInfo *apiLayerInfo,
	XrInstance *instance)
{
	XrApiLayerCreateInfo chain_info;
	XrResult result;
	PFN_xrVoidFunction locate_space_fn = NULL;
	const XrApiLayerNextInfo *next_info;

	if (!apiLayerInfo || !instance) {
		return XR_ERROR_VALIDATION_FAILURE;
	}

	if (apiLayerInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO ||
		!apiLayerInfo->nextInfo ||
		apiLayerInfo->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO) {
		return XR_ERROR_INITIALIZATION_FAILED;
	}

	next_info = apiLayerInfo->nextInfo;
	g_next_get_instance_proc_addr = next_info->nextGetInstanceProcAddr;
	g_next_create_api_layer_instance = next_info->nextCreateApiLayerInstance;

	chain_info = *apiLayerInfo;
	chain_info.nextInfo = next_info->next;

	if (info) {
		snprintf(g_app_name, sizeof(g_app_name), "%s", info->applicationInfo.applicationName);
		g_app_version = info->applicationInfo.applicationVersion;
		snprintf(g_engine_name, sizeof(g_engine_name), "%s", info->applicationInfo.engineName);
		g_engine_version = info->applicationInfo.engineVersion;
	}

	capture_process_identity();
	log_runtime_identity();

	result = g_next_create_api_layer_instance(info, &chain_info, instance);
	if (result != XR_SUCCESS) {
		return result;
	}

	if (g_next_get_instance_proc_addr) {
		XrResult locate_result = g_next_get_instance_proc_addr(*instance, "xrLocateSpace", &locate_space_fn);
		if (locate_result == XR_SUCCESS) {
			g_next_locate_space = (PFN_xrLocateSpace)locate_space_fn;
		}
	}

	return XR_SUCCESS;
}

LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
	const XrNegotiateLoaderInfo *loaderInfo,
	const char *apiLayerName,
	XrNegotiateApiLayerRequest *apiLayerRequest)
{
	(void)apiLayerName;

	if (!loaderInfo || !apiLayerRequest) {
		return XR_ERROR_INITIALIZATION_FAILED;
	}

	if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
		loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION) {
		return XR_ERROR_INITIALIZATION_FAILED;
	}

	apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
	apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
	apiLayerRequest->getInstanceProcAddr = xrGetInstanceProcAddr;
	apiLayerRequest->createApiLayerInstance = xrCreateApiLayerInstance;

	return XR_SUCCESS;
}
