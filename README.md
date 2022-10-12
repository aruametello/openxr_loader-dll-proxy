# openxr_loader.dll proxy

A proxy dll to Kronos openxr_loader.dll.

it exposes bilateral comunications between the XR aplication and the OpenXR api, allowing the user to change inputs and outputs to OpenXR.

the target here is to implement a setting (per game) of controller rotation offsets. i personally think every vr game should have this as an option the same way desktop mouse driven first person shooters have mouse sensitivity settings.

in short: "i will do my own settings, with blackjack and hookers".

This could serve as a starting point for a more complex tool to tweak everything between OpenXR and aplication, like OpenVR Advanced Settings does for OpenVR.

ideas that i desire to explore in the future:
 * filter inputs (remove shaky hands) under certains scenarios.
    - tiny input to the trigger finger would enable the filter.
    
 * OpenXR does have "full hand poses" support, could Virtual Desktop expose hand poses so i can inject it directly as an input?
 
 * Weird tweaks, like positional offsets to the controllers. (cant see use for it, but someone probably will)
