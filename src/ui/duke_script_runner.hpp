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

#include "common/game_mode.hpp"
#include "data/actor_ids.hpp"
#include "data/duke_script.hpp"
#include "data/saved_game.hpp"
#include "engine/tiled_texture.hpp"
#include "engine/timing.hpp"
#include "loader/palette.hpp"
#include "renderer/texture.hpp"
#include "ui/menu_element_renderer.hpp"

#include <cstddef>
#include <optional>


namespace rigel::ui {

class DukeScriptRunner {
public:
  enum class ScriptTerminationType {
    RanToCompletion,
    AbortedByUser,
    MenuItemSelected
  };

  struct ExecutionResult {
    ScriptTerminationType mTerminationType;
    std::optional<int> mSelectedPage;
  };

  DukeScriptRunner(
    loader::ResourceLoader* pResourceLoader,
    renderer::Renderer* pRenderer,
    const data::SaveSlotArray* pSaveSlots,
    IGameServiceProvider* pServiceProvider);

  void executeScript(const data::script::Script& script);

  bool hasFinishedExecution() const;
  std::optional<ExecutionResult> result() const;

  void updateAndRender(engine::TimeDelta dt);
  void handleEvent(const SDL_Event& event);

private:
  enum class State {
    ReadyToExecute,
    ExecutingScript,
    AwaitingUserInput,
    FinishedExecution,
    ExecutionInterrupted
  };

  struct DelayState {
    explicit DelayState(const int ticksToWait)
      : mTicksToWait(ticksToWait)
    {
    }

    int mTicksToWait;
    engine::TimeDelta mElapsedTime = 0;
  };

  struct NewsReporterState {
    explicit NewsReporterState(const int talkDuration)
      : mTalkDuration(talkDuration)
    {
    }

    int mTalkDuration;
    int mLastTalkFrame = -1;
    engine::TimeDelta mElapsedTime = 0;
  };

  struct MenuSelectionIndicatorState {
    explicit MenuSelectionIndicatorState(const int posY)
      : mPosY(posY)
    {
    }

    int mPosY;
    engine::TimeDelta mElapsedTime = 0;
  };

  enum PagingMode {
    Menu,
    PagingOnly
  };

  struct PagerState {
    std::vector<data::script::Script> mPageScripts;
    PagingMode mMode;
    int mCurrentPageIndex;
    int mMaxPageIndex;
  };

  struct CheckBoxState {
    int mPosY;
    bool mChecked;
    data::script::SetupCheckBoxes::CheckBoxID mID;
  };

  struct CheckBoxesState {
    int mPosX;
    std::vector<CheckBoxState> mStates;
    int mCurrentMenuPosY;
  };

  void startExecution(const data::script::Script& script);
  void interpretNextAction();

  bool isInWaitState() const;
  void clearWaitState();

  void drawSprite(data::ActorID id, int frame, int x, int y);
  void updatePalette(const loader::Palette16& palette);

  void drawSaveSlotNames(int selectedIndex);

  void drawCurrentKeyBindings();

  bool hasMenuPages() const;
  void selectNextPage(PagerState& state);
  void selectPreviousPage(PagerState& state);
  void onPageChanged(PagerState& state);
  void executeCurrentPageScript(PagerState& state);
  void selectCurrentMenuItem(PagerState& state);
  void showMenuSelectionIndicator(int y);
  void hideMenuSelectionIndicator();

  void drawMenuSelectionIndicator(
    MenuSelectionIndicatorState& state, engine::TimeDelta dt);
  void clearMenuSelectionIndicator(const MenuSelectionIndicatorState& state);

  bool hasCheckBoxes() const;
  void displayCheckBoxes(const CheckBoxesState& state);

  void handleDelay(DelayState& state, engine::TimeDelta timeDelta);
  void animateNewsReporter(
    NewsReporterState& state,
    engine::TimeDelta timeDelta);
  void stopNewsReporterAnimation();

  void drawBigText(int x, int y, int colorIndex, const std::string& text) const;

private:
  const loader::ResourceLoader* mpResourceBundle;
  loader::Palette16 mCurrentPalette;
  renderer::Renderer* mpRenderer;
  const data::SaveSlotArray* mpSaveSlots;
  IGameServiceProvider* mpServices;
  engine::TiledTexture mUiSpriteSheetRenderer;
  MenuElementRenderer mMenuElementRenderer;


  data::script::Script mCurrentInstructions;
  std::size_t mProgramCounter;
  State mState = State::ReadyToExecute;

  std::optional<DelayState> mDelayState;
  std::optional<NewsReporterState> mNewsReporterAnimationState;

  std::optional<PagerState> mPagerState;
  bool mMenuItemWasSelected = false;
  std::unordered_map<int, int> mPersistentMenuSelections;
  std::optional<MenuSelectionIndicatorState> mMenuSelectionIndicatorState;
  std::optional<MenuSelectionIndicatorState> mPreviousSelectionIndicatorState;
  std::optional<int> mCurrentPersistentSelectionSlot;

  std::optional<CheckBoxesState> mCheckBoxStates;

  bool mFadeInBeforeNextWaitStateScheduled = false;
  bool mDisableMenuFunctionalityForNextPagesDefinition = false;
  bool mTextBoxOffsetEnabled = false;
};

}
