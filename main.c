/*
    Copyright (C) 2022 Aruã Metello <aruametello@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


    its easier to find me at discord: osso#6389


    OpenXR proxy dll - adds controler rotation offsets.


    * what is the point of this tool/modification
    allow tweakable controler rotation offsets on theoretically any game running openXR.
    Personally i think all games should have those tweaks within the options the same
    way all mouse input games have sensitivity settings.

    * how does this proxy dll work?
    Rewrites some of the communication between the game and the openXR api, basically the api reads
    the real controler data, adds/subtracts the desired offsets and then passes the data to the game.

    * can this interfere with cheat protected games. (i.e. EAC)
    yes... i do not recomend to attempt usage on those.
    (they probably wont ban you but... who knows)

    * Why all functions are being "proxied by hand" ?
    Usually i use the awesome "wrappit" tool to automatically generate C code of
    a proxy dll without hassle and as close to no overhead you can get (naked pure assembly call of a jump)

    you can find wrappit here: https://www.codeproject.com/Articles/16541/Create-your-Proxy-DLLs-automatically

    but the "fine" folks at microsoft decided that "inline assembly" is bad and those functions
    are not supported anymore with their C/CPP compiler when targeting x86_64 or ARM code, so i went for the
    retarded route and wrote all calls by hand, since this dll has just a few (about 55 of them)

    on the other hand if anyone else wants to take a peek/poke at anything else, is probably just a few lines
    of code away from this starting point.

    OVR Advanced Settings perhalps could integrate IPC comunication with a tool like this to show a cool
    interface to tweak OpenXR stuff. (pretty much anything since the whole interface can be hijacked here)
*/



#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <stdio.h>
#include <openxr\openxr.h>
#include <time.h>
#include <math.h>

//uses https://github.com/MartinWeigel/Quaternion for quaternions because i suck at math
#include "Quaternion.h"

#ifndef M_PI
    #define M_PI (3.14159265358979323846)
#endif


#define degToRad(angleInDegrees) ((angleInDegrees) * M_PI / 180.0)
#define radToDeg(angleInRadians) ((angleInRadians) * 180.0 / M_PI)

#pragma pack(1)


HINSTANCE hLThis = 0;
HINSTANCE hL = 0;


#define jump_table_size 2048
#define jump_table_name_size 64
void ** jump_table;
char *jump_table_names;
FILE *h_log=0;

char root_folder[256];//workdir, OG dll and config file location.

/*
    some default values, what i use in bonelab atm.

    those values are in radians, the config file has
    them as degress.
*/
double offset_rotation_left_x=-0.11;
double offset_rotation_left_y=0.085;
double offset_rotation_left_z=0.0;

double offset_rotation_right_x=-0.11;
double offset_rotation_right_y=-0.085;
double offset_rotation_right_z=0.0;


//those are not constant, and will be corrected by xrStringToPath as needed.
uint64_t id_user_hand_left = 9;
uint64_t id_user_hand_right = 10;

/*
    aparently unity does initialize all controlers that it knows regardless
    of availability in the system.

    since the user might be a layman, just store all possible controlers
    on a "left hand" and "right hand" list, if any of those are found, apply offsets.

    if not, the user would have to know what controler he does have to assign
    the offsets only to it.

    knows controler types so far:
        htcvivecontroller - vive wands? no idea
        khrsimplecontroller - sounds like a skeleton driver
        microsoftmotioncontroller - probaly windows mixed reality
        oculustouchcontroller - oculus touch (any version?)
        valveindexcontroller - valve knuckles

*/
int action_space_list_hand_left_cnt=0;
uint64_t action_space_list_hand_left[64];
int action_space_list_hand_right_cnt=0;
uint64_t action_space_list_hand_right[64];

/*
    but beware!
    the idea above is dumb and will apply offsets to anything that
    has the subspace /user/hand/left or /user/hand/right
*/


/*
    Read the configuration file
*/
void str_lowercase(char *str)
{
    for(int i = 0; str[i]; i++)
      str[i] = tolower(str[i]);
}
int read_config(char *config_path)
{
    double double_temp;
    char line[512];
    char cmd[512];


    FILE *arq = fopen(config_path,"r");

    if (arq)
    {
        while (!feof(arq))
        {
            fscanf(arq,"%[^\n]\n",line);//ler uma linha
            str_lowercase(line);
            if (sscanf(line,"%[^#]",line))//cortar comentarios
            {
                //conferir qual tipo de informacao temos aqui
                sscanf(line,"%[^=]",cmd);

                if (strcmp(cmd,"right_hand_rotation_offset_x")==0)
                {
                    sscanf(line,"%*[^=]=%lf",&double_temp);
                    offset_rotation_right_x = degToRad(double_temp);
                }else
                if (strcmp(cmd,"right_hand_rotation_offset_y")==0)
                {
                    sscanf(line,"%*[^=]=%lf",&double_temp);
                    offset_rotation_right_y = degToRad(double_temp);
                }else
                if (strcmp(cmd,"right_hand_rotation_offset_z")==0)
                {
                    sscanf(line,"%*[^=]=%lf",&double_temp);
                    offset_rotation_right_z = degToRad(double_temp);
                }else
                if (strcmp(cmd,"left_hand_rotation_offset_x")==0)
                {
                    sscanf(line,"%*[^=]=%lf",&double_temp);
                    offset_rotation_left_x = degToRad(double_temp);
                }else
                if (strcmp(cmd,"left_hand_rotation_offset_y")==0)
                {
                    sscanf(line,"%*[^=]=%lf",&double_temp);
                    offset_rotation_left_y = degToRad(double_temp);
                }else
                if (strcmp(cmd,"left_hand_rotation_offset_z")==0)
                {
                    sscanf(line,"%*[^=]=%lf",&double_temp);
                    offset_rotation_left_z = degToRad(double_temp);
                } else {
                    char err[256];
                    sprintf(err,"Invalid line in openxr_loader_config.txt:\n%s\nThe application will be closed.",line);
                    MessageBox(0,err,"openvr_loader.dll proxy",MB_ICONERROR);
                    exit(0);
                }
            }
        }
    } else {
        char err[512];
        sprintf(err,"Could not find:\n%s!\n\nRead the instructions for further information.\nThe application will be closed.",config_path);
        MessageBoxA(0,err,"openvr_loader.dll proxy",MB_ICONERROR);
        exit(0);
    }

    fclose(arq);
}



/*
    trying to find where we are (where is openxr_loader.dll?)

    it will often not match the "current folder" of the process.

    in unity it is game_folder\game_data\plugins\x86_x64

    the plan is to look in the loaded module list, and pick
    the folder we are to use as a root folder.
    (search path for configuration and OG dll)

*/
int find_module_folder(char *search,char *output)
{
    HMODULE hMods[1024];
    HANDLE hProcess;
    DWORD cbNeeded;
    unsigned int i;
    DWORD processID = GetCurrentProcessId();

    // Print the process identifier.

    int ret_value = 0;
    //printf( "\nProcess ID: %u\n", processID );

    // Get a handle to the process.

    hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
                            PROCESS_VM_READ,
                            FALSE, processID );
    if (NULL == hProcess)
        return 1;

   // Get a list of all the modules in this process.

    if( EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
    {
        for ( i = 0; i < (cbNeeded / sizeof(HMODULE)); i++ )
        {
            TCHAR szModName[MAX_PATH];

            // Get the full path to the module's file.

            if ( GetModuleFileNameExA( hProcess, hMods[i], szModName,
                                      sizeof(szModName) / sizeof(TCHAR)))
            {
                // Print the module name and handle value.
                //printf( TEXT("\t%s (0x%08X)\n"), szModName, hMods[i] );

                //compare everything in lowercase
                str_lowercase(szModName);
                //find the last slash
                int s=-1;
                for (int c=0;szModName[c]!=0;c++)
                    if (szModName[c]=='\\') s = c;
                if (s>0)
                {
                    //the name of the file is the one we are looking for?
                    if (strcmp(&szModName[s+1],search)==0)
                    {
                        //return the folder it is
                        strncpy(output,szModName,s+1);
                        output[s+1]=0;
                        ret_value = 1;
                    }
                }


            }
        }
    }

    // Release the handle to the process.

    CloseHandle( hProcess );

    return ret_value;
}


