//
//  MetalCompatibility.m
//  wxVulkanTex
//
//  Created by takashi kawase on 5/9/19.
//
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

void makeViewMetalCompatible(void* handle)
{
    NSView* view = (NSView*)handle;
    assert([view isKindOfClass:[NSView class]]);
    
    if (![view.layer isKindOfClass:[CAMetalLayer class]])
    {
        [view setLayer:[CAMetalLayer layer]];
        [view setWantsLayer:YES];
    }
}

// Diagnostic: describe the view and its layer (used by the Qt canvas to log
// what MoltenVK is about to be handed).
void describeViewLayer(const char* tag, void* handle)
{
    NSObject* obj = (NSObject*)handle;
    if (![obj isKindOfClass:[NSView class]])
    {
        NSLog(@"[%s] handle %p is not an NSView (class %@)", tag, handle, obj ? NSStringFromClass([obj class]) : @"nil");
        return;
    }
    NSView* view = (NSView*)obj;
    NSLog(@"[%s] view %p class=%@ wantsLayer=%d inWindow=%d layer=%p layerClass=%@ isMetal=%d",
          tag, view, NSStringFromClass([view class]), (int)view.wantsLayer, view.window != nil,
          view.layer, view.layer ? NSStringFromClass([view.layer class]) : @"nil",
          (int)[view.layer isKindOfClass:[CAMetalLayer class]]);
}

static CAMetalLayer* findMetalLayer(CALayer* layer, int depth)
{
    if (!layer || depth > 4) return nil;
    if ([layer isKindOfClass:[CAMetalLayer class]]) return (CAMetalLayer*)layer;
    for (CALayer* sub in layer.sublayers)
    {
        CAMetalLayer* found = findMetalLayer(sub, depth + 1);
        if (found) return found;
    }
    return nil;
}

// Return the CAMetalLayer that backs the view, or NULL. Qt >= 6.10 wraps the
// view's real layer in a "QContainerLayer" whose content layer is the
// CAMetalLayer, so view.layer itself is not one; MoltenVK accepts a
// CAMetalLayer in place of the NSView when creating the surface.
void* metalLayerForView(void* handle)
{
    NSObject* obj = (NSObject*)handle;
    if ([obj isKindOfClass:[CAMetalLayer class]]) return handle;
    if (![obj isKindOfClass:[NSView class]]) return NULL;
    return findMetalLayer(((NSView*)obj).layer, 0);
}
