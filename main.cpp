#include "Application/Application.h"
#include "Hardware/CompositeDeviceManager.h"

int main(int argc, char* argv[]) {
    CompositeDeviceManager manager;
    Application app(argc, argv, manager);

    return app.run();
}
