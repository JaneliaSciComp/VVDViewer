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

#endif /* MetalCompatibility_h */