int log_message(const char *fmt, ...)
{
    //log file is not opened?
    if (!h_log) return;

    char buffer[8192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    #ifdef DEBUG
        printf("%s\n",buffer);
        return 1;
    #endif // debug


    fprintf(h_log,"%s\n",buffer);
    fflush(h_log);


    return 1;
}

int log_init()
{
    #ifdef DEBUG
        return 1;
    #endif // DEBUG

    do {
        char log_filename[256];
        srand (time(NULL));
        sprintf(log_filename,"log_%d.txt",rand());
        h_log = fopen(log_filename,"a");
    } while (h_log==0);

    log_message("--- log start ---");
    return 1;
}

int log_close()
{
    fclose(h_log);
    return 1;
}

/*
    this hash function only has to be
    good enough to avoid colisions with
    the 55 ish names of functions in here

    i.e: i know its shit
*/
int simple_hash(const char *string)
{
    unsigned int hash=0;
    for (int c=0;string[c]!=0;c++) {
            //hash += string[c];
            hash = ((hash << 5) + hash) + string[c];
    }
    hash = hash % jump_table_size;
    return hash;
}

/*
    adds a name to the jump table

    the jump table exists to avoid a lot
    of strcmp calls when looking for
    a function name match.
*/
int assign_jump(const char *name,PFN_xrVoidFunction ptr)
{

    int position = simple_hash(name);

    if (jump_table[position]!=0)
    {
        char error[256];
        sprintf(error,"Hash colision! hash %d - %s",
                position,
                name);
        printf(error);

        exit(666);
    } else {
        jump_table[position] = ptr;
        char *n = &jump_table_names[position*64];
        strcpy(n,name);
        //log_message("added %s to jumptable location %d",name,position);
    }
    return 1;
}

int search_jump(const char *name,PFN_xrVoidFunction* ptr)
{
    /*
        1) do hash
        2) look at "hash" position in list if there is a value there
        3) check if the "name" being looked for maths with the found entry
    */
    int p = simple_hash(name);
    char *n = &jump_table_names[p*jump_table_name_size];
    if (strcmp(name,n)==0)
    {
        *ptr = jump_table[p];
        return 1;
    } else {
        return 0;//jump not in the list
    }

}



/*
    jump locations of each proxied function, functions with an preceding
    underline are the real dll ones.
*/

//those 3 functions are not in the exports, but exist in the OG dll? odd.
//probably many others fit this.
PFN_xrCreateHandTrackerEXT _xrCreateHandTrackerEXT=0;
PFN_xrDestroyHandTrackerEXT _xrDestroyHandTrackerEXT=0;
PFN_xrLocateHandJointsEXT _xrLocateHandJointsEXT=0;

//those others are all exported by the OG dll. the ordinals match in the distributed binary.
PFN_xrGetInstanceProcAddr _xrGetInstanceProcAddr;
PFN_xrEnumerateApiLayerProperties _xrEnumerateApiLayerProperties;
PFN_xrEnumerateInstanceExtensionProperties _xrEnumerateInstanceExtensionProperties;
PFN_xrCreateInstance _xrCreateInstance;
PFN_xrDestroyInstance _xrDestroyInstance;
PFN_xrGetInstanceProperties _xrGetInstanceProperties;
PFN_xrPollEvent _xrPollEvent;
PFN_xrResultToString _xrResultToString;
PFN_xrStructureTypeToString _xrStructureTypeToString;
PFN_xrGetSystem _xrGetSystem;
PFN_xrGetSystemProperties _xrGetSystemProperties;
PFN_xrEnumerateEnvironmentBlendModes _xrEnumerateEnvironmentBlendModes;
PFN_xrCreateSession _xrCreateSession;
PFN_xrDestroySession _xrDestroySession;
PFN_xrEnumerateReferenceSpaces _xrEnumerateReferenceSpaces;
PFN_xrCreateReferenceSpace _xrCreateReferenceSpace;
PFN_xrGetReferenceSpaceBoundsRect _xrGetReferenceSpaceBoundsRect;
PFN_xrCreateActionSpace _xrCreateActionSpace;
PFN_xrLocateSpace _xrLocateSpace;
PFN_xrDestroySpace _xrDestroySpace;
PFN_xrEnumerateViewConfigurations _xrEnumerateViewConfigurations;
PFN_xrGetViewConfigurationProperties _xrGetViewConfigurationProperties;
PFN_xrEnumerateViewConfigurationViews _xrEnumerateViewConfigurationViews;
PFN_xrEnumerateSwapchainFormats _xrEnumerateSwapchainFormats;
PFN_xrCreateSwapchain _xrCreateSwapchain;
PFN_xrDestroySwapchain _xrDestroySwapchain;
PFN_xrEnumerateSwapchainImages _xrEnumerateSwapchainImages;
PFN_xrAcquireSwapchainImage _xrAcquireSwapchainImage;
PFN_xrWaitSwapchainImage _xrWaitSwapchainImage;
PFN_xrReleaseSwapchainImage _xrReleaseSwapchainImage;
PFN_xrBeginSession _xrBeginSession;
PFN_xrEndSession _xrEndSession;
PFN_xrRequestExitSession _xrRequestExitSession;
PFN_xrWaitFrame _xrWaitFrame;
PFN_xrBeginFrame _xrBeginFrame;
PFN_xrEndFrame _xrEndFrame;
PFN_xrLocateViews _xrLocateViews;
PFN_xrStringToPath _xrStringToPath;
PFN_xrPathToString _xrPathToString;
PFN_xrCreateActionSet _xrCreateActionSet;
PFN_xrDestroyActionSet _xrDestroyActionSet;
PFN_xrCreateAction _xrCreateAction;
PFN_xrDestroyAction _xrDestroyAction;
PFN_xrSuggestInteractionProfileBindings _xrSuggestInteractionProfileBindings;
PFN_xrAttachSessionActionSets _xrAttachSessionActionSets;
PFN_xrGetCurrentInteractionProfile _xrGetCurrentInteractionProfile;
PFN_xrGetActionStateBoolean _xrGetActionStateBoolean;
PFN_xrGetActionStateFloat _xrGetActionStateFloat;
PFN_xrGetActionStateVector2f _xrGetActionStateVector2f;
PFN_xrGetActionStatePose _xrGetActionStatePose;
PFN_xrSyncActions _xrSyncActions;
PFN_xrEnumerateBoundSourcesForAction _xrEnumerateBoundSourcesForAction;
PFN_xrGetInputSourceLocalizedName _xrGetInputSourceLocalizedName;
PFN_xrApplyHapticFeedback _xrApplyHapticFeedback;
PFN_xrStopHapticFeedback _xrStopHapticFeedback;


/*
    entry point of a windows dll.
*/
BOOL WINAPI DllMain(HINSTANCE hInst,DWORD reason,LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
    {
		hLThis = hInst;

        //start a logfile (before any attempt of log_message!)
		log_init();


		//try to find myself (openxr_loader.dll) within the loaded modules
		//of the current process, we need that to know what folder to look
		//for other stuff. (config file and OG dll)
        if (!find_module_folder("openxr_loader.dll",root_folder))
        {
            root_folder[0]=0;//not found? trying with the current directory anyway.
            log_message("could not find my own dll!");
        } else {
            log_message("found my own dll at %s",root_folder);
        }

        char config_file_path[256];
        char ogdll_file_path[256];
        sprintf(config_file_path,"%sopenxr_loader_config.txt",root_folder);
        sprintf(ogdll_file_path,"%soriginal_openxr_loader.dll",root_folder);

        read_config(config_file_path);

        log_message("read configuration file from: %s\n",config_file_path);


        log_message("Using the following rotation offsets:");
        log_message("left: %fx%fx%f",
                    radToDeg(offset_rotation_left_x),
                    radToDeg(offset_rotation_left_y),
                    radToDeg(offset_rotation_left_z));
        log_message("right: %fx%fx%f",
                    radToDeg(offset_rotation_right_x),
                    radToDeg(offset_rotation_right_y),
                    radToDeg(offset_rotation_right_z));
        log_message("");

        //jumptable memory
        jump_table = (PFN_xrVoidFunction) malloc(jump_table_size * sizeof(PFN_xrVoidFunction));
        jump_table_names = (char *) malloc(sizeof(char) * jump_table_name_size * jump_table_size);


		hL = LoadLibrary(ogdll_file_path);
		if (!hL) {
            char err[512];
            sprintf(err,"Could not find:\n%s!\n\nread the instructions for further information.\n\n(do not overwrite the original file! rename it!)",ogdll_file_path);
            MessageBoxA(0,err,"OpenXR controler offset mod",MB_ICONERROR);
            exit(0);
		} else {
            log_message("hook sucessful to original_openxr_loader.dll");
		}



        /*
        //those functions arent exported, but exist, oddly xrGetInstanceProcAddr can tell their address.
        _xrCreateHandTrackerEXT = (PFN_xrCreateHandTrackerEXT) GetProcAddress(hL,"xrCreateHandTrackerEXT");
        _xrDestroyHandTrackerEXT = (PFN_xrDestroyHandTrackerEXT) GetProcAddress(hL,"xrDestroyHandTrackerEXT");
        _xrLocateHandJointsEXT = (PFN_xrLocateHandJointsEXT) GetProcAddress(hL,"xrLocateHandJointsEXT");
        */

        /*
            those are pointers to the real dll functions, from exports

            the functinos with a preceding underline are the real dll functions.

            the ones without are the proxied functions (ours)
        */
		_xrAcquireSwapchainImage = (PFN_xrAcquireSwapchainImage) GetProcAddress(hL,"xrAcquireSwapchainImage");
		_xrApplyHapticFeedback = (PFN_xrApplyHapticFeedback) GetProcAddress(hL,"xrApplyHapticFeedback");
		_xrAttachSessionActionSets = (PFN_xrAttachSessionActionSets) GetProcAddress(hL,"xrAttachSessionActionSets");
		_xrBeginFrame = (PFN_xrBeginFrame) GetProcAddress(hL,"xrBeginFrame");
		_xrBeginSession = (PFN_xrBeginSession) GetProcAddress(hL,"xrBeginSession");
		_xrCreateAction = (PFN_xrCreateAction) GetProcAddress(hL,"xrCreateAction");
		_xrCreateActionSet = (PFN_xrCreateActionSet) GetProcAddress(hL,"xrCreateActionSet");
		_xrCreateActionSpace = (PFN_xrCreateActionSpace) GetProcAddress(hL,"xrCreateActionSpace");
		_xrCreateInstance = (PFN_xrCreateInstance) GetProcAddress(hL,"xrCreateInstance");
		_xrCreateReferenceSpace = (PFN_xrCreateReferenceSpace) GetProcAddress(hL,"xrCreateReferenceSpace");
		_xrCreateSession = (PFN_xrCreateSession) GetProcAddress(hL,"xrCreateSession");
		_xrCreateSwapchain = (PFN_xrCreateSwapchain) GetProcAddress(hL,"xrCreateSwapchain");
		_xrDestroyAction = (PFN_xrDestroyAction) GetProcAddress(hL,"xrDestroyAction");
		_xrDestroyActionSet = (PFN_xrDestroyActionSet) GetProcAddress(hL,"xrDestroyActionSet");
		_xrDestroyInstance = (PFN_xrDestroyInstance) GetProcAddress(hL,"xrDestroyInstance");
		_xrDestroySession = (PFN_xrDestroySession) GetProcAddress(hL,"xrDestroySession");
		_xrDestroySpace = (PFN_xrDestroySpace) GetProcAddress(hL,"xrDestroySpace");
		_xrDestroySwapchain = (PFN_xrDestroySwapchain) GetProcAddress(hL,"xrDestroySwapchain");
		_xrEndFrame = (PFN_xrEndFrame) GetProcAddress(hL,"xrEndFrame");
		_xrEndSession = (PFN_xrEndSession) GetProcAddress(hL,"xrEndSession");
		_xrEnumerateApiLayerProperties = (PFN_xrEnumerateApiLayerProperties) GetProcAddress(hL,"xrEnumerateApiLayerProperties");
		_xrEnumerateBoundSourcesForAction = (PFN_xrEnumerateBoundSourcesForAction) GetProcAddress(hL,"xrEnumerateBoundSourcesForAction");
		_xrEnumerateEnvironmentBlendModes = (PFN_xrEnumerateEnvironmentBlendModes) GetProcAddress(hL,"xrEnumerateEnvironmentBlendModes");
		_xrEnumerateInstanceExtensionProperties = (PFN_xrEnumerateInstanceExtensionProperties) GetProcAddress(hL,"xrEnumerateInstanceExtensionProperties");
		_xrEnumerateReferenceSpaces = (PFN_xrEnumerateReferenceSpaces) GetProcAddress(hL,"xrEnumerateReferenceSpaces");
		_xrEnumerateSwapchainFormats = (PFN_xrEnumerateSwapchainFormats) GetProcAddress(hL,"xrEnumerateSwapchainFormats");
		_xrEnumerateSwapchainImages = (PFN_xrEnumerateSwapchainImages) GetProcAddress(hL,"xrEnumerateSwapchainImages");
		_xrEnumerateViewConfigurationViews = (PFN_xrEnumerateViewConfigurationViews) GetProcAddress(hL,"xrEnumerateViewConfigurationViews");
		_xrEnumerateViewConfigurations = (PFN_xrEnumerateViewConfigurations) GetProcAddress(hL,"xrEnumerateViewConfigurations");
		_xrGetActionStateBoolean = (PFN_xrGetActionStateBoolean) GetProcAddress(hL,"xrGetActionStateBoolean");
		_xrGetActionStateFloat = (PFN_xrGetActionStateFloat) GetProcAddress(hL,"xrGetActionStateFloat");
		_xrGetActionStatePose = (PFN_xrGetActionStatePose) GetProcAddress(hL,"xrGetActionStatePose");
		_xrGetActionStateVector2f = (PFN_xrGetActionStateVector2f) GetProcAddress(hL,"xrGetActionStateVector2f");
		_xrGetCurrentInteractionProfile = (PFN_xrGetCurrentInteractionProfile) GetProcAddress(hL,"xrGetCurrentInteractionProfile");
		_xrGetInputSourceLocalizedName = (PFN_xrGetInputSourceLocalizedName) GetProcAddress(hL,"xrGetInputSourceLocalizedName");
		_xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr) GetProcAddress(hL,"xrGetInstanceProcAddr");
		_xrGetInstanceProperties = (PFN_xrGetInstanceProperties) GetProcAddress(hL,"xrGetInstanceProperties");
		_xrGetReferenceSpaceBoundsRect = (PFN_xrGetReferenceSpaceBoundsRect) GetProcAddress(hL,"xrGetReferenceSpaceBoundsRect");
		_xrGetSystem = (PFN_xrGetSystem) GetProcAddress(hL,"xrGetSystem");
		_xrGetSystemProperties = (PFN_xrGetSystemProperties) GetProcAddress(hL,"xrGetSystemProperties");
		_xrGetViewConfigurationProperties = (PFN_xrGetViewConfigurationProperties) GetProcAddress(hL,"xrGetViewConfigurationProperties");
		_xrLocateSpace = (PFN_xrLocateSpace) GetProcAddress(hL,"xrLocateSpace");
		_xrLocateViews = (PFN_xrLocateViews) GetProcAddress(hL,"xrLocateViews");
		_xrPathToString = (PFN_xrPathToString) GetProcAddress(hL,"xrPathToString");
		_xrPollEvent = (PFN_xrPollEvent) GetProcAddress(hL,"xrPollEvent");
		_xrReleaseSwapchainImage = (PFN_xrReleaseSwapchainImage) GetProcAddress(hL,"xrReleaseSwapchainImage");
		_xrRequestExitSession = (PFN_xrRequestExitSession) GetProcAddress(hL,"xrRequestExitSession");
		_xrResultToString = (PFN_xrResultToString) GetProcAddress(hL,"xrResultToString");
		_xrStopHapticFeedback = (PFN_xrStopHapticFeedback) GetProcAddress(hL,"xrStopHapticFeedback");
		_xrStringToPath = (PFN_xrStringToPath) GetProcAddress(hL,"xrStringToPath");
		_xrStructureTypeToString = (PFN_xrStructureTypeToString) GetProcAddress(hL,"xrStructureTypeToString");
		_xrSuggestInteractionProfileBindings = (PFN_xrSuggestInteractionProfileBindings) GetProcAddress(hL,"xrSuggestInteractionProfileBindings");
		_xrSyncActions = (PFN_xrSyncActions) GetProcAddress(hL,"xrSyncActions");
		_xrWaitFrame = (PFN_xrWaitFrame) GetProcAddress(hL,"xrWaitFrame");
		_xrWaitSwapchainImage = (PFN_xrWaitSwapchainImage) GetProcAddress(hL,"xrWaitSwapchainImage");


        //do the jump table based on the dead simple hash names of the functions
        for (int c=0;c<jump_table_size;c++) jump_table[c] = 0;//init


        /*
            add the functions to the jump table (pointers to the proxy functions)

            any ommited functions arent proxied, so you can add/ommit stuff as
            you need.

            warning: i did mistakes while the matching function prototypes, therefore
                     some of those could crash due to mistakes in pointers and whatnot.
                     what happened is: i realised i didnt needed to implement everything
                     because i just wanted controler offsets, so "beware" of everything
                     else.
        */

        /*
        assign_jump("xrAcquireSwapchainImage",(PFN_xrVoidFunction) xrAcquireSwapchainImage);
        assign_jump("xrApplyHapticFeedback",(PFN_xrVoidFunction) xrApplyHapticFeedback);
        assign_jump("xrAttachSessionActionSets",(PFN_xrVoidFunction) xrAttachSessionActionSets);
        assign_jump("xrBeginFrame",(PFN_xrVoidFunction) xrBeginFrame);
        assign_jump("xrBeginSession",(PFN_xrVoidFunction) xrBeginSession);
        assign_jump("xrCreateInstance",(PFN_xrVoidFunction) xrCreateInstance);
        assign_jump("xrCreateReferenceSpace",(PFN_xrVoidFunction) xrCreateReferenceSpace);
        assign_jump("xrCreateSession",(PFN_xrVoidFunction) xrCreateSession);
        assign_jump("xrCreateSwapchain",(PFN_xrVoidFunction) xrCreateSwapchain);
        assign_jump("xrDestroyAction",(PFN_xrVoidFunction) xrDestroyAction);
        assign_jump("xrDestroyActionSet",(PFN_xrVoidFunction) xrDestroyActionSet);
        assign_jump("xrDestroyInstance",(PFN_xrVoidFunction) xrDestroyInstance);
        assign_jump("xrDestroySession",(PFN_xrVoidFunction) xrDestroySession);
        assign_jump("xrDestroySpace",(PFN_xrVoidFunction) xrDestroySpace);
        assign_jump("xrDestroySwapchain",(PFN_xrVoidFunction) xrDestroySwapchain);
        assign_jump("xrEndFrame",(PFN_xrVoidFunction) xrEndFrame);
        assign_jump("xrEndSession",(PFN_xrVoidFunction) xrEndSession);
        assign_jump("xrEnumerateApiLayerProperties",(PFN_xrVoidFunction) xrEnumerateApiLayerProperties);
        assign_jump("xrEnumerateBoundSourcesForAction",(PFN_xrVoidFunction) xrEnumerateBoundSourcesForAction);
        assign_jump("xrEnumerateEnvironmentBlendModes",(PFN_xrVoidFunction) xrEnumerateEnvironmentBlendModes);
        assign_jump("xrEnumerateInstanceExtensionProperties",(PFN_xrVoidFunction) xrEnumerateInstanceExtensionProperties);
        assign_jump("xrEnumerateReferenceSpaces",(PFN_xrVoidFunction) xrEnumerateReferenceSpaces);
        assign_jump("xrEnumerateSwapchainFormats",(PFN_xrVoidFunction) xrEnumerateSwapchainFormats);
        assign_jump("xrEnumerateSwapchainImages",(PFN_xrVoidFunction) xrEnumerateSwapchainImages);
        assign_jump("xrEnumerateViewConfigurationViews",(PFN_xrVoidFunction) xrEnumerateViewConfigurationViews);
        assign_jump("xrEnumerateViewConfigurations",(PFN_xrVoidFunction) xrEnumerateViewConfigurations);
        assign_jump("xrGetActionStateBoolean",(PFN_xrVoidFunction) xrGetActionStateBoolean);
        assign_jump("xrGetActionStateFloat",(PFN_xrVoidFunction) xrGetActionStateFloat);
        assign_jump("xrGetActionStatePose",(PFN_xrVoidFunction) xrGetActionStatePose);
        assign_jump("xrGetActionStateVector2f",(PFN_xrVoidFunction) xrGetActionStateVector2f);
        assign_jump("xrGetCurrentInteractionProfile",(PFN_xrVoidFunction) xrGetCurrentInteractionProfile);
        assign_jump("xrGetInputSourceLocalizedName",(PFN_xrVoidFunction) xrGetInputSourceLocalizedName);
        assign_jump("xrGetInstanceProcAddr",(PFN_xrVoidFunction) xrGetInstanceProcAddr);
        assign_jump("xrGetInstanceProperties",(PFN_xrVoidFunction) xrGetInstanceProperties);
        assign_jump("xrGetReferenceSpaceBoundsRect",(PFN_xrVoidFunction) xrGetReferenceSpaceBoundsRect);
        assign_jump("xrGetSystem",(PFN_xrVoidFunction) xrGetSystem);
        assign_jump("xrGetSystemProperties",(PFN_xrVoidFunction) xrGetSystemProperties);
        assign_jump("xrGetViewConfigurationProperties",(PFN_xrVoidFunction) xrGetViewConfigurationProperties);
        */

        assign_jump("xrCreateAction",(PFN_xrVoidFunction) xrCreateAction);
        assign_jump("xrCreateActionSet",(PFN_xrVoidFunction) xrCreateActionSet);
        assign_jump("xrCreateActionSpace",(PFN_xrVoidFunction) xrCreateActionSpace);

        assign_jump("xrLocateSpace",(PFN_xrVoidFunction) xrLocateSpace);

        assign_jump("xrStringToPath",(PFN_xrVoidFunction) xrStringToPath);


        /*
        assign_jump("xrLocateViews",(PFN_xrVoidFunction) xrLocateViews);
        assign_jump("xrPathToString",(PFN_xrVoidFunction) xrPathToString);
        assign_jump("xrPollEvent",(PFN_xrVoidFunction) xrPollEvent);
        assign_jump("xrReleaseSwapchainImage",(PFN_xrVoidFunction) xrReleaseSwapchainImage);
        assign_jump("xrRequestExitSession",(PFN_xrVoidFunction) xrRequestExitSession);
        assign_jump("xrResultToString",(PFN_xrVoidFunction) xrResultToString);
        assign_jump("xrStopHapticFeedback",(PFN_xrVoidFunction) xrStopHapticFeedback);
        assign_jump("xrStructureTypeToString",(PFN_xrVoidFunction) xrStructureTypeToString);
        assign_jump("xrSuggestInteractionProfileBindings",(PFN_xrVoidFunction) xrSuggestInteractionProfileBindings);
        assign_jump("xrSyncActions",(PFN_xrVoidFunction) xrSyncActions);
        assign_jump("xrWaitFrame",(PFN_xrVoidFunction) xrWaitFrame);
        assign_jump("xrWaitSwapchainImage",(PFN_xrVoidFunction) xrWaitSwapchainImage);
        */
    }

	if (reason == DLL_PROCESS_DETACH)
    {
        log_message("--- DLL_PROCESS_DETACH, end of log ---");
        log_close();
        free(jump_table);
        free(jump_table_names);
		FreeLibrary(hL);
    }

	return 1;
}



