#include "TextRenderer.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
TextRenderer::TextRenderer()
    : device(VK_NULL_HANDLE), physicalDevice(VK_NULL_HANDLE),
      fontAtlasImage(VK_NULL_HANDLE), fontAtlasMemory(VK_NULL_HANDLE),
      fontAtlasView(VK_NULL_HANDLE), fontSampler(VK_NULL_HANDLE),
      descriptorPool(VK_NULL_HANDLE), descriptorSetLayout(VK_NULL_HANDLE),
      descriptorSet(VK_NULL_HANDLE), pipelineLayout(VK_NULL_HANDLE),
      pipeline(VK_NULL_HANDLE), vertexBuffer(VK_NULL_HANDLE),
      vertexMemory(VK_NULL_HANDLE), fontBuffer(nullptr), bitmapBuffer(nullptr),
      atlasWidth(512), atlasHeight(512), fontSize(32.0f), vertexCount(0) {}

TextRenderer::~TextRenderer() { cleanup(); }

bool TextRenderer::init(VkDevice device, VkPhysicalDevice physicalDevice,
                        VkCommandPool commandPool, VkQueue graphicsQueue,
                        const char *fontPath, float fontSize) {
  this->device = device;
  this->physicalDevice = physicalDevice;
  this->fontSize = fontSize;

  if (!loadFont(fontPath, fontSize)) {
    return false;
  }

  createFontAtlas();

  // Create texture image
  VkDeviceSize imageSize = atlasWidth * atlasHeight;

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  void *data;
  vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
  memcpy(data, bitmapBuffer, static_cast<size_t>(imageSize));
  vkUnmapMemory(device, stagingBufferMemory);

  // Create image
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = atlasWidth;
  imageInfo.extent.height = atlasHeight;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device, &imageInfo, nullptr, &fontAtlasImage) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create font atlas image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, fontAtlasImage, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = findMemoryType(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &fontAtlasMemory) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate font atlas memory!");
  }

  vkBindImageMemory(device, fontAtlasImage, fontAtlasMemory, 0);

  transitionImageLayout(commandPool, graphicsQueue, fontAtlasImage,
                        VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(commandPool, graphicsQueue, stagingBuffer, fontAtlasImage,
                    atlasWidth, atlasHeight);
  transitionImageLayout(commandPool, graphicsQueue, fontAtlasImage,
                        VK_FORMAT_R8_UNORM,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vkDestroyBuffer(device, stagingBuffer, nullptr);
  vkFreeMemory(device, stagingBufferMemory, nullptr);

  // Create image view
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = fontAtlasImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(device, &viewInfo, nullptr, &fontAtlasView) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create font atlas image view!");
  }

  // Create sampler
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  if (vkCreateSampler(device, &samplerInfo, nullptr, &fontSampler) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create font sampler!");
  }

  createDescriptorSetLayout();
  createDescriptorPool();
  createDescriptorSet();
  createVertexBuffer();

  return true;
}

void TextRenderer::cleanup() {
  if (device != VK_NULL_HANDLE) {
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }
    if (vertexBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(device, vertexBuffer, nullptr);
    }
    if (vertexMemory != VK_NULL_HANDLE) {
      vkFreeMemory(device, vertexMemory, nullptr);
    }
    if (descriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    }
    if (fontSampler != VK_NULL_HANDLE) {
      vkDestroySampler(device, fontSampler, nullptr);
    }
    if (fontAtlasView != VK_NULL_HANDLE) {
      vkDestroyImageView(device, fontAtlasView, nullptr);
    }
    if (fontAtlasImage != VK_NULL_HANDLE) {
      vkDestroyImage(device, fontAtlasImage, nullptr);
    }
    if (fontAtlasMemory != VK_NULL_HANDLE) {
      vkFreeMemory(device, fontAtlasMemory, nullptr);
    }
  }

  if (fontBuffer) {
    delete[] fontBuffer;
    fontBuffer = nullptr;
  }
  if (bitmapBuffer) {
    delete[] bitmapBuffer;
    bitmapBuffer = nullptr;
  }
}

