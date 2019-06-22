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

#pragma once

#include "base/array_view.hpp"
#include "base/spatial_types.hpp"
#include "base/warnings.hpp"
#include "engine/base_components.hpp"
#include "engine/timing.hpp"
#include "renderer/renderer.hpp"
#include "renderer/texture.hpp"

RIGEL_DISABLE_WARNINGS
#include <entityx/entityx.h>
RIGEL_RESTORE_WARNINGS

#include <optional>
#include <vector>


namespace rigel::engine {

struct SpriteFrame {
  SpriteFrame() = default;
  SpriteFrame(
    renderer::OwningTexture image,
    base::Vector drawOffset
  )
    : mImage(std::move(image))
    , mDrawOffset(drawOffset)
  {
  }

  renderer::OwningTexture mImage;
  base::Vector mDrawOffset;
};


struct SpriteDrawData {
  std::vector<SpriteFrame> mFrames;
  base::ArrayView<int> mVirtualToRealFrameMap;
  std::optional<int> mOrientationOffset;
  int mDrawOrder;
};


constexpr auto IGNORE_RENDER_SLOT = -1;


int virtualToRealFrame(
  const int virtualFrame,
  const SpriteDrawData& drawData,
  const entityx::Entity entity);

void drawSpriteFrame(
  const SpriteFrame& frame,
  const base::Vector& position,
  renderer::Renderer* pRenderer);


namespace components {

struct Sprite {
  Sprite() = default;
  Sprite(const SpriteDrawData* pDrawData, std::vector<int> framesToRender)
    : mFramesToRender(std::move(framesToRender))
    , mpDrawData(pDrawData)
  {
  }

  void flashWhite() {
    mFlashingWhite = true;
  }

  std::vector<int> mFramesToRender;
  const SpriteDrawData* mpDrawData = nullptr;
  bool mFlashingWhite = false;
  bool mTranslucent = false;
  bool mShow = true;
};


/** Specify a custom rendering function for a sprite
 *
 * When a sprite entity also has this component, the provided function
 * pointer will be invoked instead of rendering the sprite directly.
 *
 * The last argument is the sprite's world position converted to a screen-space
 * pixel position.
 */
using CustomRenderFunc = void(*)(
  renderer::Renderer*,
  entityx::Entity,
  const engine::components::Sprite&,
  const base::Vector&);


/** Indicates that an entity should always be drawn last
 *
 * An entity marked with this component will alwyas have its Sprite drawn after
 * drawing the world, even if it is placed on top of foreground tiles.
 */
struct DrawTopMost {};


struct OverrideDrawOrder {
  explicit OverrideDrawOrder(const int drawOrder)
    : mDrawOrder(drawOrder)
  {
  }

  int mDrawOrder;
};


struct AnimationLoop {
  AnimationLoop() = default;
  explicit AnimationLoop(
    const int delayInFrames,
    std::optional<int> endFrame = std::nullopt
  )
    : AnimationLoop(delayInFrames, 0, endFrame)
  {
  }

  AnimationLoop(
    const int delayInFrames,
    const int startFrame,
    std::optional<int> endFrame,
    const int renderSlot = 0
  )
    : mDelayInFrames(delayInFrames)
    , mStartFrame(startFrame)
    , mEndFrame(endFrame)
    , mRenderSlot(renderSlot)
  {
  }

  int mDelayInFrames = 0;
  int mFramesElapsed = 0;
  int mStartFrame = 0;
  std::optional<int> mEndFrame;
  int mRenderSlot = 0;
};


struct AnimationSequence {
  explicit AnimationSequence(
    const base::ArrayView<int>& frames,
    const int renderSlot = 0,
    const bool repeat = false)
    : mFrames(frames)
    , mRenderSlot(renderSlot)
    , mRepeat(repeat)
  {
  }

  base::ArrayView<int> mFrames;
  decltype(mFrames)::size_type mCurrentFrame = 0;
  int mRenderSlot = 0;
  bool mRepeat = false;
};

}}