// xrGetInstanceProcAddr
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
    XrInstance                                  instance,
    const char*                                 name,
    PFN_xrVoidFunction*                         function)
{

    /*
        some monkey business needs to happen here.

        this function is when the game asks openXR what is the pointer
        of the named function so it call it by itself.

        if we just forward the original DLL answer, the game will
        "bypass" the proxy dll and call the original function instead

        what we need is to answer with "our" function when needed.

        this serves as an dual purpose to select what functions to
        actually handle here, if any are desired to be fully ignored.

    */

    //we call the real function anyway (to avoid changing the normal flow)
    XrResult ret = _xrGetInstanceProcAddr(instance,name,function);

    //if the real function succeded, we spoof the answer.
    if (ret==XR_SUCCESS)
    {
        //finding the same procedure inside our proxy dll
        PFN_xrVoidFunction our_target=0;
        if (search_jump(name,&our_target))
        {
            log_message("i: %u - Rerouting %s to the proxy. (%p -> %p)",instance,name,function,our_target);

            //we override the answer to the game engine
            *function = our_target;

        } else {
            //the function to override is not in the jump table?

            //but is one of those know to be in the OG dll without an export?
            /*
            if (strcmp("-xrCreateHandTrackerEXT",name)==0)
            {
                _xrCreateHandTrackerEXT=function;
                function = xrCreateHandTrackerEXT;
            } else
            if (strcmp("-xrLocateHandJointsEXT",name)==0)
            {
                _xrLocateHandJointsEXT=function;
                function = xrLocateHandJointsEXT;
            } else
            if (strcmp("-xrDestroyHandTrackerEXT",name)==0)
            {
                _xrDestroyHandTrackerEXT=function;
                function = xrDestroyHandTrackerEXT;
            }*/

            //keeping the original dll answer, we dont have a replacement function
            //log_message("i: %d - Function requested is not in the jump table %s, bypassing to OG DLL: %p",instance,name,function);
        }
    } else {
        log_message("i: %d - xrGetInstanceProcAddr: %s not found on the original dll? - %d",instance,name,ret);
    }

    //whatever is the return value, always pass it forward!
    return ret;

}


