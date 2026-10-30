#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include <chrono>
#include <GL/glew.h>   
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstdlib>

using namespace glm;
using namespace std;
using Clock = std::chrono::high_resolution_clock;
bool Gravity = false;
bool showGrid = false;

double c = 299792458.0;
double G = 6.67430e-11;

struct Camera {
    vec3 target = vec3(0.0f, 0.0f, 0.0f);
    float radius = 1.9e11f;
    float minRadius = 3e10f, maxRadius = 1e12f;

    float azimuth = 0.0f;
    // float elevation = 1.35f;
    float elevation = M_PI / 2.0f;

    float orbitSpeed = 0.01f;
    float panSpeed = 0.01f;
    double zoomSpeed = 25e9f;

    bool dragging = false;
    bool panning = false;
    bool moving = false;
    double lastX = 0.0, lastY = 0.0;

    float fovY = 60.0f * float(M_PI) / 180.0f;

    vec3 position() const {
        float clampedElevation = glm::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
        return vec3(
            radius * sin(clampedElevation) * cos(azimuth),
            radius * cos(clampedElevation),
            radius * sin(clampedElevation) * sin(azimuth)
        );
    }
    void update() {
        target = vec3(0.0f, 0.0f, 0.0f);
        if(dragging | panning) {
            moving = true;
        } else {
            moving = false;
        }
    }

    void processMouseMove(double x, double y) {
        float dx = float(x - lastX);
        float dy = float(y - lastY);

        if (dragging && panning) {
        }
        else if (dragging && !panning) {
            azimuth   += dx * orbitSpeed;
            elevation -= dy * orbitSpeed;
            elevation = glm::clamp(elevation, 0.01f, float(M_PI) - 0.01f);
        }

        lastX = x;
        lastY = y;
        update();
    }
    void processMouseButton(int button, int action, int mods, GLFWwindow* win) {
        if (button == GLFW_MOUSE_BUTTON_LEFT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
            if (action == GLFW_PRESS) {
                dragging = true;
                panning = false;
                glfwGetCursorPos(win, &lastX, &lastY);
            } else if (action == GLFW_RELEASE) {
                dragging = false;
                panning = false;
            }
        }
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            if (action == GLFW_PRESS) {
                Gravity = true;
            } else if (action == GLFW_RELEASE) {
                Gravity = false;
            }
        }
    }
    void processScroll(double xoffset, double yoffset) {
        radius *= (float)pow(1.1, -yoffset);
        radius = glm::clamp(radius, minRadius, maxRadius);
        update();
    }
    void processKey(int key, int scancode, int action, int mods) {
        if (action == GLFW_PRESS && key == GLFW_KEY_G) {
            Gravity = !Gravity;
            cout << "[INFO] Gravity turned " << (Gravity ? "ON" : "OFF") << endl;
        }
        if (action == GLFW_PRESS && key == GLFW_KEY_T) showGrid = !showGrid;   // <- here
    }
};

Camera camera; 

struct CameraUBO {
    vec3 camPos;     float _pad0;
    vec3 camRight;   float _pad1;
    vec3 camUp;      float _pad2;
    vec3 camForward; float _pad3;
    float tanHalfFov;
    float aspect;
    int   moving;
    int   _pad4;
};
struct DiskUBO { float r1, r2, num, thickness; };
struct ObjectsUBO {
    int  numObjects; int _pad[3];
    vec4 posRadius[16];
    vec4 color[16];
    vec4 mass[16];         
};
static_assert(sizeof(CameraUBO)  == 80,  "CameraUBO must match std140");
static_assert(sizeof(ObjectsUBO) == 784, "ObjectsUBO must match std140");

GLuint makeUBO(GLuint binding, size_t size) {
    GLuint ubo;
    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, ubo);
    return ubo;
}



struct Engine {
    GLFWwindow* window;
    GLuint texture = 0, readFBO = 0, computeProgram = 0;
    GLuint camUBO = 0, diskUBO = 0, objUBO = 0;;

    int WIDTH = 800;
    int HEIGHT = 600;
    float width = 1e11;
    float height = 7.5e10;

