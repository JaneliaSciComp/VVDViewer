//
//  MetalCompatibility.h
//  wxVulkanTex
//
//  Created by takashi kawase on 5/9/19.
//

#ifndef MetalCompatibility_h
#define MetalCompatibility_h

extern "C" void makeViewMetalCompatible(void* handle);
extern "C" void describeViewLayer(const char* tag, void* handle);
extern "C" void* metalLayerForView(void* handle);
// Diagnostic: the CAMetalLayer behind the view (metalLayerForView) as
// v[0..7] = drawableSize w, h; bounds w, h (points); contentsScale;
// maximumDrawableCount; the view's bounds w, h (points, 0 when handle is not
// an NSView). Returns 0 when there is no such layer.
extern "C" int metalLayerSizes(void* handle, double* v);
// Qt >= 6.10 keeps the CAMetalLayer as the content of a QContainerLayer and
// lays it out to the view's size only in a later layout pass, which waits
// while the main thread draws frames: a surface rebuilt right after a resize
// got the old size. Sets the CAMetalLayer's frame to the view's bounds when
// it is such a content layer; returns 1 when the frame changed.
extern "C" int syncMetalLayerToView(void* handle);
// MoltenVK 1.4.0 / 1.4.1 (MVKSwapchain::initCAMetalLayer) write a new
// swapchain's drawableSize and only then flush the old swapchain's unfinished
// presentations by setting drawableSize to 1x1, which leaves the layer at 1x1
// whenever an earlier frame is not on screen yet (MoltenVK main flushes
// first). Sets the drawableSize of the view's CAMetalLayer to w x h when it
// differs; returns 1 when it had to.
extern "C" int restoreMetalDrawableSize(void* handle, unsigned int w, unsigned int h);

#endif /* MetalCompatibility_h */