void XRAPI_PTR xrCreateHandTrackerEXT (XrSession session, const XrHandTrackerCreateInfoEXT* createInfo, XrHandTrackerEXT* handTracker)
{
    log_message("xrCreateHandTrackerEXT");
    _xrCreateHandTrackerEXT(session,createInfo,handTracker);
}

void XRAPI_PTR xrDestroyHandTrackerEXT(XrHandTrackerEXT handTracker)
{
    log_message("xrDestroyHandTrackerEXT");
    _xrDestroyHandTrackerEXT(handTracker);
}

void XRAPI_PTR xrLocateHandJointsEXT(XrHandTrackerEXT handTracker, const XrHandJointsLocateInfoEXT* locateInfo, XrHandJointLocationsEXT* locations)
{
    log_message("xrLocateHandJointsEXT");
    _xrLocateHandJointsEXT(handTracker,locateInfo,locations);
}


// xrBeginSession
XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(
    XrSession                                   session,
    const XrSessionBeginInfo*                   beginInfo)
{
    log_message("xrBeginSession %d",rand()%1000);

    return _xrBeginSession(session,beginInfo);
}


// xrAcquireSwapchainImage
XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(
    XrSwapchain                                 swapchain,
    const XrSwapchainImageAcquireInfo*          acquireInfo,
    uint32_t*                                   index)
{

    log_message("xrAcquireSwapchainImage %d",rand()%1000);

    return _xrAcquireSwapchainImage(swapchain,acquireInfo,index);
}

