
#include "gui.h"




HWND hwnd;               /* This is the handle for our window */
MSG messages;            /* Here messages to the application are saved */
WNDCLASSEX wincl;        /* Data structure for the windowclass */
HANDLE hInstance;


#ifndef M_PI
    #define M_PI (3.14159265358979323846)
#endif

#define degToRad(angleInDegrees) ((angleInDegrees) * M_PI / 180.0)
#define radToDeg(angleInRadians) ((angleInRadians) * 180.0 / M_PI)



HANDLE HWND_LABEL_L_RX;
HANDLE HWND_LABEL_L_RY;
HANDLE HWND_LABEL_L_RZ;

HANDLE HWND_LABEL_R_RX;
HANDLE HWND_LABEL_R_RY;
HANDLE HWND_LABEL_R_RZ;


char config_fp[256];

int flag_quit=0;


#define offset_dialog_constats 32000

enum {
    ID_NULL,
    ID_LABEL_L_RY,
    ID_LABEL_L_RX,
    ID_LABEL_L_RZ,

    ID_BTN_L_YP_10,
    ID_BTN_L_YP_01,
    ID_BTN_L_YN_10,
    ID_BTN_L_YN_01,

    ID_BTN_L_XP_10,
    ID_BTN_L_XP_01,
    ID_BTN_L_XN_10,
    ID_BTN_L_XN_01,

    ID_BTN_L_ZP_10,
    ID_BTN_L_ZP_01,
    ID_BTN_L_ZN_10,
    ID_BTN_L_ZN_01,

    ID_LABEL_R_RY,
    ID_LABEL_R_RX,
    ID_LABEL_R_RZ,

    ID_BTN_R_YP_10,
    ID_BTN_R_YP_01,
    ID_BTN_R_YN_10,
    ID_BTN_R_YN_01,

    ID_BTN_R_XP_10,
    ID_BTN_R_XP_01,
    ID_BTN_R_XN_10,
    ID_BTN_R_XN_01,

    ID_BTN_R_ZP_10,
    ID_BTN_R_ZP_01,
    ID_BTN_R_ZN_10,
    ID_BTN_R_ZN_01
};


