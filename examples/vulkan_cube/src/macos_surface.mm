// macOS surface creation helper (Objective-C++)
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#include <Cocoa/Cocoa.h>
#include <QuartzCore/CAMetalLayer.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#endif

extern "C" {
    // Get the Metal layer from a GLFW window (macOS only)
    void* getMetalLayer(GLFWwindow* window) {
#ifdef __APPLE__
        NSWindow* nswindow = glfwGetCocoaWindow(window);
        if (!nswindow) {
            return nullptr;
        }
        
        NSView* view = [nswindow contentView];
        if (!view) {
            return nullptr;
        }
        
        // Create CAMetalLayer if it doesn't exist
        if (![view.layer isKindOfClass:[CAMetalLayer class]]) {
            [view setLayer:[CAMetalLayer layer]];
            [view setWantsLayer:YES];
        }
        
        CAMetalLayer* metalLayer = (CAMetalLayer*)view.layer;
        return (__bridge void*)metalLayer;
#else
        (void)window; // Suppress unused parameter warning
        return nullptr;
#endif
    }
}