// xrApplyHapticFeedback
XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(
    XrSession                                   session,
    const XrHapticActionInfo*                   hapticActionInfo,
    const XrHapticBaseHeader*                   hapticFeedback)
{
    log_message("xrApplyHapticFeedback %d",rand()%1000);

    return _xrApplyHapticFeedback(session,hapticActionInfo,hapticFeedback);
}

// xrAttachSessionActionSets
XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(
    XrSession                                   session,
    const XrSessionActionSetsAttachInfo*        attachInfo)
{

    log_message("xrAttachSessionActionSets %d",rand()%1000);

    return _xrAttachSessionActionSets(session,attachInfo);
}

// xrBeginFrame
XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(
    XrSession                                   session,
    const XrFrameBeginInfo*                     frameBeginInfo)
{
    log_message("xrBeginFrame %d",rand()%1000);

    return _xrBeginFrame(session,frameBeginInfo);
}

// xrCreateAction
XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(
    XrActionSet                                 actionSet,
    const XrActionCreateInfo*                   createInfo,
    XrAction*                                   action)
{


    XrResult ret = _xrCreateAction(actionSet,createInfo,action);

    if (ret==XR_SUCCESS)
    {
        log_message("xrCreateAction parentActionset: %lu type: %d actiontype: %d actionname: %s localizedactionname: %s output: %lu",
                    actionSet,
                    createInfo->type,
                    createInfo->actionType,
                    createInfo->actionName,
                    createInfo->localizedActionName,
                    *action);

        //list subpaths? (i dunno even what that is exactly)
        for (int c=0;c<createInfo->countSubactionPaths;c++)
        {
            log_message("Subpath: %u",createInfo->subactionPaths[c]);
        }
    }


    return ret;
}

// xrCreateActionSet
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(
    XrInstance                                  instance,
    const XrActionSetCreateInfo*                createInfo,
    XrActionSet*                                actionSet)
{

    XrResult ret = _xrCreateActionSet(instance,createInfo,actionSet);

    if (ret==XR_SUCCESS)
    {
        log_message("xrCreateActionSet type: %d - %s - %s - output: %lu",
                    createInfo->type,
                    createInfo->actionSetName,
                    createInfo->localizedActionSetName,
                    *actionSet);
    }
    return ret;

}

// xrCreateActionSpace
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(
    XrSession                                   session,
    const XrActionSpaceCreateInfo*              createInfo,
    XrSpace*                                    space)
{


    XrResult ret = _xrCreateActionSpace(session,createInfo,space);

    log_message("xrCreateActionSpace action:%u  subactionpath:%lu next:%u space out:%lu",
                createInfo->action,
                //createInfo->poseInActionSpace,//algo sobre coordenadas desse action space
                createInfo->subactionPath,
                createInfo->next,
                *space);

    //is the "thing" attached to the user left hand or right hand? put it in the list...
    if (createInfo->subactionPath==id_user_hand_left)
    {
        action_space_list_hand_left[action_space_list_hand_left_cnt] = *space;
        action_space_list_hand_left_cnt++;
    } else
    if (createInfo->subactionPath==id_user_hand_right)
    {
        action_space_list_hand_right[action_space_list_hand_right_cnt] = *space;
        action_space_list_hand_right_cnt++;
    }


    return ret;

}

// xrCreateInstance
XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance(
    const XrInstanceCreateInfo*                 createInfo,
    XrInstance*                                 instance)
{
    log_message("xrCreateInstance %d",rand()%1000);
    return _xrCreateInstance(createInfo,instance);
}

// xrCreateReferenceSpace
XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(
    XrSession                                   session,
    const XrReferenceSpaceCreateInfo*           createInfo,
    XrSpace*                                    space)
{
    log_message("xrCreateReferenceSpace %d",createInfo->type);

    return _xrCreateReferenceSpace(session,createInfo,space);
}

// xrCreateSession
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSession(
    XrInstance                                  instance,
    const XrSessionCreateInfo*                  createInfo,
    XrSession*                                  session)
{
    log_message(" #### xrCreateSession %d #### ",rand()%1000);
    return _xrCreateSession(instance,createInfo,session);
}

// xrCreateSwapchain
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(
    XrSession                                   session,
    const XrSwapchainCreateInfo*                createInfo,
    XrSwapchain*                                swapchain)
{
    log_message("xrCreateSwapchain %d",rand()%1000);
    return _xrCreateSwapchain(session,createInfo,swapchain);
}

// xrDestroyAction
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyAction(
    XrAction                                    action)
{
    log_message("xrDestroyAction %d",rand()%1000);
    return _xrDestroyAction(action);
}

// xrDestroyActionSet
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet(
    XrActionSet                                 actionSet)
{
    log_message("xrDestroyActionSet %d",rand()%1000);
    return _xrDestroyActionSet(actionSet);
}

// xrDestroyInstance
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(
    XrInstance                                  instance)
{
    log_message("xrDestroyInstance %d",rand()%1000);
    return _xrDestroyInstance(instance);
}

// xrDestroySession
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession(
    XrSession                                   session)
{
    log_message("xrDestroySession %d",rand()%1000);
    return _xrDestroySession(session);
}

// xrDestroySpace
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(
    XrSpace                                     space)
{
    log_message("xrDestroySpace %d",rand()%1000);
    return _xrDestroySpace(space);
}

// xrDestroySwapchain
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(
    XrSwapchain                                 swapchain)
{
    log_message("xrDestroySwapchain %d",rand()%1000);
    return _xrDestroySwapchain(swapchain);
}

// xrEndFrame
XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(
    XrSession                                   session,
    const XrFrameEndInfo*                       frameEndInfo)
{
    log_message("xrEndFrame %d",rand()%1000);
    return _xrEndFrame(session,frameEndInfo);
}

