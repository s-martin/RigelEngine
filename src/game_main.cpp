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

#include "game_main.hpp"
#include "game_main.ipp"

#include "base/math_tools.hpp"
#include "data/duke_script.hpp"
#include "data/game_traits.hpp"
#include "engine/timing.hpp"
#include "loader/duke_script_loader.hpp"
#include "sdl_utils/error.hpp"
#include "ui/imgui_integration.hpp"

#include "game_session_mode.hpp"
#include "intro_demo_loop_mode.hpp"
#include "menu_mode.hpp"

RIGEL_DISABLE_WARNINGS
#include <imgui.h>
RIGEL_RESTORE_WARNINGS

#include <cassert>


namespace rigel {

using namespace engine;
using namespace sdl_utils;

using RenderTargetBinder = renderer::RenderTargetTexture::Binder;

namespace {

struct NullGameMode : public GameMode {
  void handleEvent(const SDL_Event&) override {}
  void updateAndRender(engine::TimeDelta) override {}
};


auto loadScripts(const loader::ResourceLoader& resources) {
  auto allScripts = resources.loadScriptBundle("TEXT.MNI");
  const auto optionsScripts = resources.loadScriptBundle("OPTIONS.MNI");
  const auto orderInfoScripts = resources.loadScriptBundle("ORDERTXT.MNI");

  allScripts.insert(std::begin(optionsScripts), std::end(optionsScripts));
  allScripts.insert(std::begin(orderInfoScripts), std::end(orderInfoScripts));

  return allScripts;
}


// The game's original 320x200 resolution would give us a 16:10 aspect ratio
// when using square pixels, but monitors of the time had a 4:3 aspect ratio,
// and that's what the game's graphics were designed for (very noticeable e.g.
// with the earth in the Apogee logo). CRTs are not limited to square pixels,
// and the monitor would stretch the 320x200 into the right shape for a 4:3
// picture.
const auto TARGET_ASPECT_RATIO = 4.0f / 3.0f;


[[nodiscard]] auto setupSimpleUpscaling(renderer::Renderer* pRenderer) {
  auto saved = renderer::Renderer::StateSaver{pRenderer};

  const auto [windowWidthInt, windowHeightInt] = pRenderer->windowSize();
  const auto windowWidth = float(windowWidthInt);
  const auto windowHeight = float(windowHeightInt);

  const auto usableWidth = windowWidth > windowHeight
    ? TARGET_ASPECT_RATIO * windowHeight
    : windowWidth;
  const auto usableHeight = windowHeight >= windowWidth
    ? 1.0f / TARGET_ASPECT_RATIO * windowWidth
    : windowHeight;

  const auto widthScale = usableWidth / data::GameTraits::viewPortWidthPx;
  const auto heightScale = usableHeight / data::GameTraits::viewPortHeightPx;

  pRenderer->setGlobalScale({widthScale, heightScale});

  const auto offsetX = (windowWidth - usableWidth) / 2.0f;
  const auto offsetY = (windowHeight - usableHeight) / 2.0f;
  const auto offset = base::Vector{int(offsetX), int(offsetY)};
  pRenderer->setGlobalTranslation(offset);

  pRenderer->setClipRect(base::Rect<int>{
    offset, {int(usableWidth), int(usableHeight)}});

  return saved;
}

}


void gameMain(const StartupOptions& options, SDL_Window* pWindow) {
  Game game(options.mGamePath, pWindow);
  game.run(options);
}


Game::Game(const std::string& gamePath, SDL_Window* pWindow)
  : mpWindow(pWindow)
  , mRenderer(pWindow)
  , mResources(gamePath)
  , mIsShareWareVersion(true)
  , mRenderTarget(
      [&]() {
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(pWindow, &windowWidth, &windowHeight);

        return renderer::RenderTargetTexture{
          &mRenderer, static_cast<size_t>(windowWidth), static_cast<size_t>(windowHeight)};
      }())
  , mpCurrentGameMode(std::make_unique<NullGameMode>())
  , mIsRunning(true)
  , mIsMinimized(false)
  , mUserProfile(loadOrCreateUserProfile(gamePath))
  , mScriptRunner(&mResources, &mRenderer, &mUserProfile.mSaveSlots, this)
  , mAllScripts(loadScripts(mResources))
  , mUiSpriteSheet(
      renderer::OwningTexture{
        &mRenderer, mResources.loadTiledFullscreenImage("STATUS.MNI")},
      &mRenderer)
  , mTextRenderer(&mUiSpriteSheet, &mRenderer, mResources)
{
}


void Game::run(const StartupOptions& startupOptions) {
  mRenderer.clear();
  mRenderer.swapBuffers();

  data::forEachSoundId([this](const auto id) {
    mSoundsById.emplace_back(mSoundSystem.addSound(mResources.loadSound(id)));
  });

  mMusicEnabled = startupOptions.mEnableMusic;

  // Check if running registered version
  if (
    mResources.mFilePackage.hasFile("LCR.MNI") &&
    mResources.mFilePackage.hasFile("O1.MNI")
  ) {
    mIsShareWareVersion = false;
  }

  if (startupOptions.mLevelToJumpTo)
  {
    auto [episode, level] = *startupOptions.mLevelToJumpTo;

    mpNextGameMode = std::make_unique<GameSessionMode>(
      data::GameSessionId{episode, level, data::Difficulty::Medium},
      makeModeContext(),
      startupOptions.mPlayerPosition);
  }
  else if (startupOptions.mSkipIntro)
  {
    mpNextGameMode = std::make_unique<MenuMode>(makeModeContext());
  }
  else
  {
    if (!mIsShareWareVersion) {
      showAntiPiracyScreen();
    }
    mpNextGameMode = std::make_unique<IntroDemoLoopMode>(
      makeModeContext(),
      true);
  }

  mainLoop();

  mUserProfile.saveToDisk();
}


void Game::showAntiPiracyScreen() {
  auto saved = setupSimpleUpscaling(&mRenderer);

  auto antiPiracyImage = mResources.loadAntiPiracyImage();
  renderer::OwningTexture imageTexture(&mRenderer, antiPiracyImage);
  imageTexture.render(&mRenderer, 0, 0);
  mRenderer.submitBatch();
  mRenderer.swapBuffers();

  SDL_Event event;
  while (SDL_WaitEvent(&event) && event.type != SDL_KEYDOWN);
}


void Game::mainLoop() {
  using namespace std::chrono;

  SDL_Event event;
  mLastTime = high_resolution_clock::now();

  for (;;) {
    const auto startOfFrame = high_resolution_clock::now();
    const auto elapsed =
      duration<entityx::TimeDelta>(startOfFrame - mLastTime).count();
    mLastTime = startOfFrame;

    ui::imgui_integration::beginFrame(mpWindow);
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    {
      RenderTargetBinder bindRenderTarget(mRenderTarget, &mRenderer);
      auto saved = setupSimpleUpscaling(&mRenderer);

      while (mIsMinimized && SDL_WaitEvent(&event)) {
        handleEvent(event);
      }
      while (SDL_PollEvent(&event)) {
        handleEvent(event);
      }
      if (!mIsRunning) {
        break;
      }

      if (mpNextGameMode) {
        fadeOutScreen();
        mpCurrentGameMode = std::move(mpNextGameMode);
        mpCurrentGameMode->updateAndRender(0);
        fadeInScreen();
      }

      mpCurrentGameMode->updateAndRender(elapsed);
    }

    mRenderer.clear();
    mRenderTarget.render(&mRenderer, 0, 0);
    mRenderer.submitBatch();

    if (mShowFps) {
      const auto afterRender = high_resolution_clock::now();
      const auto innerRenderTime =
        duration<engine::TimeDelta>(afterRender - startOfFrame).count();
      mFpsDisplay.updateAndRender(elapsed, innerRenderTime);
    }

    ui::imgui_integration::endFrame();
    mRenderer.swapBuffers();
  }
}


GameMode::Context Game::makeModeContext() {
  return {
    &mResources,
    &mRenderer,
    this,
    &mScriptRunner,
    &mAllScripts,
    &mTextRenderer,
    &mUiSpriteSheet,
    &mUserProfile};
}


void Game::handleEvent(const SDL_Event& event) {
  if (ui::imgui_integration::handleEvent(event)) {
    return;
  }

  switch (event.type) {
    case SDL_KEYUP:
      if (event.key.keysym.sym == SDLK_F6) {
        mShowFps = !mShowFps;
      }
      mpCurrentGameMode->handleEvent(event);
      break;

    case SDL_QUIT:
      mIsRunning = false;
      break;

    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_MINIMIZED) {
        mIsMinimized = true;
      } else if (event.window.event == SDL_WINDOWEVENT_RESTORED) {
        mIsMinimized = false;
      }
      break;

    default:
      mpCurrentGameMode->handleEvent(event);
      break;
  }
}


