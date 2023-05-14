#ifndef __MAIN_H__
#define __MAIN_H__

#include <windows.h>

/*  To use this exported function of dll, include this header
 *  in your project.
 */

#ifdef BUILD_DLL
    #define DLL_EXPORT __declspec(dllexport)
#else
    #define DLL_EXPORT __declspec(dllimport)
#endif


#ifdef __cplusplus
extern "C"
{
#endif

//void DLL_EXPORT SomeFunction(const LPCSTR sometext);


typedef struct {
    char config_fp[256];
} thread_gui_data;


extern double offset_rotation_left_x;
extern double offset_rotation_left_y;
extern double offset_rotation_left_z;

extern double offset_rotation_right_x;
extern double offset_rotation_right_y;
extern double offset_rotation_right_z;

extern char config_file_path[256];

int write_config();

//int log_message(const char *fmt, ...);


#ifdef __cplusplus
}
#endif

#endif // __MAIN_H__