// xrEndSession
XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(
    XrSession                                   session)
{
    log_message("xrEndSession %d",rand()%1000);
    return _xrEndSession(session);
}

// xrEnumerateApiLayerProperties
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateApiLayerProperties(
    uint32_t                                    propertyCapacityInput,
    uint32_t*                                   propertyCountOutput,
    XrApiLayerProperties*                       properties)
{
    return xrEnumerateApiLayerProperties(propertyCapacityInput,propertyCountOutput,properties);
    log_message("xrEnumerateApiLayerProperties - count: %d",*propertyCountOutput);
}

// xrEnumerateBoundSourcesForAction
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateBoundSourcesForAction(
    XrSession                                   session,
    const XrBoundSourcesForActionEnumerateInfo* enumerateInfo,
    uint32_t                                    sourceCapacityInput,
    uint32_t*                                   sourceCountOutput,
    XrPath*                                     sources)
{
    log_message("xrEnumerateBoundSourcesForAction %d",rand()%1000);
    return _xrEnumerateBoundSourcesForAction(session,enumerateInfo,sourceCapacityInput,sourceCountOutput,sources);
}

// xrEnumerateEnvironmentBlendModes
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    XrViewConfigurationType                     viewConfigurationType,
    uint32_t                                    environmentBlendModeCapacityInput,
    uint32_t*                                   environmentBlendModeCountOutput,
    XrEnvironmentBlendMode*                     environmentBlendModes)
{
    log_message("xrEnumerateEnvironmentBlendModes %d",rand()%1000);
    return _xrEnumerateEnvironmentBlendModes(instance,systemId,viewConfigurationType,environmentBlendModeCapacityInput,environmentBlendModeCountOutput,environmentBlendModes);
}

// xrEnumerateInstanceExtensionProperties
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(
    const char*                                 layerName,
    uint32_t                                    propertyCapacityInput,
    uint32_t*                                   propertyCountOutput,
    XrExtensionProperties*                      properties)
{

    //observing data before calling the real dll, this is useful to peek
    //and modify the "question" to openXR
    log_message("xrEnumerateInstanceExtensionProperties name: %s Capacity: %u ptr: %p",
                layerName,
                propertyCapacityInput,
                properties);

    //the real call
    XrResult ret = _xrEnumerateInstanceExtensionProperties(layerName,propertyCapacityInput,propertyCountOutput,properties);

    //observing data after calling the real dll, this is useful to peek
    //and modify the "answer" to the game engine
    log_message("Count output: %u  %p - ret: %d",
                *propertyCountOutput,
                properties,
                ret);

    return ret;

}

// xrEnumerateReferenceSpaces
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateReferenceSpaces(
    XrSession                                   session,
    uint32_t                                    spaceCapacityInput,
    uint32_t*                                   spaceCountOutput,
    XrReferenceSpaceType*                       spaces)
{
    log_message("xrEnumerateReferenceSpaces %d",rand()%1000);
    return _xrEnumerateReferenceSpaces(session,spaceCapacityInput,spaceCountOutput,spaces);
}

// xrEnumerateSwapchainFormats
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainFormats(
    XrSession                                   session,
    uint32_t                                    formatCapacityInput,
    uint32_t*                                   formatCountOutput,
    int64_t*                                    formats)
{
    log_message("xrEnumerateSwapchainFormats %d",rand()%1000);
    return _xrEnumerateSwapchainFormats(session,formatCapacityInput,formatCountOutput,formats);
}

// xrEnumerateSwapchainImages
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(
    XrSwapchain                                 swapchain,
    uint32_t                                    imageCapacityInput,
    uint32_t*                                   imageCountOutput,
    XrSwapchainImageBaseHeader*                 images)
{
    log_message("xrEnumerateSwapchainImages %d",rand()%1000);
    return _xrEnumerateSwapchainImages(swapchain,imageCapacityInput,imageCountOutput,images);
}

// xrEnumerateViewConfigurationViews
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    XrViewConfigurationType                     viewConfigurationType,
    uint32_t                                    viewCapacityInput,
    uint32_t*                                   viewCountOutput,
    XrViewConfigurationView*                    views)
{
    log_message("xrEnumerateViewConfigurationViews %d",rand()%1000);
    return _xrEnumerateViewConfigurationViews(instance,systemId,viewConfigurationType,viewCapacityInput,viewCountOutput,views);
}

// xrEnumerateViewConfigurations
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurations(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    uint32_t                                    viewConfigurationTypeCapacityInput,
    uint32_t*                                   viewConfigurationTypeCountOutput,
    XrViewConfigurationType*                    viewConfigurationTypes)
{
    log_message("xrEnumerateViewConfigurations %d",rand()%1000);
    return _xrEnumerateViewConfigurations(instance,systemId,viewConfigurationTypeCapacityInput,viewConfigurationTypeCountOutput,viewConfigurationTypes);
}

// xrGetActionStateBoolean
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(
    XrSession                                   session,
    const XrActionStateGetInfo*                 getInfo,
    XrActionStateBoolean*                       state)
{
    log_message("xrGetActionStateBoolean %d",rand()%1000);
    return _xrGetActionStateBoolean(session,getInfo,state);
}

// xrGetActionStateFloat
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(
    XrSession                                   session,
    const XrActionStateGetInfo*                 getInfo,
    XrActionStateFloat*                         state)
{
    log_message("xrGetActionStateFloat %d",rand()%1000);
    return _xrGetActionStateFloat(session,getInfo,state);
}

// xrGetActionStatePose
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(
    XrSession                                   session,
    const XrActionStateGetInfo*                 getInfo,
    XrActionStatePose*                          state)
{
    log_message("xrGetActionStatePose %d",rand()%1000);
    return _xrGetActionStatePose(session,getInfo,state);
}

// xrGetActionStateVector2f
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(
    XrSession                                   session,
    const XrActionStateGetInfo*                 getInfo,
    XrActionStateVector2f*                      state)
{
    log_message("xrGetActionStateVector2f %d",rand()%1000);
    return _xrGetActionStateVector2f(session,getInfo,state);
}
// xrGetCurrentInteractionProfile
XRAPI_ATTR XrResult XRAPI_CALL xrGetCurrentInteractionProfile(
    XrSession                                   session,
    XrPath                                      topLevelUserPath,
    XrInteractionProfileState*                  interactionProfile)
{
    log_message("xrGetCurrentInteractionProfile %d",rand()%1000);
    return _xrGetCurrentInteractionProfile(session,topLevelUserPath,interactionProfile);
}

// xrGetInputSourceLocalizedName
XRAPI_ATTR XrResult XRAPI_CALL xrGetInputSourceLocalizedName(
    XrSession                                   session,
    const XrInputSourceLocalizedNameGetInfo*    getInfo,
    uint32_t                                    bufferCapacityInput,
    uint32_t*                                   bufferCountOutput,
    char*                                       buffer)
{
    log_message("xrGetInputSourceLocalizedName %d",rand()%1000);
    return _xrGetInputSourceLocalizedName(session,getInfo,bufferCapacityInput,bufferCountOutput,buffer);
}


// xrGetInstanceProperties
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProperties(
    XrInstance                                  instance,
    XrInstanceProperties*                       instanceProperties)
{
    log_message("xrGetInstanceProperties %d",rand()%1000);
    return _xrGetInstanceProperties(instance,instanceProperties);
}

// xrGetReferenceSpaceBoundsRect
XRAPI_ATTR XrResult XRAPI_CALL xrGetReferenceSpaceBoundsRect(
    XrSession                                   session,
    XrReferenceSpaceType                        referenceSpaceType,
    XrExtent2Df*                                bounds)
{
    log_message("xrGetReferenceSpaceBoundsRect %d",rand()%1000);
    return _xrGetReferenceSpaceBoundsRect(session,referenceSpaceType,bounds);
}

// xrGetSystem
XRAPI_ATTR XrResult XRAPI_CALL xrGetSystem(
    XrInstance                                  instance,
    const XrSystemGetInfo*                      getInfo,
    XrSystemId*                                 systemId)
{
    log_message("xrGetSystem %d",rand()%1000);
    return _xrGetSystem(instance,getInfo,systemId);
}

// xrGetSystemProperties
XRAPI_ATTR XrResult XRAPI_CALL xrGetSystemProperties(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    XrSystemProperties*                         properties)
{
    log_message("xrGetSystemProperties %d",rand()%1000);
    return _xrGetSystemProperties(instance,systemId,properties);
}

