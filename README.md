# openxr_loader.dll proxy

![screenshot](https://github.com/aruametello/openxr_loader-dll-proxy/blob/main/screenshot-2.png?raw=true)

A proxy dll to Khronos openxr_loader.dll. (https://registry.khronos.org/OpenXR/specs/1.0/loader.html)

video description and demonstration here: https://www.youtube.com/watch?v=WixaeDdFIWg

it exposes bilateral comunications between the XR aplication and the OpenXR api, allowing the user to change inputs and outputs to OpenXR.

the target here is to implement a setting (per game) of controller rotation and position offsets. 
I personally think every vr game should have this as an option the same way desktop mouse driven first person shooters have mouse sensitivity settings.


https://github.com/aruametello/openxr_loader-dll-proxy/assets/25987323/0478a50b-3348-48ee-9340-1d7ea83c6feb



in short: "i will do my own settings, with blackjack and hookers".

This could serve as a starting point for a more complex tool to tweak everything between OpenXR and aplication, like OpenVR Advanced Settings does for OpenVR.

ideas that i desire to explore in the future:
 * filter inputs (remove shaky hands) under certains scenarios.
    - tiny input to the trigger finger would enable the filter.
    
 * OpenXR does have "full hand poses" support, could we use those for anything interesting? (perhalps injecting an external data source or using REALLY custom hand posess)
 


Compiling:

sorry but I havent provided a makefile yet.

this is a pure C application that can be compiled with mingw-w64 from the msys2 packages, the OpenXR headers are required (package is mingw-w64-x86_64-openxr-sdk ?) 

the makefile would be something that does this:
gcc.exe -Wall -fexceptions -O2 -Wall -DBUILD_DLL -march=core2 -Wall -std=c11 -m64 -ID:\compiladores\msys64\mingw64\include -c D:\projetos\dll_proxy_gcc\gui.c -o obj\Release\gui.o
gcc.exe -Wall -fexceptions -O2 -Wall -DBUILD_DLL -march=core2 -Wall -std=c11 -m64 -ID:\compiladores\msys64\mingw64\include -c D:\projetos\dll_proxy_gcc\main.c -o obj\Release\main.o
gcc.exe -Wall -fexceptions -O2 -Wall -DBUILD_DLL -march=core2 -Wall -std=c11 -m64 -ID:\compiladores\msys64\mingw64\include -c D:\projetos\dll_proxy_gcc\Quaternion.c -o obj\Release\Quaternion.o
gcc.exe -shared   -Wl,--dll -LD:\compiladores\msys64\mingw64\lib obj\Release\gui.o obj\Release\main.o obj\Release\Quaternion.o  -o "C:\Program Files (x86)\Steam\steamapps\common\VAIL\Engine\Binaries\ThirdParty\OpenXR\win64\openxr_loader.dll" -s -LD D:\projetos\dll_proxy_gcc\openxr_loader.def -static-libstdc++ -static-libgcc -m64  -lgdi32 -lkernel32 -lpsapi -luser32

adjust your paths acordingly and perhalps drop the compiled .dll into the required folder of game you are using to test with. (i tested mostly with Bonelab and Vail VR)


