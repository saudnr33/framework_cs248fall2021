# framework_cs248fall2021

1. Set up environment:
1.1: Install Emscripten: 
- Following this link to install Emscripten:
https://emscripten.org/docs/getting_started/downloads.html

- Note: Please check Platform-specific notes before installation
1.2: Add environment path: If you use Windows, please perform this step:
Add these paths to environment:
path\to\emsdk: Path to folder that you download emsdk
path\to\emsdk\upstream\emscripten
path\to\emsdk\node\version_of_node\bin

1.3: Install cmake:
https://cmake.org/install/

1.4: If you use Windows, please install Ninja, and add path to Ninja to environment:
https://github.com/rwols/CMakeBuilder/wiki/Ninja-for-Windows-Installation-Instructions

1.5: Install Google Chrome Canary:
https://www.google.com/chrome/canary/
After finish installation, please enable the flag: --enable-unsafe-webgpu

2. Build source code:
2.1 Native build:
- In the folder of framework, run these commands:
    mkdir native-build
    cd native-build
    cmake ..
    cmake --build .
- If you use Windows and Visual Studio 2019, you can see .sln file and Debug folder in native-build folder:
Copy dawn_native.dll, dawn_platform.dll, dawn_proc.dll from source_code_folder/lib/dawn/bin/win/x64/Debug to your Debug folder.
Open .sln file
Set framework_cs248fall2021 project to StartUp Project
Compile and Run, you will see a blue window.
- If you use MacOS, you can see excutable file in native-build folder, run it and you will see see a blue window.

2.2 Web build
- In the folder of framework, run these commands:
    mkdir web-build
    cd web-build
    emcmake cmake ..
    cmake --build .
- You can see .html file in the web-build folder
- Go to web-build folder, run command:
    python -m http.server
- Open Google Chrome Canary, on the tab page, type http://localhost:8000/
You can see your .html file, click it, and you can see a blue page.

