/* Copyright (C) 2016, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "renderer.hpp"

#include "data/game_traits.hpp"
#include "loader/palette.hpp"

RIGEL_DISABLE_WARNINGS
#include <glm/gtc/matrix_transform.hpp>
RIGEL_RESTORE_WARNINGS

#include <array>


namespace rigel::renderer {

namespace {

// The game's original 320x200 resolution would give us a 16:10 aspect ratio
// when using square pixels, but monitors of the time had a 4:3 aspect ratio,
// and that's what the game's graphics were designed for (very noticeable e.g.
// with the earth in the Apogee logo). It worked out fine back then because
// CRTs can show non-square pixels, but that's not possible with today's
// screens anymore. Therefore, we need to stretch the image slightly before
// actually rendering it. We do that by rendering the game into a 320x200
// render target, and then stretching that onto our logical display which has a
// slightly bigger vertical resolution in order to get a 4:3 aspect ratio.
const auto ASPECT_RATIO_CORRECTED_VIEW_PORT_HEIGHT = 240;

// By making the logical display bigger than the aspect-ratio corrected
// original resolution, we can show text with debug info (e.g. FPS) without it
// taking up too much space or being hard to read.
const auto SCALE_FACTOR = 2;

const auto LOGICAL_DISPLAY_WIDTH =
  data::GameTraits::viewPortWidthPx * SCALE_FACTOR;
const auto LOGICAL_DISPLAY_HEIGHT =
  ASPECT_RATIO_CORRECTED_VIEW_PORT_HEIGHT * SCALE_FACTOR;


const GLushort QUAD_INDICES[] = { 0, 1, 2, 2, 3, 1 };


constexpr auto WATER_MASK_WIDTH = 8;
constexpr auto WATER_MASK_HEIGHT = 8;
constexpr auto WATER_NUM_MASKS = 5;
constexpr auto WATER_MASK_INDEX_FILLED = 4;


#ifdef RIGEL_USE_GL_ES

const auto SHADER_PREAMBLE = R"shd(
#version 100

#define ATTRIBUTE attribute
#define OUT varying
#define IN varying
#define TEXTURE_LOOKUP texture2D
#define OUTPUT_COLOR gl_FragColor
#define OUTPUT_COLOR_DECLARATION
#define SET_POINT_SIZE(size) gl_PointSize = size;
)shd";

#else

const auto SHADER_PREAMBLE = R"shd(
#version 150

#define ATTRIBUTE in
#define OUT out
#define IN in
#define TEXTURE_LOOKUP texture
#define OUTPUT_COLOR outputColor
#define OUTPUT_COLOR_DECLARATION out vec4 outputColor;
#define SET_POINT_SIZE
)shd";

#endif


const auto VERTEX_SOURCE = R"shd(
ATTRIBUTE vec2 position;
ATTRIBUTE vec2 texCoord;

OUT vec2 texCoordFrag;

uniform mat4 transform;

void main() {
  gl_Position = transform * vec4(position, 0.0, 1.0);
  texCoordFrag = vec2(texCoord.x, 1.0 - texCoord.y);
}
)shd";

const auto FRAGMENT_SOURCE = R"shd(
OUTPUT_COLOR_DECLARATION

IN vec2 texCoordFrag;

uniform sampler2D textureData;
uniform vec4 overlayColor;

uniform vec4 colorModulation;

void main() {
  vec4 baseColor = TEXTURE_LOOKUP(textureData, texCoordFrag);
  vec4 modulated = baseColor * colorModulation;
  float targetAlpha = modulated.a;

  OUTPUT_COLOR =
    vec4(mix(modulated.rgb, overlayColor.rgb, overlayColor.a), targetAlpha);
}
)shd";

const auto VERTEX_SOURCE_SOLID = R"shd(
ATTRIBUTE vec2 position;
ATTRIBUTE vec4 color;

OUT vec4 colorFrag;

uniform mat4 transform;

void main() {
  SET_POINT_SIZE(1.0);
  gl_Position = transform * vec4(position, 0.0, 1.0);
  colorFrag = color;
}
)shd";

const auto FRAGMENT_SOURCE_SOLID = R"shd(
OUTPUT_COLOR_DECLARATION

IN vec4 colorFrag;

void main() {
  OUTPUT_COLOR = colorFrag;
}
)shd";


const auto VERTEX_SOURCE_WATER_EFFECT = R"shd(
ATTRIBUTE vec2 position;
ATTRIBUTE vec2 texCoord;
ATTRIBUTE vec2 texCoordMask;

OUT vec2 texCoordFrag;
OUT vec2 texCoordMaskFrag;

uniform mat4 transform;

void main() {
  SET_POINT_SIZE(1.0);
  gl_Position = transform * vec4(position, 0.0, 1.0);
  texCoordFrag = vec2(texCoord.x, 1.0 - texCoord.y);
  texCoordMaskFrag = vec2(texCoordMask.x, 1.0 - texCoordMask.y);
}
)shd";

const auto FRAGMENT_SOURCE_WATER_EFFECT = R"shd(
OUTPUT_COLOR_DECLARATION

IN vec2 texCoordFrag;
IN vec2 texCoordMaskFrag;

uniform sampler2D textureData;
uniform sampler2D maskData;
uniform vec3 palette[16];


vec4 applyWaterEffect(vec4 color) {
  int index = 0;
  for (int i = 0; i < 16; ++i) {
    if (color.rgb == palette[i]) {
      index = i;
    }
  }

  int adjustedIndex = (index & 0x3) | 0x8;
  return vec4(palette[adjustedIndex], color.a);
}

void main() {
  vec4 color = TEXTURE_LOOKUP(textureData, texCoordFrag);
  vec4 mask = TEXTURE_LOOKUP(maskData, texCoordMaskFrag);
  float maskValue = mask.r;
  OUTPUT_COLOR = mix(color, applyWaterEffect(color), maskValue);
}
)shd";


base::Rect<int> determineDefaultViewport(SDL_Window* pWindow) {
  // This calculates the required view port coordinates to have aspect-ratio
  // correct scaling from the internal display resolution to the window's
  // actual size.
  int windowWidthInt = 0;
  int windowHeightInt = 0;
  SDL_GetWindowSize(pWindow, &windowWidthInt, &windowHeightInt);

  const auto windowWidth = float(windowWidthInt);
  const auto windowHeight = float(windowHeightInt);

  const auto widthRatio = windowWidth / LOGICAL_DISPLAY_WIDTH;
  const auto heightRatio = windowHeight / LOGICAL_DISPLAY_HEIGHT;

  const auto smallerRatio = std::min(widthRatio, heightRatio);
  const auto usableWidth = LOGICAL_DISPLAY_WIDTH * smallerRatio;
  const auto usableHeight = LOGICAL_DISPLAY_HEIGHT * smallerRatio;

  // Calculate the appropriate offset to center the viewport inside the window
  const auto offsetX = (windowWidth - usableWidth) / 2.0f;
  const auto offsetY = (windowHeight - usableHeight) / 2.0f;

  return {
    {int(offsetX), int(offsetY)},
    {int(usableWidth), int(usableHeight)}
  };
}


void* toAttribOffset(std::uintptr_t offset) {
  return reinterpret_cast<void*>(offset);
}


glm::vec4 toGlColor(const base::Color& color) {
  return glm::vec4{color.r, color.g, color.b, color.a} / 255.0f;
}


void setScissorBox(
  const base::Rect<int>& clipRect,
  const base::Size<int>& frameBufferSize
) {
  const auto offsetAtBottom = frameBufferSize.height - clipRect.bottom();
  glScissor(
    clipRect.topLeft.x,
    offsetAtBottom,
    clipRect.size.width,
    clipRect.size.height);
}


template <typename Iter>
void fillVertexData(
  float left,
  float right,
  float top,
  float bottom,
  Iter&& destIter,
  const std::size_t offset,
  const std::size_t stride
) {
  using namespace std;
  advance(destIter, offset);

  const auto innerStride = stride - 2;

  *destIter++ = left;
  *destIter++ = bottom;
  advance(destIter, innerStride);

  *destIter++ = left;
  *destIter++ = top;
  advance(destIter, innerStride);

  *destIter++ = right;
  *destIter++ = bottom;
  advance(destIter, innerStride);

  *destIter++ = right;
  *destIter++ = top;
  advance(destIter, innerStride);
}


template <typename Iter>
void fillVertexPositions(
  const base::Rect<int>& rect,
  Iter&& destIter,
  const std::size_t offset,
  const std::size_t stride
) {
  glm::vec2 posOffset(float(rect.topLeft.x), float(rect.topLeft.y));
  glm::vec2 posScale(float(rect.size.width), float(rect.size.height));

  const auto left = posOffset.x;
  const auto right = posScale.x + posOffset.x;
  const auto top = posOffset.y;
  const auto bottom = posScale.y + posOffset.y;

  fillVertexData(
    left, right, top, bottom, std::forward<Iter>(destIter), offset, stride);
}


template <typename Iter>
void fillTexCoords(
  const base::Rect<int>& rect,
  Renderer::TextureData textureData,
  Iter&& destIter,
  const std::size_t offset,
  const std::size_t stride
) {
  using namespace std;

  glm::vec2 texOffset(
    rect.topLeft.x / float(textureData.mWidth),
    rect.topLeft.y / float(textureData.mHeight));
  glm::vec2 texScale(
    rect.size.width / float(textureData.mWidth),
    rect.size.height / float(textureData.mHeight));

  const auto left = texOffset.x;
  const auto right = texScale.x + texOffset.x;
  const auto top = texOffset.y;
  const auto bottom = texScale.y + texOffset.y;

  fillVertexData(
    left, right, top, bottom, std::forward<Iter>(destIter), offset, stride);
}


data::Image createWaterSurfaceAnimImage() {
  auto pixels = data::PixelBuffer{
    WATER_MASK_WIDTH * WATER_MASK_HEIGHT * WATER_NUM_MASKS,
    base::Color{255, 255, 255, 255}};

  const std::array<int, 16> patternCalmSurface{
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1
  };

  const std::array<int, 16> patternWaveRight{
    0, 0, 0, 0, 0, 1, 1, 0,
    1, 0, 0, 1, 1, 1, 1, 1
  };

  const std::array<int, 16> patternWaveLeft{
    0, 1, 1, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 0, 0, 1
  };

  auto applyPattern = [&pixels](
    const auto& pattern,
    const auto destOffset
  ) {
    std::transform(
      std::begin(pattern),
      std::end(pattern),
      std::begin(pixels) + destOffset,
      [](const int patternValue) {
        const auto value = static_cast<uint8_t>(255 * patternValue);
        return base::Color{value, value, value, value};
      });
  };

  const auto pixelsPerAnimStep = WATER_MASK_WIDTH * WATER_MASK_HEIGHT;

  applyPattern(patternCalmSurface, 0);
  applyPattern(patternWaveRight, pixelsPerAnimStep);
  applyPattern(patternCalmSurface, pixelsPerAnimStep * 2);
  applyPattern(patternWaveLeft, pixelsPerAnimStep * 3);

  return data::Image{
    move(pixels),
    static_cast<size_t>(WATER_MASK_WIDTH),
    static_cast<size_t>(WATER_MASK_HEIGHT * WATER_NUM_MASKS)};
}

}


Renderer::Renderer(SDL_Window* pWindow)
  : mpWindow(pWindow)
  , mTexturedQuadShader(
      SHADER_PREAMBLE,
      VERTEX_SOURCE,
      FRAGMENT_SOURCE,
      {"position", "texCoord"})
  , mSolidColorShader(
      SHADER_PREAMBLE,
      VERTEX_SOURCE_SOLID,
      FRAGMENT_SOURCE_SOLID,
      {"position", "color"})
  , mWaterEffectShader(
      SHADER_PREAMBLE,
      VERTEX_SOURCE_WATER_EFFECT,
      FRAGMENT_SOURCE_WATER_EFFECT,
      {"position", "texCoord", "texCoordMask"})
  , mLastUsedShader(0)
  , mLastUsedTexture(0)
  , mRenderMode(RenderMode::SpriteBatch)
  , mCurrentFbo(0)
  , mCurrentFramebufferSize(LOGICAL_DISPLAY_WIDTH, LOGICAL_DISPLAY_HEIGHT)
  , mDefaultViewport(determineDefaultViewport(pWindow))
  , mGlobalTranslation(0.0f, 0.0f)
  , mGlobalScale(1.0f, 1.0f)
{
  using namespace std;

  // General configuration
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  SDL_GL_SetSwapInterval(1);

  // Setup a VBO for streaming data to the GPU, stays bound all the time
  glGenBuffers(1, &mStreamVbo);
  glBindBuffer(GL_ARRAY_BUFFER, mStreamVbo);
  glGenBuffers(1, &mStreamEbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mStreamEbo);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);

  // One-time setup for water effect shader
  useShaderIfChanged(mWaterEffectShader);
  array<glm::vec3, 16> palette;
  transform(
    begin(loader::INGAME_PALETTE),
    end(loader::INGAME_PALETTE),
    begin(palette),
    [](const base::Color& color) {
      return glm::vec3{color.r, color.g, color.b} / 255.0f;
    });
  mWaterEffectShader.setUniform("palette", palette);
  mWaterEffectShader.setUniform("textureData", 0);
  mWaterEffectShader.setUniform("maskData", 1);

  mWaterSurfaceAnimTexture = createTexture(createWaterSurfaceAnimImage());

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, mWaterSurfaceAnimTexture.mHandle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glActiveTexture(GL_TEXTURE0);

  // One-time setup for textured quad shader
  useShaderIfChanged(mTexturedQuadShader);
  mTexturedQuadShader.setUniform("textureData", 0);

  // Remaining setup
  onRenderTargetChanged();

  setColorModulation({255, 255, 255, 255});
}


Renderer::~Renderer() {
  glDeleteBuffers(1, &mStreamVbo);
  glDeleteTextures(1, &mWaterSurfaceAnimTexture.mHandle);
}


base::Rect<int> Renderer::fullScreenRect() const {
  return {{0, 0}, mCurrentFramebufferSize};
}


void Renderer::setOverlayColor(const base::Color& color) {
  if (color != mLastOverlayColor) {
    submitBatch();

    setRenderModeIfChanged(RenderMode::SpriteBatch);
    mTexturedQuadShader.setUniform("overlayColor", toGlColor(color));
    mLastOverlayColor = color;
  }
}


void Renderer::setColorModulation(const base::Color& colorModulation) {
  if (colorModulation != mLastColorModulation) {
    submitBatch();

    setRenderModeIfChanged(RenderMode::SpriteBatch);
    mTexturedQuadShader.setUniform(
      "colorModulation", toGlColor(colorModulation));
    mLastColorModulation = colorModulation;
  }
}


void Renderer::drawTexture(
  const TextureData& textureData,
  const base::Rect<int>& sourceRect,
  const base::Rect<int>& destRect
) {
  if (!isVisible(destRect)) {
    return;
  }

  setRenderModeIfChanged(RenderMode::SpriteBatch);

  if (textureData.mHandle != mLastUsedTexture) {
    submitBatch();

    glBindTexture(GL_TEXTURE_2D, textureData.mHandle);
    mLastUsedTexture = textureData.mHandle;
  }

  // x, y, tex_u, tex_v
  GLfloat vertices[4 * (2 + 2)];
  fillVertexPositions(destRect, std::begin(vertices), 0, 4);
  fillTexCoords(sourceRect, textureData, std::begin(vertices), 2, 4);

  batchQuadVertices(std::cbegin(vertices), std::cend(vertices), 4u);
}


void Renderer::submitBatch() {
  if (mBatchData.empty()) {
    return;
  }

  auto submitBatchedQuads = [this]() {
    glBufferData(
      GL_ARRAY_BUFFER,
      sizeof(float) * mBatchData.size(),
      mBatchData.data(),
      GL_STREAM_DRAW);
    glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      sizeof(GLushort) * mBatchIndices.size(),
      mBatchIndices.data(),
      GL_STREAM_DRAW);
    glDrawElements(
      GL_TRIANGLES,
      GLsizei(mBatchIndices.size()),
      GL_UNSIGNED_SHORT,
      nullptr);
  };

  switch (mRenderMode) {
    case RenderMode::SpriteBatch:
    case RenderMode::WaterEffect:
      submitBatchedQuads();
      break;

    case RenderMode::Points:
      glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(float) * mBatchData.size(),
        mBatchData.data(),
        GL_STREAM_DRAW);
      glDrawArrays(GL_POINTS, 0, GLsizei(mBatchData.size() / 6));
      break;

    case RenderMode::NonTexturedRender:
      // No batching yet for NonTexturedRender
      assert(false);
      break;
  }

  mBatchData.clear();
  mBatchIndices.clear();
}


void Renderer::drawRectangle(
  const base::Rect<int>& rect,
  const base::Color& color
) {
  // Note: No batching for now, drawRectangle is only used for debugging at
  // the moment
  if (!isVisible(rect)) {
    return;
  }

  setRenderModeIfChanged(RenderMode::NonTexturedRender);

  const auto left = float(rect.left());
  const auto right = float(rect.right());
  const auto top = float(rect.top());
  const auto bottom = float(rect.bottom());

  const auto colorVec = toGlColor(color);
  float vertices[] = {
    left, top, colorVec.r, colorVec.g, colorVec.b, colorVec.a,
    left, bottom, colorVec.r, colorVec.g, colorVec.b, colorVec.a,
    right, bottom, colorVec.r, colorVec.g, colorVec.b, colorVec.a,
    right, top, colorVec.r, colorVec.g, colorVec.b, colorVec.a,
    left, top, colorVec.r, colorVec.g, colorVec.b, colorVec.a
  };

  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
  glDrawArrays(GL_LINE_STRIP, 0, 5);
}


void Renderer::drawLine(
  const int x1,
  const int y1,
  const int x2,
  const int y2,
  const base::Color& color
) {
  // Note: No batching for now, drawLine is only used for debugging at the
  // moment
  setRenderModeIfChanged(RenderMode::NonTexturedRender);

  const auto colorVec = toGlColor(color);

  float vertices[] = {
    float(x1), float(y1), colorVec.r, colorVec.g, colorVec.b, colorVec.a,
    float(x2), float(y2), colorVec.r, colorVec.g, colorVec.b, colorVec.a
  };

  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
  glDrawArrays(GL_LINE_STRIP, 0, 2);
}


void Renderer::drawPoint(
  const base::Vector& position,
  const base::Color& color
) {
  const auto& visibleRect = fullScreenRect();
  if (!visibleRect.containsPoint(position)) {
    return;
  }

  setRenderModeIfChanged(RenderMode::Points);

  float vertices[] = {
    float(position.x),
    float(position.y),
    color.r / 255.0f,
    color.g / 255.0f,
    color.b / 255.0f,
    color.a / 255.0f
  };
  mBatchData.insert(
    std::end(mBatchData), std::cbegin(vertices), std::cend(vertices));
}


void Renderer::drawWaterEffect(
  const base::Rect<int>& area,
  TextureData textureData,
  std::optional<int> surfaceAnimationStep
) {
  assert(
    !surfaceAnimationStep ||
    (*surfaceAnimationStep >= 0 && *surfaceAnimationStep < 4));

  using namespace std;

  if (!isVisible(area)) {
    return;
  }

  const auto areaWidth = area.size.width;
  auto drawWater = [&, this](
    const base::Rect<int>& destRect,
    const int maskIndex
  ) {
    const auto maskTexStartY = maskIndex * WATER_MASK_HEIGHT;
    const auto animSourceRect = base::Rect<int>{
      {0, maskTexStartY},
      {areaWidth, WATER_MASK_HEIGHT}
    };

    // x, y, tex_u, tex_v, mask_u, mask_v
    GLfloat vertices[4 * (2 + 2 + 2)];
    fillVertexPositions(destRect, std::begin(vertices), 0, 6);
    fillTexCoords(destRect, textureData, std::begin(vertices), 2, 6);
    fillTexCoords(
      animSourceRect, mWaterSurfaceAnimTexture, std::begin(vertices), 4, 6);

    batchQuadVertices(std::cbegin(vertices), std::cend(vertices), 6);
  };

  setRenderModeIfChanged(RenderMode::WaterEffect);

  if (mLastUsedTexture != textureData.mHandle) {
    submitBatch();
    glBindTexture(GL_TEXTURE_2D, textureData.mHandle);
    mLastUsedTexture = textureData.mHandle;
  }

  if (surfaceAnimationStep) {
    const auto waterSurfaceArea = base::Rect<int>{
      area.topLeft,
      {areaWidth, WATER_MASK_HEIGHT}
    };

    drawWater(waterSurfaceArea, *surfaceAnimationStep);

    auto remainingArea = area;
    remainingArea.topLeft.y += WATER_MASK_HEIGHT;
    remainingArea.size.height -= WATER_MASK_HEIGHT;

    drawWater(remainingArea, WATER_MASK_INDEX_FILLED);
  } else {
    drawWater(area, WATER_MASK_INDEX_FILLED);
  }
}


void Renderer::setGlobalTranslation(const base::Vector& translation) {
  const auto glTranslation = glm::vec2{translation.x, translation.y};
  if (glTranslation != mGlobalTranslation) {
    submitBatch();

    mGlobalTranslation = glTranslation;
    updateProjectionMatrix();
  }
}


base::Vector Renderer::globalTranslation() const {
  return base::Vector{
    static_cast<int>(mGlobalTranslation.x),
    static_cast<int>(mGlobalTranslation.y)};
}


void Renderer::setGlobalScale(const base::Point<float>& scale) {
  const auto glScale = glm::vec2{scale.x, scale.y};
  if (glScale != mGlobalScale) {
    submitBatch();

    mGlobalScale = glScale;
    updateProjectionMatrix();
  }
}


base::Point<float> Renderer::globalScale() const {
  return {mGlobalScale.x, mGlobalScale.y};
}


void Renderer::setClipRect(const std::optional<base::Rect<int>>& clipRect) {
  if (clipRect == mClipRect) {
    return;
  }

  mClipRect = clipRect;
  if (mClipRect) {
    glEnable(GL_SCISSOR_TEST);

    setScissorBox(*clipRect, mCurrentFramebufferSize);
  } else {
    glDisable(GL_SCISSOR_TEST);
  }
}


std::optional<base::Rect<int>> Renderer::clipRect() const {
  return mClipRect;
}


Renderer::RenderTarget Renderer::currentRenderTarget() const {
  return {mCurrentFramebufferSize, mCurrentFbo};
}


void Renderer::setRenderTarget(const RenderTarget& target) {
  if (target.mFbo == mCurrentFbo) {
    return;
  }

  submitBatch();

  if (!target.isDefault()) {
    mCurrentFramebufferSize = target.mSize;
    mCurrentFbo = target.mFbo;
  } else {
    mCurrentFramebufferSize.width = LOGICAL_DISPLAY_WIDTH;
    mCurrentFramebufferSize.height = LOGICAL_DISPLAY_HEIGHT;
    mCurrentFbo = 0;
  }

  onRenderTargetChanged();
}


void Renderer::swapBuffers() {
  submitBatch();
  SDL_GL_SwapWindow(mpWindow);
}


void Renderer::clear(const base::Color& clearColor) {
  const auto glColor = toGlColor(clearColor);
  glClearColor(glColor.r, glColor.g, glColor.b, glColor.a);
  glClear(GL_COLOR_BUFFER_BIT);
}


template <typename VertexIter>
void Renderer::batchQuadVertices(
  VertexIter&& dataBegin,
  VertexIter&& dataEnd,
  const std::size_t attributesPerVertex
) {
  using namespace std;

  const auto currentIndex = GLushort(mBatchData.size() / attributesPerVertex);

  GLushort indices[6];
  transform(
    cbegin(QUAD_INDICES),
    cend(QUAD_INDICES),
    begin(indices),
    [&](const GLushort index) -> GLushort {
      return index + currentIndex;
    });

  // TODO: Limit maximum batch size
  mBatchData.insert(
    mBatchData.end(),
    forward<VertexIter>(dataBegin),
    forward<VertexIter>(dataEnd));
  mBatchIndices.insert(mBatchIndices.end(), cbegin(indices), cend(indices));
}


void Renderer::setRenderModeIfChanged(const RenderMode mode) {
  if (mRenderMode != mode) {
    submitBatch();

    mRenderMode = mode;
    updateShaders();
  }
}


void Renderer::updateShaders() {
  switch (mRenderMode) {
    case RenderMode::SpriteBatch:
      useShaderIfChanged(mTexturedQuadShader);
      mTexturedQuadShader.setUniform("transform", mProjectionMatrix);
      glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(float) * 4,
        toAttribOffset(0));
      glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(float) * 4,
        toAttribOffset(2 * sizeof(float)));
      glDisableVertexAttribArray(2);
      break;

    case RenderMode::Points:
    case RenderMode::NonTexturedRender:
      useShaderIfChanged(mSolidColorShader);
      mSolidColorShader.setUniform("transform", mProjectionMatrix);
      glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(float) * 6,
        toAttribOffset(0));
      glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(float) * 6,
        toAttribOffset(2 * sizeof(float)));
      glDisableVertexAttribArray(2);
      break;

    case RenderMode::WaterEffect:
      useShaderIfChanged(mWaterEffectShader);
      mWaterEffectShader.setUniform("transform", mProjectionMatrix);
      glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(float) * 6,
        toAttribOffset(0));
      glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(float) * 6,
        toAttribOffset(2 * sizeof(float)));
      glVertexAttribPointer(
        2,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(float) * 6,
        toAttribOffset(4 * sizeof(float)));
      glEnableVertexAttribArray(2);
      break;
  }
}


Renderer::RenderTargetHandles Renderer::createRenderTargetTexture(
  const int width,
  const int height
) {
  const auto textureHandle =
    createGlTexture(GLsizei(width), GLsizei(height), nullptr);
  glBindTexture(GL_TEXTURE_2D, textureHandle);

  GLuint fboHandle;
  glGenFramebuffers(1, &fboHandle);
  glBindFramebuffer(GL_FRAMEBUFFER, fboHandle);
  glFramebufferTexture2D(
    GL_FRAMEBUFFER,
    GL_COLOR_ATTACHMENT0,
    GL_TEXTURE_2D,
    textureHandle,
    0);

  glBindFramebuffer(GL_FRAMEBUFFER, mCurrentFbo);
  glBindTexture(GL_TEXTURE_2D, mLastUsedTexture);

  return {textureHandle, fboHandle};
}


auto Renderer::createTexture(const data::Image& image) -> TextureData {
  // OpenGL wants pixel data in bottom-up format, so transform it accordingly
  std::vector<std::uint8_t> pixelData;
  pixelData.resize(image.width() * image.height() * 4);
  for (std::size_t y = 0; y < image.height(); ++y) {
    const auto sourceRow = image.height() - (y + 1);
    const auto yOffsetSource = image.width() * sourceRow;
    const auto yOffset = y * image.width() * 4;

    for (std::size_t x = 0; x < image.width(); ++x) {
      const auto& pixel = image.pixelData()[x + yOffsetSource];
      pixelData[x*4 +     yOffset] = pixel.r;
      pixelData[x*4 + 1 + yOffset] = pixel.g;
      pixelData[x*4 + 2 + yOffset] = pixel.b;
      pixelData[x*4 + 3 + yOffset] = pixel.a;
    }
  }

  auto handle = createGlTexture(
    GLsizei(image.width()),
    GLsizei(image.height()),
    pixelData.data());
  return {int(image.width()), int(image.height()), handle};
}


GLuint Renderer::createGlTexture(
  const GLsizei width,
  const GLsizei height,
  const GLvoid* const pData
) {
  GLuint handle = 0;
  glGenTextures(1, &handle);

  glBindTexture(GL_TEXTURE_2D, handle);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glTexImage2D(
    GL_TEXTURE_2D,
    0,
    GL_RGBA,
    width,
    height,
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    pData);
  glBindTexture(GL_TEXTURE_2D, mLastUsedTexture);

  return handle;
}



void Renderer::useShaderIfChanged(Shader& shader) {
  if (shader.handle() != mLastUsedShader) {
    shader.use();
    mLastUsedShader = shader.handle();
  }
}


bool Renderer::isVisible(const base::Rect<int>& rect) const {
  return rect.intersects(fullScreenRect());
}


void Renderer::onRenderTargetChanged() {
  glBindFramebuffer(GL_FRAMEBUFFER, mCurrentFbo);
  if (mCurrentFbo == 0) {
    glViewport(
      mDefaultViewport.topLeft.x,
      mDefaultViewport.topLeft.y,
      mDefaultViewport.size.width,
      mDefaultViewport.size.height);
  } else {
    glViewport(0, 0, mCurrentFramebufferSize.width, mCurrentFramebufferSize.height);
  }

  updateProjectionMatrix();

  if (mClipRect) {
    setScissorBox(*mClipRect, mCurrentFramebufferSize);
  }
}


void Renderer::updateProjectionMatrix() {
  const auto projection = glm::ortho(
    0.0f,
    float(mCurrentFramebufferSize.width),
    float(mCurrentFramebufferSize.height),
    0.0f);

  mProjectionMatrix = glm::translate(
    glm::scale(projection, glm::vec3(mGlobalScale, 1.0f)),
    glm::vec3(mGlobalTranslation, 0.0f));

  updateShaders();
}

}
