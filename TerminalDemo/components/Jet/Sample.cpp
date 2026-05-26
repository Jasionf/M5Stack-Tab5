#ifdef __MBED_SAMPLE__
#include "Jet.hpp"

using namespace Renderer;

Scene *scene;
Camera *camera;
DirectionalLight *dirLight;
AmbientLight *ambLight;
Material *redMaterial;
Material *greenMaterial;
Material *blueMaterial;
Object *cube;
Object *plane;
Object *sphere;

void setup()
{
    // Initialize framebuffer and screen dimensions
    uint16_t *framebuffer = new uint16_t[800 * 600];
    int screenWidth = 800;
    int screenHeight = 600;

    // Create scene
    scene = new Scene(framebuffer, screenWidth, screenHeight);

    // Create and set camera
    camera = new Camera();
    camera->setPosition(0, 0, -500);
    camera->setRotation(0, 0, 0);
    camera->setFOV(90, screenWidth);
    scene->setCamera(camera);

    // Create and set lights
    dirLight = new DirectionalLight({-90, 0}, {255, 255, 255});
    ambLight = new AmbientLight({50, 50, 50});
    scene->setDirectionalLight(dirLight);
    scene->setAmbientLight(ambLight);

    // Create materials
    redMaterial = new Material(0xF800);   // Red
    greenMaterial = new Material(0x07E0); // Green
    blueMaterial = new Material(0x001F);  // Blue

    // Create objects
    cube = Primitives::createCube(100, 100, 100, redMaterial);
    plane = Primitives::createPlane(500, 500, greenMaterial);
    sphere = Primitives::createSphere(50, 16, blueMaterial);

    // Set object transformations
    cube->translate(0, 0, 200);
    plane->translate(0, -100, 0);
    sphere->translate(200, 0, 0);

    // Add objects to scene
    scene->addObject(cube);
    scene->addObject(plane);
    scene->addObject(sphere);
}

void loop()
{
    // Rotate cube and sphere
    cube->rotate(1, 1, 0);
    sphere->rotate(0, 1, 1);

    // Render scene
    scene->render();
}
#endif
