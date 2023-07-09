# openxr_loader.dll proxy

![screenshot](https://github.com/aruametello/openxr_loader-dll-proxy/blob/main/screenshot-2.png?raw=true)

A proxy dll to Khronos openxr_loader.dll. (https://registry.khronos.org/OpenXR/specs/1.0/loader.html)

video description and demonstration here: https://www.youtube.com/watch?v=WixaeDdFIWg

it exposes bilateral comunications between the XR aplication and the OpenXR api, allowing the user to change inputs and outputs to OpenXR.

the target here is to implement a setting (per game) of controller rotation and position offsets. 
I personally think every vr game should have this as an option the same way desktop mouse driven first person shooters have mouse sensitivity settings.

in short: "i will do my own settings, with blackjack and hookers".

This could serve as a starting point for a more complex tool to tweak everything between OpenXR and aplication, like OpenVR Advanced Settings does for OpenVR.

ideas that i desire to explore in the future:
 * filter inputs (remove shaky hands) under certains scenarios.
    - tiny input to the trigger finger would enable the filter.
    
 * OpenXR does have "full hand poses" support, could we use those for anything interesting? (perhalps injecting an external data source or using REALLY custom hand posess)
 