// xrGetViewConfigurationProperties
XRAPI_ATTR XrResult XRAPI_CALL xrGetViewConfigurationProperties(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    XrViewConfigurationType                     viewConfigurationType,
    XrViewConfigurationProperties*              configurationProperties)
{
    log_message("xrGetViewConfigurationProperties %d",rand()%1000);
    return _xrGetViewConfigurationProperties(instance,systemId,viewConfigurationType,configurationProperties);
}

// xrLocateSpace
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(
    XrSpace                                     space,
    XrSpace                                     baseSpace,
    XrTime                                      time,
    XrSpaceLocation*                            location)
{

    XrResult ret = _xrLocateSpace(space,baseSpace,time,location);

    //if its the type we want for rotation offsets
    if (location->type==XR_TYPE_SPACE_LOCATION)
    {


        //is this an offset of the left hand?
        for (int c=0;c<action_space_list_hand_left_cnt;c++)
        {
            if (space==action_space_list_hand_left[c])
            {

                Quaternion hand;

                /* Library uses zyxw, openXR uses xyzw */
                hand.v[Z_AXIS] = location->pose.orientation.z;
                hand.v[Y_AXIS] = location->pose.orientation.y;
                hand.v[X_AXIS] = location->pose.orientation.x;
                hand.w = location->pose.orientation.w;
                Quaternion_normalize(&hand,&hand);

                Quaternion rotation;
                float rotation_zyx[3];
                rotation_zyx[Z_AXIS] = offset_rotation_left_z;
                rotation_zyx[Y_AXIS] = offset_rotation_left_y;
                rotation_zyx[X_AXIS] = offset_rotation_left_x;
                Quaternion_fromEulerZYX(rotation_zyx,&rotation);//create the "rotated" rotation
                Quaternion_multiply(&rotation,&hand,&hand);


                location->pose.orientation.z = hand.v[Z_AXIS];
                location->pose.orientation.y = hand.v[Y_AXIS];
                location->pose.orientation.x = hand.v[X_AXIS];
                location->pose.orientation.w = hand.w;


                return ret;
            }
        }
        //is this an offset of the right hand?
        for (int c=0;c<action_space_list_hand_right_cnt;c++)
        {
            if (space==action_space_list_hand_right[c])
            {

                Quaternion hand;

                /* Library uses zyxw, openXR uses xyzw */
                hand.v[Z_AXIS] = location->pose.orientation.z;
                hand.v[Y_AXIS] = location->pose.orientation.y;
                hand.v[X_AXIS] = location->pose.orientation.x;
                hand.w = location->pose.orientation.w;
                Quaternion_normalize(&hand,&hand);

                Quaternion rotation;
                float rotation_zyx[3];
                rotation_zyx[Z_AXIS] = offset_rotation_right_z;
                rotation_zyx[Y_AXIS] = offset_rotation_right_y;
                rotation_zyx[X_AXIS] = offset_rotation_right_x;
                Quaternion_fromEulerZYX(rotation_zyx,&rotation);//create the "rotated" rotation
                Quaternion_multiply(&rotation,&hand,&hand);


                location->pose.orientation.z = hand.v[Z_AXIS];
                location->pose.orientation.y = hand.v[Y_AXIS];
                location->pose.orientation.x = hand.v[X_AXIS];
                location->pose.orientation.w = hand.w;


                return ret;
            }
            //same
        }
    }

    //all other cases
    return ret;
}

// xrLocateViews
XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(
    XrSession                                   session,
    const XrViewLocateInfo*                     viewLocateInfo,
    XrViewState*                                viewState,
    uint32_t                                    viewCapacityInput,
    uint32_t*                                   viewCountOutput,
    XrView*                                     views)
{
    log_message("xrLocateViews %d",rand()%1000);
    return _xrLocateViews(session,viewLocateInfo,viewState,viewCapacityInput,viewCountOutput,views);
}

// xrPathToString
XRAPI_ATTR XrResult XRAPI_CALL xrPathToString(
    XrInstance                                  instance,
    XrPath                                      path,
    uint32_t                                    bufferCapacityInput,
    uint32_t*                                   bufferCountOutput,
    char*                                       buffer)
{
    log_message("xrPathToString %d",rand()%1000);
    return _xrPathToString(instance,path,bufferCapacityInput,bufferCountOutput,buffer);
}

// xrPollEvent
XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(
    XrInstance                                  instance,
    XrEventDataBuffer*                          eventData)
{
    log_message("xrPollEvent %d",rand()%1000);
    return _xrPollEvent(instance,eventData);
}


// xrReleaseSwapchainImage
XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(
    XrSwapchain                                 swapchain,
    const XrSwapchainImageReleaseInfo*          releaseInfo)
{
    log_message("xrReleaseSwapchainImage %d",rand()%1000);
    return _xrReleaseSwapchainImage(swapchain,releaseInfo);
}

// xrRequestExitSession
XRAPI_ATTR XrResult XRAPI_CALL xrRequestExitSession(
    XrSession                                   session)
{
    log_message("xrRequestExitSession %d",rand()%1000);
    return _xrRequestExitSession(session);
}

// xrResultToString
XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(
    XrInstance                                  instance,
    XrResult                                    value,
    char                                        buffer[XR_MAX_RESULT_STRING_SIZE])
{
    log_message("xrResultToString %d",rand()%1000);
    return _xrResultToString(instance,value,buffer);
}

// xrStopHapticFeedback
XRAPI_ATTR XrResult XRAPI_CALL xrStopHapticFeedback(
    XrSession                                   session,
    const XrHapticActionInfo*                   hapticActionInfo)
{
    log_message("xrStopHapticFeedback %d",rand()%1000);
    return _xrStopHapticFeedback(session,hapticActionInfo);
}

// xrStringToPath
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(
    XrInstance                                  instance,
    const char*                                 pathString,
    XrPath*                                     path)
{


    XrResult ret = _xrStringToPath(instance,pathString,path);



    log_message("xrStringToPath pathString: %s answer: %lu",
                pathString,
                *path);

    //store in a very dumb way the number equivalent of those
    if (strcmp(pathString,"/user/hand/left")==0) id_user_hand_left = *path;
    if (strcmp(pathString,"/user/hand/right")==0) id_user_hand_right = *path;


    return ret;
}

// xrStructureTypeToString
XRAPI_ATTR XrResult XRAPI_CALL xrStructureTypeToString(
    XrInstance                                  instance,
    XrStructureType                             value,
    char                                        buffer[XR_MAX_STRUCTURE_NAME_SIZE])
{
    log_message("xrStructureTypeToString %d",rand()%1000);
    return _xrStructureTypeToString(instance,value,buffer);
}

// xrSuggestInteractionProfileBindings
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(
    XrInstance                                  instance,
    const XrInteractionProfileSuggestedBinding* suggestedBindings)
{
    log_message("xrSuggestInteractionProfileBindings %d",rand()%1000);
    return _xrSuggestInteractionProfileBindings(instance,suggestedBindings);
}

// xrSyncActions
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(
    XrSession                                   session,
    const XrActionsSyncInfo*                    syncInfo)
{
    log_message("xrSyncActions %d",rand()%1000);
    return _xrSyncActions(session,syncInfo);
}

// xrWaitFrame
XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(
    XrSession                                   session,
    const XrFrameWaitInfo*                      frameWaitInfo,
    XrFrameState*                               frameState)
{
    log_message("xrWaitFrame %d",rand()%1000);
    return _xrWaitFrame(session,frameWaitInfo,frameState);
}

// xrWaitSwapchainImage
XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(
    XrSwapchain                                 swapchain,
    const XrSwapchainImageWaitInfo*             waitInfo)
{
    log_message("xrWaitSwapchainImage %d",rand()%1000);
    return _xrWaitSwapchainImage(swapchain,waitInfo);
}











/*
    =====================================================
    everything beyond this point is testing for some
    basic sanities because its been a while i havent
    programmed something with confusing pointers.

    this REALLY does not fit any purpose in the dll, but
    if you compile this as an console application, it
    will call the main function and do some basic
    stuff to see if the proxy is working properly. (ish)

    test code is partially borrowed from here:
    https://gitlab.freedesktop.org/monado/demos/openxr-simple-example/-/blob/master/main.c

    just enough to see if openXR gets pasts the first few calls.
*/

