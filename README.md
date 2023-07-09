# openxr_loader.dll proxy

![alt text](https://github.com/aruametello/openxr_loader-dll-proxy/blob/main/screenshot-2.png?raw=true)

A proxy dll to Khronos openxr_loader.dll. (https://registry.khronos.org/OpenXR/specs/1.0/loader.html)

video description and demonstration: https://www.youtube.com/watch?v=WixaeDdFIWg

it exposes bilateral comunications between the XR aplication and the OpenXR api, allowing the user to change inputs and outputs to OpenXR.

the target here is to implement a setting (per game) of controller rotation offsets. i personally think every vr game should have this as an option the same way desktop mouse driven first person shooters have mouse sensitivity settings.



Demonstration:

https://raw.githubusercontent.com/aruametello/openxr_loader-dll-proxy/main/.github/images/video_demo_offsets.mp4


![embeded video demo](https://raw.githubusercontent.com/aruametello/openxr_loader-dll-proxy/main/.github/images/video_demo_offsets.mp4)

in short: "i will do my own settings, with blackjack and hookers".

This could serve as a starting point for a more complex tool to tweak everything between OpenXR and aplication, like OpenVR Advanced Settings does for OpenVR.

ideas that i desire to explore in the future:
 * filter inputs (remove shaky hands) under certains scenarios.
    - tiny input to the trigger finger would enable the filter.
    
 * OpenXR does have "full hand poses" support, could Virtual Desktop expose hand poses so i can inject it directly as an input?
 
 * Weird tweaks, like positional offsets to the controllers. (cant see use for it, but someone probably will)