bool TextRenderer::loadFont(const char *fontPath, float fontSize) {
  std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    std::cerr << "Failed to open font file: " << fontPath << std::endl;
    return false;
  }

  size_t fileSize = file.tellg();
  file.seekg(0);

  fontBuffer = new unsigned char[fileSize];
  file.read(reinterpret_cast<char *>(fontBuffer), fileSize);
  file.close();

  return true;
}

void TextRenderer::createFontAtlas() {
  bitmapBuffer = new unsigned char[atlasWidth * atlasHeight];
  memset(bitmapBuffer, 0, atlasWidth * atlasHeight);

  stbtt_fontinfo font;
  if (!stbtt_InitFont(&font, fontBuffer, 0)) {
    throw std::runtime_error("Failed to initialize font");
  }

  float scale = stbtt_ScaleForPixelHeight(&font, fontSize);

  int x = 0, y = 0;
  int maxHeight = 0;

  // Bake ASCII characters (32-126)
  for (int c = 32; c < 127; ++c) {
    int advance, lsb, x0, y0, x1, y1;
    stbtt_GetCodepointHMetrics(&font, c, &advance, &lsb);
    stbtt_GetCodepointBitmapBox(&font, c, scale, scale, &x0, &y0, &x1, &y1);

    int w = x1 - x0;
    int h = y1 - y0;

    if (x + w >= atlasWidth) {
      x = 0;
      y += maxHeight + 1;
      maxHeight = 0;
    }

    if (y + h >= atlasHeight) {
      throw std::runtime_error("Font atlas too small");
    }

    stbtt_MakeCodepointBitmap(&font, bitmapBuffer + x + y * atlasWidth, w, h,
                              atlasWidth, scale, scale, c);

    CharacterInfo charInfo;
    charInfo.ax = advance * scale;
    charInfo.ay = 0;
    charInfo.bw = static_cast<float>(w);
    charInfo.bh = static_cast<float>(h);
    charInfo.bl = static_cast<float>(x0);
    charInfo.bt = static_cast<float>(y0);
    charInfo.tx = static_cast<float>(x) / atlasWidth;
    charInfo.ty = static_cast<float>(y) / atlasHeight;

    characterMap[static_cast<char>(c)] = charInfo;

    if (h > maxHeight) {
      maxHeight = h;
    }

    x += w + 1;
  }
}

void TextRenderer::createDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  samplerLayoutBinding.binding = 0;
  samplerLayoutBinding.descriptorCount = 1;
  samplerLayoutBinding.descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerLayoutBinding.pImmutableSamplers = nullptr;
  samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &samplerLayoutBinding;

  if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                  &descriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

void TextRenderer::createDescriptorPool() {
  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = 1;

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1;

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }
}

void TextRenderer::createDescriptorSet() {
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &descriptorSetLayout;

  if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate descriptor set!");
  }

  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = fontAtlasView;
  imageInfo.sampler = fontSampler;

  VkWriteDescriptorSet descriptorWrite{};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = descriptorSet;
  descriptorWrite.dstBinding = 0;
  descriptorWrite.dstArrayElement = 0;
  descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrite.descriptorCount = 1;
  descriptorWrite.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

void TextRenderer::createVertexBuffer() {
  VkDeviceSize bufferSize =
      sizeof(TextVertex) * 10000; // Reserve space for vertices

  createBuffer(bufferSize,
               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               vertexBuffer, vertexMemory);
}

