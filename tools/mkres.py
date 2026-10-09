"""Builds Windows resource files (.res) without a resource compiler: icon, version info and manifest.
The linker (ld.lld) takes .res files directly.

    python tools/mkres.py            -> build/launcher.res, build/mhfz-plus.ico, build/fps.res
"""
import io
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
VERSION = (1, 2, 0, 0)

RT_ICON, RT_GROUP_ICON, RT_VERSION, RT_MANIFEST = 3, 14, 16, 24


def res_entry(rtype, rid, data, lang=0x0409, flags=0x1030):
    hdr = struct.pack("<II", len(data), 32) + struct.pack("<HH", 0xFFFF, rtype) + struct.pack("<HH", 0xFFFF, rid)
    hdr += struct.pack("<IHHII", 0, flags, lang, 0, 0)
    pad = b"\0" * ((4 - len(data) % 4) % 4)
    return hdr + data + pad


def res_file(entries):
    empty = struct.pack("<II", 0, 32) + struct.pack("<HHHH", 0xFFFF, 0, 0xFFFF, 0) + struct.pack("<IHHII", 0, 0, 0, 0, 0)
    return empty + b"".join(entries)


# ------------------------------------------------------------------------------------------------ icon
def draw_icon(size):
    s = size * 4  # supersample
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = s // 6
    # warm dusk gradient square (Frontier orange into deep red-brown)
    grad = Image.new("RGBA", (s, s))
    gd = ImageDraw.Draw(grad)
    for y in range(s):
        t = y / (s - 1)
        c = (int(232 - 120 * t), int(120 - 80 * t), int(40 - 10 * t), 255)
        gd.line([(0, y), (s, y)], fill=c)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], r, fill=255)
    im.paste(grad, (0, 0), mask)
    font_path = r"C:\Windows\Fonts\segoeuib.ttf"
    if not os.path.exists(font_path):
        font_path = r"C:\Windows\Fonts\arialbd.ttf"
    fz = ImageFont.truetype(font_path, int(s * 0.78))
    # "Z" with a small "+" up right
    bb = d.textbbox((0, 0), "Z", font=fz)
    zw, zh = bb[2] - bb[0], bb[3] - bb[1]
    zx = (s - zw) // 2 - bb[0] - s // 14
    zy = (s - zh) // 2 - bb[1] + s // 40
    d.text((zx + s // 60, zy + s // 60), "Z", font=fz, fill=(60, 15, 5, 160))
    d.text((zx, zy), "Z", font=fz, fill=(255, 246, 228, 255))
    pw = max(s // 16, 2)
    cx, cy, pl = int(s * 0.80), int(s * 0.22), int(s * 0.12)
    d.rectangle([cx - pl, cy - pw // 2, cx + pl, cy + pw // 2], fill=(255, 246, 228, 255))
    d.rectangle([cx - pw // 2, cy - pl, cx + pw // 2, cy + pl], fill=(255, 246, 228, 255))
    return im.resize((size, size), Image.LANCZOS)


def dib(im):
    """32-bit BGRA DIB as stored in icons: BITMAPINFOHEADER (double height), bottom-up pixels, AND mask."""
    w, h = im.size
    hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    px = im.tobytes("raw", "BGRA")
    rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h)]
    xor = b"".join(reversed(rows))
    mask_row = ((w + 31) // 32) * 4
    andm = b"\0" * (mask_row * h)
    return hdr + xor + andm


def icon_images():
    out = []
    for size in (16, 20, 24, 32, 40, 48, 64, 256):
        im = draw_icon(size)
        if size >= 256:
            b = io.BytesIO()
            im.save(b, "PNG")
            data = b.getvalue()
        else:
            data = dib(im)
        out.append((size, data))
    return out


def icon_resources(images, first_id=1):
    entries = []
    grp = struct.pack("<HHH", 0, 1, len(images))
    for i, (size, data) in enumerate(images):
        rid = first_id + i
        entries.append(res_entry(RT_ICON, rid, data))
        wb = 0 if size >= 256 else size
        grp += struct.pack("<BBBBHHIH", wb, wb, 0, 0, 1, 32, len(data), rid)
    entries.append(res_entry(RT_GROUP_ICON, 1, grp))
    return entries


def ico_file(images):
    out = struct.pack("<HHH", 0, 1, len(images))
    off = 6 + 16 * len(images)
    body = b""
    for size, data in images:
        wb = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", wb, wb, 0, 0, 1, 32, len(data), off + len(body))
        body += data
    return out + body


# ------------------------------------------------------------------------------------------------ version info
def _wstr(s):
    return (s + "\0").encode("utf-16-le")


def _pad4(b):
    return b + b"\0" * ((4 - len(b) % 4) % 4)


def vblock(key, value=b"", children=(), text=False):
    """One VERSIONINFO node: wLength, wValueLength, wType, szKey, pad, Value, pad, children."""
    head_key = _pad4(struct.pack("<HHH", 0, 0, 0) + _wstr(key))
    body = _pad4(head_key + value) if value else head_key
    for c in children:
        body = _pad4(body) + c
    vlen = len(value) // 2 if text else len(value)
    return struct.pack("<HHH", len(body), vlen, 1 if text else 0) + body[6:]


def version_resource(desc, internal, original, product):
    ver = VERSION
    ms, ls = (ver[0] << 16) | ver[1], (ver[2] << 16) | ver[3]
    fixed = struct.pack("<13I", 0xFEEF04BD, 0x00010000, ms, ls, ms, ls, 0x3F, 0, 0x40004, 1 if original.endswith(".exe") else 2, 0, 0, 0)
    vs = ".".join(str(x) for x in ver)
    strings = [("CompanyName", "MHFZ Plus"), ("FileDescription", desc), ("FileVersion", vs), ("InternalName", internal),
               ("LegalCopyright", "Free fan-made mod, not affiliated with Capcom"), ("OriginalFilename", original),
               ("ProductName", product), ("ProductVersion", vs)]
    table = vblock("040904B0", children=[vblock(k, _wstr(v), text=True) for k, v in strings])
    sfi = vblock("StringFileInfo", children=[table])
    vfi = vblock("VarFileInfo", children=[vblock("Translation", struct.pack("<HH", 0x0409, 1200))])
    return vblock("VS_VERSION_INFO", fixed, [sfi, vfi])


MANIFEST = """<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
  <assemblyIdentity type="win32" name="MHFZPlus.Launcher" version="1.0.0.0" processorArchitecture="x86"/>
  <description>MHFZ Plus launcher</description>
  <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
    <security><requestedPrivileges><requestedExecutionLevel level="asInvoker" uiAccess="false"/></requestedPrivileges></security>
  </trustInfo>
  <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
    <application>
      <supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/>
      <supportedOS Id="{1f676c76-80e1-4239-95bb-83d0f6d0da78}"/>
      <supportedOS Id="{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}"/>
      <supportedOS Id="{35138b9a-5d96-4fbd-8e2d-a2440225f93a}"/>
    </application>
  </compatibility>
  <application xmlns="urn:schemas-microsoft-com:asm.v3">
    <windowsSettings>
      <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
      <longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>
    </windowsSettings>
  </application>
  <dependency>
    <dependentAssembly>
      <assemblyIdentity type="win32" name="Microsoft.Windows.Common-Controls" version="6.0.0.0" processorArchitecture="x86"
                        publicKeyToken="6595b64144ccf1df" language="*"/>
    </dependentAssembly>
  </dependency>
</assembly>
"""


def main():
    os.makedirs(BUILD, exist_ok=True)
    images = icon_images()
    with open(os.path.join(BUILD, "mhfz-plus.ico"), "wb") as f:
        f.write(ico_file(images))
    draw_icon(256).save(os.path.join(BUILD, "mhfz-plus.png"))
    launcher = icon_resources(images)
    launcher.append(res_entry(RT_VERSION, 1, version_resource("MHFZ Plus launcher (English patch and high frame rate for Monster Hunter Frontier Z)",
                                                              "MHFZ-Plus", "MHFZ-Plus.exe", "MHFZ Plus")))
    launcher.append(res_entry(RT_MANIFEST, 1, MANIFEST.encode("utf-8")))
    with open(os.path.join(BUILD, "launcher.res"), "wb") as f:
        f.write(res_file(launcher))
    fps = [res_entry(RT_VERSION, 1, version_resource("MHFZ Plus frame-rate mod (Direct3D 9 proxy)", "mhfz-fps", "d3d9.dll", "MHFZ Plus"))]
    with open(os.path.join(BUILD, "fps.res"), "wb") as f:
        f.write(res_file(fps))
    print("wrote build/launcher.res, build/fps.res, build/mhfz-plus.ico")


if __name__ == "__main__":
    sys.exit(main())
