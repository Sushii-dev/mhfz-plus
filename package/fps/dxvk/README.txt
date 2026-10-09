DXVK 3.1.1 (32-bit d3d9.dll), unmodified, from the official release:
https://github.com/doitsujin/dxvk/releases/tag/v3.1.1

DXVK runs Direct3D 9 on Vulkan. MHFZ Plus uses it, when "Graphics backend: DXVK" is chosen, because it costs much
less CPU time per draw than Windows' own Direct3D 9, so more in-between frames fit. It needs a graphics card and
driver with Vulkan 1.3 (most cards from 2016 on). If it can't start, MHFZ Plus uses Windows' own Direct3D 9.

License: zlib/libpng, see LICENSE.txt. DXVK is made by Philip Rebohle and contributors.