int xr_check(XrInstance instance, XrResult result, const char* format, ...)
{
	if (XR_SUCCEEDED(result))
		return 1;

	char resultString[XR_MAX_RESULT_STRING_SIZE];
	xrResultToString(instance, result, resultString);

	char formatRes[XR_MAX_RESULT_STRING_SIZE + 1024];
	snprintf(formatRes, XR_MAX_RESULT_STRING_SIZE + 1023, "%s [%s] (%d)\n", format, resultString,
	         result);

	va_list args;
	va_start(args, format);
	vprintf(formatRes, args);
	va_end(args);

	return 0;
}


static void
print_viewconfig_view_info(uint32_t view_count, XrViewConfigurationView* viewconfig_views)
{
	for (uint32_t i = 0; i < view_count; i++)
    {
		printf("View Configuration View %d:\n", i);
		printf("\tResolution       : Recommended %dx%d, Max: %dx%d\n",
		       viewconfig_views[0].recommendedImageRectWidth,
		       viewconfig_views[0].recommendedImageRectHeight, viewconfig_views[0].maxImageRectWidth,
		       viewconfig_views[0].maxImageRectHeight);
		printf("\tSwapchain Samples: Recommended: %d, Max: %d)\n",
		       viewconfig_views[0].recommendedSwapchainSampleCount,
		       viewconfig_views[0].maxSwapchainSampleCount);
	}
}


/*
    OpenXR internally has its quaternions in 32bit precision

    to avoid losing precision during calculations, we internally
    mess with them at 64bit precision, then downgrade back to 32bit.
*/
void quaternion_double_to_float(double input[4],double output[4])
{
    output[0] = input[0];
    output[1] = input[1];
    output[2] = input[2];
    output[3] = input[3];
}
void quaternion_float_to_double(float input[4],double output[4])
{
    output[0] = input[0];
    output[1] = input[1];
    output[2] = input[2];
    output[3] = input[3];
}





void print_euler(float e[3])
{
    printf("%8.3f x %8.3f x %8.3f",
           radToDeg(e[0]),
           radToDeg(e[1]),
           radToDeg(e[2]));
}

void print_quaternion(Quaternion q)
{
    float euler_zyx[3];
    Quaternion_toEulerZYX(&q,euler_zyx);
    printf("%8.3f x %8.3f x %8.3f",
           radToDeg(euler_zyx[0]),
           radToDeg(euler_zyx[1]),
           radToDeg(euler_zyx[2]));
}

void sprint_quaternion(char *s,Quaternion q)
{
    float euler_zyx[3];
    Quaternion_toEulerZYX(&q,euler_zyx);
    sprintf(s,"%8.3f x %8.3f x %8.3f",
           radToDeg(euler_zyx[0]),
           radToDeg(euler_zyx[1]),
           radToDeg(euler_zyx[2]));
}


int main()
{



    /*
    char root_folder[256];
    find_module_folder("original_openxr_loader.dll",root_folder);

    printf("Found in: %s\n",root_folder);
    exit(0);
    */


    printf("debug\n");
    DllMain(0,DLL_PROCESS_ATTACH,0);

    PFN_xrVoidFunction ptr=0;
    if (search_jump("xrGetViewConfigurationProperties",&ptr))
    {
        printf("achei ptr %p\n",ptr);
    } else {
        printf("funcao nao inclusa! %p\n",ptr);
    }

    printf("-------------- %d\n",sizeof(ptr));
    //ptr = 0;

    PFN_xrCreateInstance kek_xrCreateInstance = 0;
    xrGetInstanceProcAddr(0,"xrCreateInstance",&kek_xrCreateInstance);
    printf("Compare ptr: %p and %p\n",kek_xrCreateInstance,_xrCreateInstance);

    PFN_xrEnumerateInstanceExtensionProperties kek_xrEnumerateInstanceExtensionProperties =0;
    xrGetInstanceProcAddr(0,"xrEnumerateInstanceExtensionProperties",&kek_xrEnumerateInstanceExtensionProperties);

    printf("Compare ptr: %p and %p\n",kek_xrEnumerateInstanceExtensionProperties,kek_xrEnumerateInstanceExtensionProperties);





    //=======================================
    //demo sanity test

	int enabled_ext_count = 0;
	//const char* enabled_exts[1] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};

	XrInstanceCreateInfo instance_create_info = {
	    .type = XR_TYPE_INSTANCE_CREATE_INFO,
	    .next = NULL,
	    .createFlags = 0,
	    .enabledExtensionCount = enabled_ext_count,
	    //.enabledExtensionNames = enabled_exts,
	    .enabledApiLayerCount = 0,
	    .enabledApiLayerNames = NULL,
	    .applicationInfo =
	        {
	            // some compilers have trouble with char* initialization
	            .applicationName = "",
	            .engineName = "",
	            .applicationVersion = 1,
	            .engineVersion = 0,
	            .apiVersion = XR_CURRENT_API_VERSION,
	        },
    };


	// Changing to HANDHELD_DISPLAY or a future form factor may work, but has not been tested.
	XrFormFactor form_factor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	// Changing the form_factor may require changing the view_type too.
	XrViewConfigurationType view_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

	// Typically STAGE for room scale/standing, LOCAL for seated
	XrReferenceSpaceType play_space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
	XrSpace play_space = XR_NULL_HANDLE;

	// the system represents an (opaque) set of XR devices in use, managed by the runtime
	XrSystemId system_id = XR_NULL_SYSTEM_ID;
	// the session deals with the renderloop submitting frames to the runtime
	XrSession session = XR_NULL_HANDLE;
    XrInstance instance = XR_NULL_HANDLE;

	strncpy(instance_create_info.applicationInfo.applicationName, "OpenXR OpenGL Example",
	        XR_MAX_APPLICATION_NAME_SIZE);
	strncpy(instance_create_info.applicationInfo.engineName, "Custom", XR_MAX_ENGINE_NAME_SIZE);

	XrResult result = kek_xrCreateInstance(&instance_create_info, &instance);
	if (!xr_check(NULL, result, "Failed to create XR instance."))
		return 1;
		else
        {
            printf("Sucesso em kek_xrCreateInstance\n");
        }


    printf("------------\n");
    unsigned int cnt;
    XrExtensionProperties array_prop[64];



    for (int c=0;c<64;c++)
    {
        array_prop[c].type = XR_TYPE_EXTENSION_PROPERTIES;
        array_prop[c].next = &array_prop[c];
    }

    kek_xrEnumerateInstanceExtensionProperties(0,0,&cnt,0);
    unsigned int request_n = cnt;


    int ret = kek_xrEnumerateInstanceExtensionProperties(0,request_n,&cnt,array_prop);
    printf("retorno: %d\n",ret);


    for (int c=0;c<cnt;c++)
    {
        printf("%s %u\n",array_prop[c].extensionName,array_prop[c].extensionVersion);
    }

    printf("xrEnumerateInstanceExtensionProperties: cnt %u\n",cnt);


    printf("------------\n");



	// --- Get XrSystemId
	XrSystemGetInfo system_get_info = {
	    .type = XR_TYPE_SYSTEM_GET_INFO, .formFactor = form_factor, .next = NULL};

	result = xrGetSystem(instance, &system_get_info, &system_id);
	if (!xr_check(instance, result, "Failed to get system for HMD form factor."))
		return 1;

	printf("Successfully got XrSystem with id %lu for HMD form factor\n", system_id);


	uint32_t view_count = 0;
	// the viewconfiguration views contain information like resolution about each view
	XrViewConfigurationView* viewconfig_views = NULL;


	result = xrEnumerateViewConfigurationViews(instance, system_id, view_type, 0, &view_count, NULL);
	if (!xr_check(instance, result, "Failed to get view configuration view count!"))
		return 1;

	viewconfig_views = malloc(sizeof(XrViewConfigurationView) * view_count);
	for (uint32_t i = 0; i < view_count; i++) {
		viewconfig_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		viewconfig_views[i].next = NULL;
	}

	result = xrEnumerateViewConfigurationViews(instance, system_id, view_type, view_count,
	                                           &view_count, viewconfig_views);
	if (!xr_check(instance, result, "Failed to enumerate view configuration views!"))
		return 1;
	print_viewconfig_view_info(view_count, viewconfig_views);

    //quaternion test
    XrSpaceLocation location;



    printf("--------------\n");

    return 0;
}