    Engine() {
        if (!glfwInit()) {
            cerr << "glfw failed init" << endl;
            exit(EXIT_FAILURE);
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole", nullptr, nullptr);
        if (!window) {
            cerr << "window creation failed" << endl;
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        glfwMakeContextCurrent(window);
        if (glewInit() != GLEW_OK) {
            cerr << "glew failed init" << endl;
            exit(EXIT_FAILURE);
        }

        glViewport(0, 0, WIDTH, HEIGHT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-width/2, width/2, -height/2, height/2, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    void run() { glClear(GL_COLOR_BUFFER_BIT); }

    void initCompute(const char* path) {
        ifstream file(path);
        if (!file) { cerr << "cannot open " << path << endl; exit(EXIT_FAILURE); }
        stringstream ss; ss << file.rdbuf();
        string src = ss.str();
        const char* csrc = src.c_str();

        GLint ok; char log[4096];
        GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
        glShaderSource(sh, 1, &csrc, nullptr);
        glCompileShader(sh);
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(sh, sizeof(log), nullptr, log); cerr << log << endl; exit(EXIT_FAILURE); }

        computeProgram = glCreateProgram();
        glAttachShader(computeProgram, sh);
        glLinkProgram(computeProgram);
        glGetProgramiv(computeProgram, GL_LINK_STATUS, &ok);
        if (!ok) { glGetProgramInfoLog(computeProgram, sizeof(log), nullptr, log); cerr << log << endl; exit(EXIT_FAILURE); }
        glDeleteShader(sh);

        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        glGenFramebuffers(1, &readFBO);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

        camUBO  = makeUBO(1, sizeof(CameraUBO));
        diskUBO = makeUBO(2, sizeof(DiskUBO));
        objUBO  = makeUBO(3, sizeof(ObjectsUBO));
    }

    void dispatchCompute(const Camera& cam) {
        vec3 pos     = cam.position();
        vec3 forward = normalize(cam.target - pos);
        vec3 right   = normalize(cross(forward, vec3(0, 1, 0)));
        vec3 up      = cross(right, forward);

        CameraUBO d{};
        d.camPos = pos;  d.camRight = right;  d.camUp = up;  d.camForward = forward;
        d.tanHalfFov = tan(cam.fovY * 0.5f);
        d.aspect     = float(WIDTH) / float(HEIGHT);
        d.moving     = cam.moving ? 1 : 0;

        glBindBuffer(GL_UNIFORM_BUFFER, camUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(d), &d);

        glUseProgram(computeProgram);
        glDispatchCompute((WIDTH + 15) / 16, (HEIGHT + 15) / 16, 1);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }

    void present() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
};


struct BlackHole {
    vec3 position;
    double mass;
    double radius;
    double r_s;

    BlackHole(vec3 pos, double m) : position(pos), mass(m) {r_s = 2.0 * G * mass / (c*c);}
    bool Intercept(float px, float py, float pz) const {
        double dx = double(px) - double(position.x);
        double dy = double(py) - double(position.y);
        double dz = double(pz) - double(position.z);
        double dist2 = dx * dx + dy * dy + dz * dz;
        return dist2 < r_s * r_s;
    }
};
BlackHole SagA(vec3(0.0f, 0.0f, 0.0f), 8.54e36);

const double ESCAPE_R = 1e13;

struct Grid {
    GLuint vao = 0, vbo = 0, ebo = 0, prog = 0;
    int count = 0;

    void init() {
        const char* vs = R"(#version 330 core
            layout(location = 0) in vec3 aPos;
            uniform mat4 viewProj;
            void main() { gl_Position = viewProj * vec4(aPos, 1.0); })";
        const char* fs = R"(#version 330 core
            out vec4 c;
            void main() { c = vec4(0.3, 0.6, 1.0, 0.35); })";
        GLuint v = glCreateShader(GL_VERTEX_SHADER);   glShaderSource(v, 1, &vs, nullptr); glCompileShader(v);
        GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, nullptr); glCompileShader(f);
        prog = glCreateProgram(); glAttachShader(prog, v); glAttachShader(prog, f); glLinkProgram(prog);
        glDeleteShader(v); glDeleteShader(f);

        const int N = 60; const float half = 40.0f;
        vector<vec3> verts; vector<GLuint> idx;
        for (int j = 0; j <= N; ++j)
            for (int i = 0; i <= N; ++i) {
                float x = -half + 2 * half * i / N, z = -half + 2 * half * j / N;
                float r = std::max(1.0f, std::sqrt(x * x + z * z));
                verts.push_back(vec3(x, 2.0f * std::sqrt(r - 1.0f) - 8.0f, z));
            }
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                GLuint k = j * (N + 1) + i;
                idx.insert(idx.end(), { k, k + 1, k, k + N + 1 });
            }
        count = (int)idx.size();

        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(vec3), verts.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), nullptr);
        glBindVertexArray(0);
    }

    void draw(const Camera& cam, float aspect) {
        mat4 view = lookAt(cam.position() / float(SagA.r_s), vec3(0), vec3(0, 1, 0));
        mat4 proj = perspective(cam.fovY, aspect, 0.1f, 1000.0f);
        mat4 vp = proj * view;
        glUseProgram(prog);
        glUniformMatrix4fv(glGetUniformLocation(prog, "viewProj"), 1, GL_FALSE, &vp[0][0]);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(vao);
        glDrawElements(GL_LINES, count, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
    }
};
Grid grid;