void TextRenderer::prepareText(const std::string &text, float x, float y,
                               float scale) {
  vertices.clear();

  float cursorX = x;
  float cursorY = y;

  for (char c : text) {
    if (characterMap.find(c) == characterMap.end()) {
      continue;
    }

    CharacterInfo &ch = characterMap[c];

    float xpos = cursorX + ch.bl * scale;
    float ypos = cursorY + ch.bt * scale;
    float w = ch.bw * scale;
    float h = ch.bh * scale;

    float texX = ch.tx;
    float texY = ch.ty;
    float texW = ch.bw / atlasWidth;
    float texH = ch.bh / atlasHeight;

    // Triangle 1
    vertices.push_back({{xpos, ypos}, {texX, texY}});
    vertices.push_back({{xpos + w, ypos}, {texX + texW, texY}});
    vertices.push_back({{xpos, ypos + h}, {texX, texY + texH}});

    // Triangle 2
    vertices.push_back({{xpos + w, ypos}, {texX + texW, texY}});
    vertices.push_back({{xpos + w, ypos + h}, {texX + texW, texY + texH}});
    vertices.push_back({{xpos, ypos + h}, {texX, texY + texH}});
    cursorX += ch.ax * scale;
  }

  vertexCount = static_cast<uint32_t>(vertices.size());
  updateVertexBuffer();
}

void TextRenderer::beginBatch() {
  batchEntries.clear();
  inBatch = true;
}

void TextRenderer::addText(const std::string &text, float x, float y,
                           float scale, float color[4]) {
  TextBatchEntry entry;
  memcpy(entry.color, color, sizeof(float) * 4);

  float cursorX = x;
  float cursorY = y;

  for (char c : text) {
    if (characterMap.find(c) == characterMap.end())
      continue;

    CharacterInfo &ch = characterMap[c];
    float xpos = cursorX + ch.bl * scale;
    float ypos = cursorY + ch.bt * scale;
    float w = ch.bw * scale;
    float h = ch.bh * scale;
    float texX = ch.tx;
    float texY = ch.ty;
    float texW = ch.bw / atlasWidth;
    float texH = ch.bh / atlasHeight;

    entry.vertices.push_back({{xpos, ypos}, {texX, texY}});
    entry.vertices.push_back({{xpos + w, ypos}, {texX + texW, texY}});
    entry.vertices.push_back({{xpos, ypos + h}, {texX, texY + texH}});
    entry.vertices.push_back({{xpos + w, ypos}, {texX + texW, texY}});
    entry.vertices.push_back(
        {{xpos + w, ypos + h}, {texX + texW, texY + texH}});
    entry.vertices.push_back({{xpos, ypos + h}, {texX, texY + texH}});

    cursorX += ch.ax * scale;
  }

  if (!entry.vertices.empty()) {
    batchEntries.push_back(std::move(entry));
  }
}