LRESULT CALLBACK WindowProcedure_setup_job (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)                  /* handle the messages */
    {

        case WM_CREATE:

            break;

        case WM_DESTROY:
            PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
            break;
        case WM_COMMAND:

            switch (wParam)
            {
                //=========================
                //LEFT
                //rotacao X+
                case offset_dialog_constats + ID_BTN_L_XP_10:
                    cmd_set_l_rx(offset_rotation_left_x+degToRad(1.0));
                    return 1;
                case offset_dialog_constats + ID_BTN_L_XP_01:
                    cmd_set_l_rx(offset_rotation_left_x+degToRad(0.1));
                    return 1;

                //rotacao X-
                case offset_dialog_constats + ID_BTN_L_XN_10:
                    cmd_set_l_rx(offset_rotation_left_x-degToRad(1.0));
                    return 1;

                    break;
                case offset_dialog_constats + ID_BTN_L_XN_01:
                    cmd_set_l_rx(offset_rotation_left_x-degToRad(0.1));
                    return 1;
                    break;

                //rotacao Y+
                case offset_dialog_constats + ID_BTN_L_YP_10:
                    cmd_set_l_ry(offset_rotation_left_y+degToRad(1.0));
                    return 1;
                    break;
                case offset_dialog_constats + ID_BTN_L_YP_01:
                    cmd_set_l_ry(offset_rotation_left_y+degToRad(0.1));
                    return 1;
                    break;

                //rotacao Y-
                case offset_dialog_constats + ID_BTN_L_YN_10:
                    cmd_set_l_ry(offset_rotation_left_y-degToRad(1.0));
                    return 1;
                    break;
                case offset_dialog_constats + ID_BTN_L_YN_01:
                    cmd_set_l_ry(offset_rotation_left_y-degToRad(0.1));
                    return 1;
                    break;

                //rotacao Z+
                case offset_dialog_constats + ID_BTN_L_ZP_10:
                    cmd_set_l_rz(offset_rotation_left_z+degToRad(1.0));
                    return 1;
                    break;
                case offset_dialog_constats + ID_BTN_L_ZP_01:
                    cmd_set_l_rz(offset_rotation_left_z+degToRad(0.1));
                    return 1;
                    break;

                //rotacao Z-
                case offset_dialog_constats + ID_BTN_L_ZN_10:
                    cmd_set_l_rz(offset_rotation_left_z-degToRad(1.0));
                    return 1;
                    break;
                case offset_dialog_constats + ID_BTN_L_ZN_01:
                    cmd_set_l_rz(offset_rotation_left_z-degToRad(0.1));
                    return 1;
                    break;


                //=========================
                //RIGHT
                //rotacao X+
                case offset_dialog_constats + ID_BTN_R_XP_10:
                    cmd_set_r_rx(offset_rotation_right_x+degToRad(1.0));
                    return 1;
                    break;
                case offset_dialog_constats + ID_BTN_R_XP_01:
                    cmd_set_r_rx(offset_rotation_right_x+degToRad(0.1));
                    return 1;
                    break;

                //rotacao X-
                case offset_dialog_constats + ID_BTN_R_XN_10:
                    cmd_set_r_rx(offset_rotation_right_x-degToRad(1.0));
                    return 1;
                    break;
                case offset_dialog_constats + ID_BTN_R_XN_01:
                    cmd_set_r_rx(offset_rotation_right_x-degToRad(0.1));
                    return 1;
                    break;

                //rotacao Y+
                case offset_dialog_constats + ID_BTN_R_YP_10:
                    cmd_set_r_ry(offset_rotation_right_y+degToRad(1.0));
                    return 1;
                    break;
                case offset_dialog_constats + ID_BTN_R_YP_01:
                    cmd_set_r_ry(offset_rotation_right_y+degToRad(0.1));
                    return 1;
                    break;

                //rotacao Y-
                case offset_dialog_constats + ID_BTN_R_YN_10:
                    cmd_set_r_ry(offset_rotation_right_y-degToRad(1.0));
                    return 1;
                    break;
                case offset_dialog_constats + ID_BTN_R_YN_01:
                    cmd_set_r_ry(offset_rotation_right_y-degToRad(0.1));
                    return 1;
                    break;

                //rotacao Z+
                case offset_dialog_constats + ID_BTN_R_ZP_10:
                    cmd_set_r_rz(offset_rotation_right_z+degToRad(1.0));
                    return 1;
                    break;
                case offset_dialog_constats + ID_BTN_R_ZP_01:
                    cmd_set_r_rz(offset_rotation_right_z+degToRad(0.1));
                    return 1;
                    break;

                //rotacao Z-
                case offset_dialog_constats + ID_BTN_R_ZN_10:
                    cmd_set_r_rz(offset_rotation_right_z-degToRad(1.0));
                    return 1;
                    break;
                case offset_dialog_constats + ID_BTN_R_ZN_01:
                    cmd_set_r_rz(offset_rotation_right_z-degToRad(0.1));
                    return 1;
                    break;

                default:;
            }
            break;
        case WM_KEYDOWN:
            //printf("WM_KEYDOWN: %d\n",wParam,lParam);
            switch (wParam)
            {
                case VK_F1:
                    //printf("MODO Manual!\n");
                    break;
                case VK_F2:
                    //printf("MODO automatico!\n");
                    break;
                case VK_F3:
                    //send_gcode("!");
                    break;
                case VK_F4:
                    //send_gcode("@11");
                    break;
                case VK_F11:
                    /*
                    EnableWindow(hwnd_botao_seta_adjm,0);
                    EnableWindow(hwnd_botao_seta_adjp,0);
                    EnableWindow(hwnd_botao_seta_yp,0);
                    EnableWindow(hwnd_botao_seta_ym,0);
                    EnableWindow(hwnd_botao_seta_xp,0);
                    EnableWindow(hwnd_botao_seta_xm,0);
                    EnableWindow(hwnd_botao_seta_xp,0);
                    EnableWindow(hwnd_botao_seta_xm,0);
                    */
                    break;

                case VK_F8:
                    //printf("F8 apertado ligar alarme!!\n");
                    break;

            }
            break;

        //fazer algo qd o user apertar o botão de fechar
        case WM_CLOSE:
            //flag_quit = 1;
            return 1;


        default:                      /* for messages that we don't deal with */
            return DefWindowProc (hwnd, message, wParam, lParam);
    }

    return 0;

}


