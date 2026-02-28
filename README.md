==================================================
                RENDERING ENGINE
==================================================

A modern real-time rendering engine built with 
performance and scalability in mind.


====================
REQUIREMENTS
====================

Before compiling the project, make sure you have
the following installed and added to your system
Environment PATH:

- LLVM
- Ninja
- CMake


====================
BUILD INSTRUCTIONS
====================

Run the following commands:

cmake -B build -G Ninja
cmake --build build

The compiled binaries will be generated inside 
the "build" directory.


====================
ENGINE FEATURES
====================

[ Rendering ]
- Depth Buffer
- Frame Buffer
- Shadow Mapping
    * Cascaded Shadow Maps (CSM)
    * Directional Light Shadows

[ Lighting System ]
- Point Lights
- Spot Lights
- Directional Lights

[ Camera System ]
- Scene navigation system

[ Post Processing ]
- Coming Soon


==================================================

<img width="1916" height="1028" alt="image" src="https://github.com/user-attachments/assets/37464b5a-7638-4312-981f-6b330eca593f" />
