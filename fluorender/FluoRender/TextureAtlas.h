//
// Created by snorri on 7.10.2018.
//

#ifndef VULKAN_SPRITES_TEXTUREATLAS_H
#define VULKAN_SPRITES_TEXTUREATLAS_H

#include "VulkanDevice.hpp"
#include "ITexture.h"
#include "AreaAllocator.h"
#include "AtlasTexture.h"

class TextureAtlas : public ITexture {

public:
    TextureAtlas();

    ~TextureAtlas();

    void Initialize(vks::VulkanDevice *pDevice, uint32_t width, uint32_t height);

    std::shared_ptr<AtlasTexture> Add(uint32_t width, uint32_t height, uint8_t *pixels);

	void FreeArea();

    VkDescriptorSet GetDescriptorSet() override;
    TextureWindow GetTextureWindow() override;
    int GetWidth() override;
    int GetHeight() override;

	std::shared_ptr<vks::VTexture> m_tex;

protected:
    uint32_t m_width;
    uint32_t m_height;
    vks::VulkanDevice *m_device;
    AreaAllocator m_allocator;

protected:
};


#endif //VULKAN_SPRITES_TEXTUREATLAS_H