void setupCameraCallbacks(GLFWwindow* window) {
    glfwSetCursorPosCallback(window, [](GLFWwindow*, double x, double y) {
        camera.processMouseMove(x, y);
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* w, int b, int a, int m) {
        camera.processMouseButton(b, a, m, w);
    });
    glfwSetScrollCallback(window, [](GLFWwindow*, double xo, double yo) {
        camera.processScroll(xo, yo);
    });
    glfwSetKeyCallback(window, [](GLFWwindow*, int k, int s, int a, int m) {
        camera.processKey(k, s, a, m);
    });
}

//void saveScreenshot(const char* name, int W, int H) {
    //vector<unsigned char> px(W * H * 3);
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);
    //glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    //ofstream f(name, ios::binary);
    //if (!f) { cerr << "[ERROR] cannot open " << name << " for writing" << endl; return; }
    //f << "P6\n" << W << " " << H << "\n255\n";
    //for (int y = H - 1; y >= 0; --y) f.write((char*)&px[y * W * 3], W * 3);
    //cout << "[INFO] saved " << name << endl;
//}

int main(int argc, char** argv) {
    //cout << "[INFO] argc = " << argc << endl;
    Engine engine;
    engine.initCompute("geodesic.comp");
    setupCameraCallbacks(engine.window);
        grid.init();

    DiskUBO disk{ float(3.0 * SagA.r_s), float(12.0 * SagA.r_s), 2.0f, 1e9f };
    //DiskUBO disk{ 0.0f, 0.0f, 2.0f, 1e9f };
    glBindBuffer(GL_UNIFORM_BUFFER, engine.diskUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(disk), &disk);

    // Background objects (up to 16)
    struct Obj { vec3 pos; float radius; vec3 color; };
    const vector<Obj> OBJECTS = {
        { vec3(-20.0f,  0.0f, 0.0f), 2.0f, vec3(0.2f, 0.2f, 0.6f) },   // axis
        { vec3(-50.0f,  0.0f, 5.0f), 4.0f, vec3(0.6f, 0.2f, 0.2f) },
        //{ vec3(-60.0f,  0.0f, -15.0f), 5.0f, vec3(0.2f, 0.6f, 0.2f) },
    //const float D = (argc > 1) ? float(atof(argv[1])) : 30.0f;   // distance behind the hole, in r_s
    //const vector<Obj> OBJECTS = {
        //{ vec3(-D, 0.0f, 0.0f), 1.0f, vec3(0.2f, 0.6f, 1.0f) },
    };

    ObjectsUBO objs{};
    objs.numObjects = (int)std::min<size_t>(OBJECTS.size(), 16);
    for (int i = 0; i < objs.numObjects; ++i) {
        objs.posRadius[i] = vec4(OBJECTS[i].pos * float(SagA.r_s), OBJECTS[i].radius * float(SagA.r_s));
        objs.color[i]     = vec4(OBJECTS[i].color, 1.0f);
    }
    glBindBuffer(GL_UNIFORM_BUFFER, engine.objUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(objs), &objs);

    while (!glfwWindowShouldClose(engine.window)) {
        engine.dispatchCompute(camera);
        engine.present();
                if (showGrid) grid.draw(camera, float(engine.WIDTH) / engine.HEIGHT);
                //if (argc > 1) {
                    //saveScreenshot(("ring_" + string(argv[1]) + ".ppm").c_str(), engine.WIDTH, engine.HEIGHT);
                    //break;
                //}
        glfwSwapBuffers(engine.window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

