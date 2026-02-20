#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>

struct CharacterInfo {
  float ax; // advance x
  float ay; // advance y
  float bw; // bitmap width
  float bh; // bitmap height
  float bl; // bitmap left
  float bt; // bitmap top
  float tx; // texture x offset
  float ty;
};

struct TextVertex {
  float pos[2];
  float uv[2];
};

class TextRenderer {
public:
  TextRenderer();
  ~TextRenderer();

  // Initialize the text renderer with a font file
  bool init(VkDevice device, VkPhysicalDevice physicalDevice,
            VkCommandPool commandPool, VkQueue graphicsQueue,
            const char *fontPath, float fontSize);

  // Cleanup resources
  void cleanup();

  // Render text at specified position
  void renderText(VkCommandBuffer commandBuffer, const std::string &text,
                  float x, float y, float scale, float color[4]);

  // Get the render pass for text rendering
  VkDescriptorSetLayout getDescriptorSetLayout() const {
    return descriptorSetLayout;
  }
  VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }
  VkPipeline getPipeline() const { return pipeline; }

  // Update text vertices for a given string
  void prepareText(const std::string &text, float x, float y, float scale);

  // Get vertex buffer for binding
  VkBuffer getVertexBuffer() const { return vertexBuffer; }
  uint32_t getVertexCount() const { return vertexCount; }

  // Create graphics pipeline for text rendering
  void createPipeline(VkRenderPass renderPass, VkExtent2D swapChainExtent);

  // Add these public methods:
  void beginBatch();
  void addText(const std::string &text, float x, float y, float scale,
               float color[4]);
  void endBatch(VkCommandBuffer commandBuffer);

private:
  VkDevice device;
  VkPhysicalDevice physicalDevice;

  // Font bitmap texture
  VkImage fontAtlasImage;
  VkDeviceMemory fontAtlasMemory;
  VkImageView fontAtlasView;
  VkSampler fontSampler;

  // Descriptor sets for texture
  VkDescriptorPool descriptorPool;
  VkDescriptorSetLayout descriptorSetLayout;
  VkDescriptorSet descriptorSet;

  // Pipeline
  VkPipelineLayout pipelineLayout;
  VkPipeline pipeline;

  // Vertex buffer for text quads
  VkBuffer vertexBuffer;
  VkDeviceMemory vertexMemory;
  std::vector<TextVertex> vertices;
  uint32_t vertexCount;

  // Font data
  unsigned char *fontBuffer;
  unsigned char *bitmapBuffer;
  int atlasWidth;
  int atlasHeight;
  float fontSize;
  std::unordered_map<char, CharacterInfo> characterMap;

  // Helper functions
  bool loadFont(const char *fontPath, float fontSize);
  void createFontAtlas();
  void createDescriptorSetLayout();
  void createDescriptorPool();
  void createDescriptorSet();
  void createVertexBuffer();
  void updateVertexBuffer();

  uint32_t findMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties);
  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties, VkBuffer &buffer,
                    VkDeviceMemory &bufferMemory);
  void copyBuffer(VkCommandPool commandPool, VkQueue graphicsQueue,
                  VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
  void transitionImageLayout(VkCommandPool commandPool, VkQueue graphicsQueue,
                             VkImage image, VkFormat format,
                             VkImageLayout oldLayout, VkImageLayout newLayout);
  void copyBufferToImage(VkCommandPool commandPool, VkQueue graphicsQueue,
                         VkBuffer buffer, VkImage image, uint32_t width,
                         uint32_t height);
  VkCommandBuffer beginSingleTimeCommands(VkCommandPool commandPool);
  void endSingleTimeCommands(VkCommandPool commandPool, VkQueue graphicsQueue,
                             VkCommandBuffer commandBuffer);
  std::vector<char> readFile(const std::string &filename);
  VkShaderModule createShaderModule(const std::vector<char> &code);

  // Add private member:
  struct TextBatchEntry {
    std::vector<TextVertex> vertices;
    float color[4];
  };
  std::vector<TextBatchEntry> batchEntries;
  bool inBatch = false;
};

#endif // TEXT_RENDERER_H