void Game::performScreenFadeBlocking(const bool doFadeIn) {
  using namespace std::chrono;

  if ((doFadeIn && mAlphaMod == 255) || (!doFadeIn && mAlphaMod == 0)) {
    // Already faded in/out, nothing to do
    return;
  }

  renderer::DefaultRenderTargetBinder bindDefaultRenderTarget(&mRenderer);

  engine::TimeDelta elapsedTime = 0.0;

  while (mIsRunning) {
    const auto now = high_resolution_clock::now();
    const auto timeDelta = duration<double>(now - mLastTime).count();
    mLastTime = now;

    elapsedTime += timeDelta;
    const auto fastTicksElapsed = engine::timeToFastTicks(elapsedTime);
    const auto fadeFactor = (fastTicksElapsed / 4.0) / 16.0;

    if (fadeFactor < 1.0) {
      const auto alpha = doFadeIn ? fadeFactor : 1.0 - fadeFactor;
      mAlphaMod = base::roundTo<std::uint8_t>(255.0 * alpha);
    } else {
      mAlphaMod = doFadeIn ? 255 : 0;
    }

    mRenderer.clear();

    mRenderer.setColorModulation({255, 255, 255, mAlphaMod});
    mRenderTarget.render(&mRenderer, 0, 0);
    mRenderer.swapBuffers();

    if (fadeFactor >= 1.0) {
      break;
    }
  }

  mRenderer.setColorModulation({255, 255, 255, 255});
}