HANDLE add_label(int x,int y,int tx,int ty,char *text,int id)
{

    int  new_id = offset_dialog_constats + id;

    //picking a new id for this control
    HANDLE handle_n =  CreateWindow("static", text,//tipo, rotulo
                         WS_CHILD | WS_VISIBLE,//flags
                          x, y,//posicao
                          tx, ty,//tamanho
                           hwnd, (HMENU) new_id,//identificacao
                          hInstance, NULL);





    return handle_n;
}

HANDLE add_button(int x,int y,int tx,int ty,char *text,int id)
{
    int  new_id = offset_dialog_constats + id;

    //picking a new id for this control
    HANDLE handle_n =  CreateWindow("button", text,//tipo, rotulo
                         WS_CHILD | WS_VISIBLE,//flags
                          x, y,//posicao
                          tx, ty,//tamanho
                           hwnd, (HMENU) new_id,//identificacao
                          hInstance, NULL);


    return handle_n;
}


//funções q comunicam a gui e a thread principal de mudanças
void cmd_set_l_rx(double val)
{
    //potencial treta de threads...
    offset_rotation_left_x = val;

    char st_btn[16];
    sprintf(st_btn,"%1.2f",radToDeg(val));
    //SendMessage(HWND_LABEL_L_RX, WM_SETTEXT, 0, st_btn);
    SetWindowTextA(HWND_LABEL_L_RX,st_btn);


    write_config(config_file_path);
}

void cmd_set_l_ry(double val)
{
    //potencial treta de threads...
    offset_rotation_left_y = val;

    char st_btn[16];
    sprintf(st_btn,"%1.2f",radToDeg(val));
    //SendMessage(HWND_LABEL_L_RY, WM_SETTEXT, 0, st_btn);
    SetWindowTextA(HWND_LABEL_L_RY,st_btn);

    write_config(config_file_path);
}

void cmd_set_l_rz(double val)
{
    //potencial treta de threads...
    offset_rotation_left_z = val;

    char st_btn[16];
    sprintf(st_btn,"%1.2f",radToDeg(val));
    //SendMessage(HWND_LABEL_L_RZ, WM_SETTEXT, 0, st_btn);
    SetWindowTextA(HWND_LABEL_L_RZ,st_btn);

    write_config(config_file_path);
}


void cmd_set_r_rx(double val)
{
    //potencial treta de threads...
    offset_rotation_right_x = val;

    char st_btn[16];
    sprintf(st_btn,"%1.2f",radToDeg(val));
    //SendMessage(HWND_LABEL_R_RX, WM_SETTEXT, 0, st_btn);
    SetWindowTextA(HWND_LABEL_R_RX,st_btn);

    write_config(config_file_path);
}

void cmd_set_r_ry(double val)
{
    //potencial treta de threads...
    offset_rotation_right_y = val;

    char st_btn[16];
    sprintf(st_btn,"%1.2f",radToDeg(val));
    //SendMessage(HWND_LABEL_R_RY, WM_SETTEXT, 0, st_btn);
    SetWindowTextA(HWND_LABEL_R_RY,st_btn);

    write_config(config_file_path);
}

void cmd_set_r_rz(double val)
{
    //potencial treta de threads...
    offset_rotation_right_z = val;

    char st_btn[16];
    sprintf(st_btn,"%1.2f",radToDeg(val));
    //SendMessage(HWND_LABEL_R_RZ, WM_SETTEXT, 0, st_btn);
    SetWindowTextA(HWND_LABEL_R_RZ,st_btn);

    write_config(config_file_path);
}







char szClassName[] = "openxr offsets thingy halfassed";

