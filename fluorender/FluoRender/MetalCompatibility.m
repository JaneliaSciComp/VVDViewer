//
//  MetalCompatibility.m
//  wxVulkanTex
//
//  Created by takashi kawase on 5/9/19.
//
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

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

int metalLayerSizes(void* handle, double* v)
{
    CAMetalLayer* layer = (CAMetalLayer*)metalLayerForView(handle);
    if (!layer || !v) return 0;
    v[0] = layer.drawableSize.width;
    v[1] = layer.drawableSize.height;
    v[2] = layer.bounds.size.width;
    v[3] = layer.bounds.size.height;
    v[4] = layer.contentsScale;
    v[5] = layer.maximumDrawableCount;
    v[6] = v[7] = 0.0;
    if ([(NSObject*)handle isKindOfClass:[NSView class]])
    {
        v[6] = ((NSView*)handle).bounds.size.width;
        v[7] = ((NSView*)handle).bounds.size.height;
    }
    return 1;
}

int syncMetalLayerToView(void* handle)
{
    NSObject* obj = (NSObject*)handle;
    if (![obj isKindOfClass:[NSView class]]) return 0;
    NSView* view = (NSView*)obj;
    CALayer* container = view.layer;
    CAMetalLayer* layer = (CAMetalLayer*)metalLayerForView(handle);
    // only a content layer inside the view's own layer (the wx view's layer
    // is the CAMetalLayer itself, sized by AppKit)
    if (!container || !layer || layer == (CAMetalLayer*)container || layer.superlayer != container) return 0;
    // what QContainerLayer's layout does (content frame = container bounds),
    // with the view's current size
    const CGRect want = CGRectMake(container.bounds.origin.x, container.bounds.origin.y,
        view.bounds.size.width, view.bounds.size.height);
    if (want.size.width <= 0.0 || want.size.height <= 0.0 || CGRectEqualToRect(layer.frame, want)) return 0;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    layer.frame = want;
    [CATransaction commit];
    return 1;
}

int restoreMetalDrawableSize(void* handle, unsigned int w, unsigned int h)
{
    CAMetalLayer* layer = (CAMetalLayer*)metalLayerForView(handle);
    if (!layer || w == 0 || h == 0) return 0;
    const CGSize want = CGSizeMake(w, h);
    if (CGSizeEqualToSize(layer.drawableSize, want)) return 0;
    layer.drawableSize = want;
    return 1;
}
