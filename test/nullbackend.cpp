// A d3d9_backend that loads but can't start, like DXVK without a Vulkan 1.3 driver: Direct3DCreate9 returns nothing.
// tools\featuretest.ps1 points d3d9_backend at it; the mod must fall back to Windows' own Direct3D 9.
extern "C" __declspec(dllexport) void* __stdcall Direct3DCreate9(unsigned) { return nullptr; }
