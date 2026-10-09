# Credits

MHFZ Plus contains no code from other projects. It does stand on a lot of other people's work: the tools it was
built and analysed with, the projects that documented the game, and the teams that keep the game playable.

## The game and its community

- **Capcom** made Monster Hunter Frontier (2007–2019). Everything in the game is theirs.
- **The server teams**, among them MezeLounge and Rain, keep the game playable years after the official end.
  MHFZ Plus is developed and tested against their clients. Neither team has endorsed it.
- **[Erupe](https://github.com/ZeruLight/Erupe)**, the open-source MHF-Z server emulator (MIT), and its
  contributors made community servers possible.

## Tools used to build and analyse the frame-rate mod

| Tool | Used for | License |
|---|---|---|
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) (NSA) | static analysis of the 2019 client: frame limiter, render loop, camera | Apache-2.0 |
| [Capstone](https://www.capstone-engine.org/) | disassembly in the analysis scripts | BSD-3-Clause |
| [pefile](https://github.com/erocarrera/pefile) | reading PE files in the analysis scripts | MIT |
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (we used the copy that ships with [Windhawk](https://windhawk.net/)) | compiling the mod, launcher and test tools | Apache-2.0 with LLVM exception; mingw-w64 runtime: permissive licenses |
| C# compiler of the .NET Framework (part of Windows) | building the settings app | Microsoft |
| [DXVK](https://github.com/doitsujin/dxvk) (Philip Rebohle and contributors) | the bundled Direct3D 9 backend on Vulkan (unmodified 3.1.1, since MHFZ Plus 1.3); also used to test the mod | zlib |
| Python | analysis and test scripts | PSF |

Microsoft's Direct3D 9 documentation describes the API surface the mod hooks.

## Used for the English translation

The translation pipeline isn't part of this repository; it's credited here because the launcher applies its output.

| Project | Used for | License |
|---|---|---|
| [ReFrontier](https://github.com/Houmgaor/ReFrontier) (originally by mhvuze) | the game's file formats (encryption, compression) | see the project |
| [FrontierTextHandler](https://github.com/Houmgaor/FrontierTextHandler) (Houmgaor) | extracting and re-inserting game text | GPL-3.0 |
| [Gemma](https://ai.google.dev/gemma) (Google) and [Qwen](https://github.com/QwenLM) (Alibaba) | machine translation, with automated review passes | see each model's terms |

## Built with

- **[Sushii-dev](https://github.com/Sushii-dev)** led the project: the research, the rules and the structure of the
  work, the prompts that steered the agent, the in-game testing and diagnosis, and every decision on what shipped.
- **Claude** (Anthropic), through Claude Code, wrote most of the code and documentation under that direction.
  See [AI-USAGE.md](AI-USAGE.md).
