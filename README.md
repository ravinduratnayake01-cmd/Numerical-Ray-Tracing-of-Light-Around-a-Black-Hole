# Numerical-Ray-Tracing-of-Light-Around-a-Black-Hole
The aim of this project is to develop a numerical ray-tracing engine capable of simulating the propagation of light in the gravitational field of a black hole using C++, by solving the geodesic equations, and using numerical integration to calculate the trajectories of individual light rays.

How to Run
1 Required files
The following files should be kept in the same directory:
• black hole.cpp
• geodesic.comp
The shader is loaded from the working directory at runtime. The Python analysis script
plot ring distortion.py should be run in the directory containing the resultant .ppm files.
2 Building
The program can be built with the compiler and libraries GLEW, GLFW, GLM, and OpenGL.
g++ black_hole.cpp -o black_hole.exe -lglew32 -lglfw3 -lopengl32 -lgdi32
3 Interactive use
Run the executable from the project directory:
black_hole.exe
The controls are:
• Left mouse button and drag: orbit the camera.
• Scroll wheel: zoom in and out.
• T: toggle the spacetime grid.
The scene can be modified by editing the object list in main. Object positions and radii are
given in units of rs. The disk radii are set in the DiskUBO initialisation, and the default camera
parameters are defined in struct Camera.
4 Measuring the ring radius
To perform the ring-radius measurement:
1. In Command Prompt:
for %d in (5 10 20 30 50 80 100) do black_hole.exe %d
2. In PowerShell:
foreach ($d in 5,10,20,30,50,80,100) { .\black_hole.exe $d }