void Game::fadeOutScreen() {
  performScreenFadeBlocking(false);

  // Clear render canvas after a fade-out
  RenderTargetBinder bindRenderTarget(mRenderTarget, &mRenderer);
  mRenderer.clear();
}


void Game::fadeInScreen() {
  performScreenFadeBlocking(true);
}


void Game::playSound(const data::SoundId id) {
  const auto index = static_cast<std::size_t>(id);
  assert(index < mSoundsById.size());

  const auto handle = mSoundsById[index];
  mSoundSystem.playSound(handle);
}


void Game::stopSound(const data::SoundId id) {
  const auto index = static_cast<std::size_t>(id);
  assert(index < mSoundsById.size());

  const auto handle = mSoundsById[index];
  mSoundSystem.stopSound(handle);
}


void Game::playMusic(const std::string& name) {
  if (!mMusicEnabled) {
    return;
  }

  mSoundSystem.playSong(mResources.loadMusic(name));
}


void Game::stopMusic() {
  mSoundSystem.stopMusic();
}


void Game::scheduleNewGameStart(
  const int episode,
  const data::Difficulty difficulty
) {
  mpNextGameMode = std::make_unique<GameSessionMode>(
    data::GameSessionId{episode, 0, difficulty},
    makeModeContext());
}


void Game::scheduleStartFromSavedGame(const data::SavedGame& save) {
  mpNextGameMode = std::make_unique<GameSessionMode>(
    save,
    makeModeContext());
}


void Game::scheduleEnterMainMenu() {
  mpNextGameMode = std::make_unique<MenuMode>(makeModeContext());
}


void Game::scheduleGameQuit() {
  mIsRunning = false;
}

}
