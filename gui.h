#ifndef __GUI_H__
#define __GUI_H__

#include <windows.h>
#include <stdio.h>
#include "main.h"


extern char config_file_path[256];


int launch_gui_thread();

void cmd_set_l_rx(double val);
void cmd_set_l_ry(double val);
void cmd_set_l_rz(double val);
void cmd_set_r_rx(double val);
void cmd_set_r_ry(double val);
void cmd_set_r_rz(double val);


HANDLE add_button(int x,int y,int tx,int ty,char *text,int id);
HANDLE add_label(int x,int y,int tx,int ty,char *text,int id);




#endif