void TextRenderer::endBatch(VkCommandBuffer commandBuffer) {
  if (batchEntries.empty())
    return;

  // Collect all vertices into one big buffer upload
  std::vector<TextVertex> allVertices;
  for (auto &entry : batchEntries) {
    allVertices.insert(allVertices.end(), entry.vertices.begin(),
                       entry.vertices.end());
  }

  // Upload all vertices at once
  void *data;
  vkMapMemory(device, vertexMemory, 0, sizeof(TextVertex) * allVertices.size(),
              0, &data);
  memcpy(data, allVertices.data(), sizeof(TextVertex) * allVertices.size());
  vkUnmapMemory(device, vertexMemory);

  // Bind pipeline and descriptor once
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
  VkBuffer vertexBuffers[] = {vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

  // Draw each text entry with its color, using firstVertex offset
  uint32_t vertexOffset = 0;
  for (auto &entry : batchEntries) {
    vkCmdPushConstants(commandBuffer, pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4,
                       entry.color);
    vkCmdDraw(commandBuffer, static_cast<uint32_t>(entry.vertices.size()), 1,
              vertexOffset, 0);
    vertexOffset += static_cast<uint32_t>(entry.vertices.size());
  }

  batchEntries.clear();
  inBatch = false;
}

void TextRenderer::updateVertexBuffer() {
  if (vertices.empty())
    return;

  void *data;
  vkMapMemory(device, vertexMemory, 0, sizeof(TextVertex) * vertices.size(), 0,
              &data);
  memcpy(data, vertices.data(), sizeof(TextVertex) * vertices.size());
  vkUnmapMemory(device, vertexMemory);
}

void TextRenderer::renderText(VkCommandBuffer commandBuffer,
                              const std::string &text, float x, float y,
                              float scale, float color[4]) {
  prepareText(text, x, y, scale);

  if (vertexCount == 0)
    return;

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

  VkBuffer vertexBuffers[] = {vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

  vkCmdPushConstants(commandBuffer, pipelineLayout,
                     VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4, color);

  vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
}

void TextRenderer::createPipeline(VkRenderPass renderPass,
                                  VkExtent2D swapChainExtent) {
  // Clean up old pipeline if it exists
  if (pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, pipeline, nullptr);
  }
  if (pipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
  }

  // Load shaders (you need to compile text_vert.glsl and text_frag.glsl to
  // SPIR-V)
  auto vertShaderCode = readFile("text_vert.spv");
  auto fragShaderCode = readFile("text_frag.spv");

  VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
  VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

  VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
  vertShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vertShaderStageInfo.module = vertShaderModule;
  vertShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
  fragShaderStageInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fragShaderStageInfo.module = fragShaderModule;
  fragShaderStageInfo.pName = "main";

  VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                    fragShaderStageInfo};

  // Vertex input
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(TextVertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(TextVertex, pos);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(TextVertex, uv);

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
  vertexInputInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputInfo.vertexBindingDescriptionCount = 1;
  vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
  vertexInputInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(attributeDescriptions.size());
  vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  inputAssembly.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(swapChainExtent.width);
  viewport.height = static_cast<float>(swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = swapChainExtent;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Enable blending for text transparency
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_TRUE;
  colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  colorBlendAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

  VkPipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(float) * 4;

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                             &pipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create text pipeline layout!");
  }

  VkGraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = pipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, &pipeline) != VK_SUCCESS) {
    throw std::runtime_error("failed to create text graphics pipeline!");
  }

  vkDestroyShaderModule(device, fragShaderModule, nullptr);
  vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

VkShaderModule TextRenderer::createShaderModule(const std::vector<char> &code) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create shader module!");
  }

  return shaderModule;
}

std::vector<char> TextRenderer::readFile(const std::string &filename) {
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    throw std::runtime_error("failed to open file: " + filename);
  }

  size_t fileSize = (size_t)file.tellg();
  std::vector<char> buffer(fileSize);

  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();

  return buffer;
}
uint32_t TextRenderer::findMemoryType(uint32_t typeFilter,
                                      VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void TextRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags properties,
                                VkBuffer &buffer,
                                VkDeviceMemory &bufferMemory) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to create buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate buffer memory!");
  }

  vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

VkCommandBuffer
TextRenderer::beginSingleTimeCommands(VkCommandPool commandPool) {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);
  return commandBuffer;
}

void TextRenderer::endSingleTimeCommands(VkCommandPool commandPool,
                                         VkQueue graphicsQueue,
                                         VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQueue);

  vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

void TextRenderer::transitionImageLayout(VkCommandPool commandPool,
                                         VkQueue graphicsQueue, VkImage image,
                                         VkFormat format,
                                         VkImageLayout oldLayout,
                                         VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands(commandPool);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::invalid_argument("unsupported layout transition!");
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  endSingleTimeCommands(commandPool, graphicsQueue, commandBuffer);
}

void TextRenderer::copyBufferToImage(VkCommandPool commandPool,
                                     VkQueue graphicsQueue, VkBuffer buffer,
                                     VkImage image, uint32_t width,
                                     uint32_t height) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands(commandPool);

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  endSingleTimeCommands(commandPool, graphicsQueue, commandBuffer);
}
