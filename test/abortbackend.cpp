// A d3d9_backend that ends the process while starting, like DXVK when it finds no Vulkan it can use (its uncaught
// error reaches the C runtime's abort). tools\featuretest.ps1 and tools/linuxtest.sh point d3d9_backend at it; the
// mod must catch that abort and fall back to Windows' own Direct3D 9.
#include <cstdlib>
extern "C" __declspec(dllexport) void* __stdcall Direct3DCreate9(unsigned) { abort(); }