void create_gui()
{


    //printf("Pasta: %s\n",ret_browsefolder());


    hInstance = GetModuleHandle(0);


    /* The Window structure */
    wincl.hInstance = hInstance;
    wincl.lpszClassName = szClassName;
    wincl.lpfnWndProc = WindowProcedure_setup_job;      /* This function is called by windows */
    wincl.style = CS_DBLCLKS;                 /* Catch double-clicks */
    wincl.cbSize = sizeof (WNDCLASSEX);

    /* Use default icon and mouse-pointer */
    wincl.hIcon = LoadIcon (NULL, IDI_APPLICATION);
    wincl.hIconSm = LoadIcon (NULL, IDI_APPLICATION);
    wincl.hCursor = LoadCursor (NULL, IDC_ARROW);
    wincl.lpszMenuName = NULL;                 /* No menu */
    wincl.cbClsExtra = 0;                      /* No extra bytes after the window class */
    wincl.cbWndExtra = 0;                      /* structure or the window instance */
    /* Use Windows's default color as the background of the window */
    wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

    /* Register the window class, and if it fails quit the program */
    if (!RegisterClassEx (&wincl))
        return;

    /* The class is registered, let's create the program*/
    hwnd = CreateWindowEx (
           0,                   /* Extended possibilites for variation */
           szClassName,         /* Classname */
           "OpenXR controller offsets",       /* Title Text */
           WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, /* default window */
           CW_USEDEFAULT,       /* Windows decides the position */
           CW_USEDEFAULT,       /* where the window ends up on the screen */
           370,                 /* The programs width */
           420,                 /* and height in pixels */
           HWND_DESKTOP,        /* The window is a child-window to desktop */
           NULL,                /* No menu */
           hInstance,       /* Program Instance handler */
           NULL                 /* No Window Creation data */
           );



    //
    //int x_setas = 50;
    //int y_setas = 100;
    //int setas_tam_btn = 40;

    //
    int yo;
    int xo;
    char stlabel[32];

    //================================================
    //menu superior
    add_label(135,10,100,20,"left controller",0);


    yo=70;
    xo=0;

    add_label(xo+25,yo-30,40,20,"Pitch:",0);

    sprintf(stlabel,"%1.2f",radToDeg(offset_rotation_left_x));
    HWND_LABEL_L_RX = add_label(xo+65,yo-30,40,20,stlabel,ID_LABEL_L_RX);


    add_button(xo+10,yo,50,50,"+1.0",ID_BTN_L_XP_10);
    add_button(xo+10,yo+50,50,50,"-1.0",ID_BTN_L_XN_10);

    add_button(xo+65,yo,50,50,"+0.1",ID_BTN_L_XP_01);
    add_button(xo+65,yo+50,50,50,"-0.1",ID_BTN_L_XN_01);

    yo=70;
    xo=120;

    add_label(xo+25,yo-30,40,20,"Yaw:",0);
    sprintf(stlabel,"%1.2f",radToDeg(offset_rotation_left_y));
    HWND_LABEL_L_RY = add_label(xo+65,yo-30,40,20,stlabel,ID_LABEL_L_RY);

    add_button(xo+10,yo,50,50,"+1.0",ID_BTN_L_YP_10);
    add_button(xo+10,yo+50,50,50,"-1.0",ID_BTN_L_YN_10);

    add_button(xo+65,yo,50,50,"+0.1",ID_BTN_L_YP_01);
    add_button(xo+65,yo+50,50,50,"-0.1",ID_BTN_L_YN_01);


    yo=70;
    xo=240;

    add_label(xo+25,yo-30,40,20,"Roll:",0);
    sprintf(stlabel,"%1.2f",radToDeg(offset_rotation_left_z));
    HWND_LABEL_L_RZ = add_label(xo+65,yo-30,40,20,stlabel,ID_LABEL_L_RZ);

    add_button(xo+10,yo,50,50,"+1.0",ID_BTN_L_ZP_10);
    add_button(xo+10,yo+50,50,50,"-1.0",ID_BTN_L_ZN_10);

    add_button(xo+65,yo,50,50,"+0.1",ID_BTN_L_ZP_01);
    add_button(xo+65,yo+50,50,50,"-0.1",ID_BTN_L_ZN_01);


    //==========================================
    add_label(135,210,100,20,"right controller",0);

    yo=270;
    xo=0;

    add_label(xo+25,yo-30,40,20,"Pitch:",0);
    sprintf(stlabel,"%1.2f",radToDeg(offset_rotation_right_x));
    HWND_LABEL_R_RX = add_label(xo+65,yo-30,40,20,stlabel,ID_LABEL_R_RX);

    add_button(xo+10,yo,50,50,"+1.0",ID_BTN_R_XP_10);
    add_button(xo+10,yo+50,50,50,"-1.0",ID_BTN_R_XN_10);

    add_button(xo+65,yo,50,50,"+0.1",ID_BTN_R_XP_01);
    add_button(xo+65,yo+50,50,50,"-0.1",ID_BTN_R_XN_01);

    yo=270;
    xo=120;

    add_label(xo+25,yo-30,40,20,"Yaw:",0);
    sprintf(stlabel,"%1.2f",radToDeg(offset_rotation_right_y));
    HWND_LABEL_R_RY = add_label(xo+65,yo-30,40,20,stlabel,ID_LABEL_R_RY);

    add_button(xo+10,yo,50,50,"+1.0",ID_BTN_R_YP_10);
    add_button(xo+10,yo+50,50,50,"-1.0",ID_BTN_R_YN_10);

    add_button(xo+65,yo,50,50,"+0.1",ID_BTN_R_YP_01);
    add_button(xo+65,yo+50,50,50,"-0.1",ID_BTN_R_YN_01);


    yo=270;
    xo=240;

    add_label(xo+25,yo-30,40,20,"Roll:",0);
    sprintf(stlabel,"%1.2f",radToDeg(offset_rotation_right_z));
    HWND_LABEL_R_RZ = add_label(xo+65,yo-30,40,20,stlabel,ID_LABEL_R_RZ);

    add_button(xo+10,yo,50,50,"+1.0",ID_BTN_R_ZP_10);
    add_button(xo+10,yo+50,50,50,"-1.0",ID_BTN_R_ZN_10);

    add_button(xo+65,yo,50,50,"+0.1",ID_BTN_R_ZP_01);
    add_button(xo+65,yo+50,50,50,"-0.1",ID_BTN_R_ZN_01);





    /*
     Novos botões:

     - Fazer pos atual (não incluindo Z) como novo offset (mirar e cortar rapido)
        -> definir offset

     -> mover para origem (0,0,0) (levando offset em consideração)

     -> posicao de troca de ferramenta e wp

     -> colocar cores nos botões (especialmente botão verde para iniciar
                                   e botão vermelho para parar)
    */



    //tamanho ajuste

    if (hwnd == NULL)
    {
        //printf("Falha ao criar a janela!\n");
        return;
    } else {
        ShowWindow(hwnd, SW_SHOW);
        //printf("janela criada com sucesso!\n");
    }
}


int kill_gui()
{
    flag_quit = 1;
    return 1;
}

int gui_doevents()
{
    MSG msg;

    // enquanto nós temos novas mensagens
    //while (GetMessage(&msg, NULL, 0, 0))
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        // Manuzear as mensagns
        switch (msg.message)
        {
            //casos de fechar a engine
            case WM_QUIT:
            case WM_CLOSE:
                flag_quit = 1;//pediram para eu fechar
                break;

            default:;
                TranslateMessage (&msg);
                DispatchMessage (&msg);
        }
    }

    return flag_quit;
}


DWORD WINAPI thread_gui( void *ptr )
{

    //wait a bit before actually launching the gui
    //unity seems to do some funky stuff with windows that
    //its process owns at startup
    Sleep(8000);

    //creates the form
    create_gui();

    //do the message pump stuff
    while(!gui_doevents())
    {
        Sleep(5);
    }

    return 0;
}

int launch_gui_thread()
{
    HANDLE hthread;
    HANDLE dwThreadId;


    hthread = CreateThread(
        NULL,                   // default security attributes
        0,                      // use default stack size
        thread_gui,       // thread function name
        0,          // argument to thread function
        0,                      // use default creation flags
        &dwThreadId);   // returns the thread identifier
}
