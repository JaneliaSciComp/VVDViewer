//
// Created by snorri on 7.10.2018.
//

#include "TextureAtlas.h"
#include <string>

TextureAtlas::TextureAtlas() : m_width(0), m_height(0), m_device(nullptr)
{}

TextureAtlas::~TextureAtlas() {
	m_tex.reset();
}

void TextureAtlas::Initialize(vks::VulkanDevice* pDevice, uint32_t width, uint32_t height) {
    m_device = pDevice;

    m_width = width;
    m_height = height;

    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	m_tex = m_device->GenTexture2D(format, VK_FILTER_LINEAR, m_width, m_height);
    m_allocator.Initialize(m_width, m_height);
}

int TextureAtlas::GetWidth() {
    return m_width;
}

int TextureAtlas::GetHeight() {
    return m_height;
}

std::shared_ptr<AtlasTexture> TextureAtlas::Add(uint32_t width, uint32_t height, uint8_t *pixels) {
    auto area = m_allocator.Allocate(width, height);
    if(!area) {
        return nullptr;
    }

    VkDeviceSize imageSize = width * height * 4;
	VkOffset2D offset;
	VkExtent2D extent;
	offset.x = area->x;
	offset.y = area->y;
	extent.width = width;
	extent.height = height;
	//the upload transitions the whole atlas image, which an in-flight frame may be
	//sampling; glyphs are cached, so this only happens on first rasterization
	m_device->WaitIdleAllFrameSlots();
	m_device->UploadSubTexture2D(m_tex, pixels, offset, extent);

    return std::make_shared<AtlasTexture>(this, area);
}

void TextureAtlas::FreeArea()
{
	m_allocator.Initialize(m_width, m_height);
}

VkDescriptorSet TextureAtlas::GetDescriptorSet() {
    return VK_NULL_HANDLE;
}

TextureWindow TextureAtlas::GetTextureWindow() {
    return TextureWindow{0.0f, 0.0f, 1.0f, 1.0f};
}

