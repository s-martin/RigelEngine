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

#include "base/warnings.hpp"
#include "engine/opengl.hpp"
#include "sdl_utils/error.hpp"
#include "sdl_utils/ptr.hpp"

#include "game_main.hpp"

RIGEL_DISABLE_WARNINGS
#include <SDL.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <clara.hpp>
RIGEL_RESTORE_WARNINGS

#include <iostream>
#include <stdexcept>
#include <string>


using namespace rigel;
using namespace rigel::sdl_utils;
using namespace std;

namespace ba = boost::algorithm;


namespace {

#if defined( __APPLE__) || defined(RIGEL_USE_GL_ES)
  const auto WINDOW_FLAGS = SDL_WINDOW_FULLSCREEN;
#else
  const auto WINDOW_FLAGS = SDL_WINDOW_BORDERLESS;
#endif


// Default values for screen resolution in case we can't figure out the
// current Desktop size
const auto DEFAULT_RESOLUTION_X = 1920;
const auto DEFAULT_RESOLUTION_Y = 1080;


struct SdlInitializer {
  SdlInitializer() {
    throwIfFailed([]() { return SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO); });
  }
  ~SdlInitializer() {
    SDL_Quit();
  }

  SdlInitializer(const SdlInitializer&) = delete;
  SdlInitializer& operator=(const SdlInitializer&) = delete;
};


class OpenGlContext {
public:
  explicit OpenGlContext(SDL_Window* pWindow)
    : mpOpenGLContext(
        throwIfCreationFailed([=]() { return SDL_GL_CreateContext(pWindow); }))
  {
  }
  ~OpenGlContext() {
    SDL_GL_DeleteContext(mpOpenGLContext);
  }

  OpenGlContext(const OpenGlContext&) = delete;
  OpenGlContext& operator=(const OpenGlContext&) = delete;

private:
  SDL_GLContext mpOpenGLContext;
};


SDL_Window* createWindow() {
  SDL_DisplayMode displayMode;
  if (SDL_GetDesktopDisplayMode(0, &displayMode) != 0) {
    displayMode.w = DEFAULT_RESOLUTION_X;
    displayMode.h = DEFAULT_RESOLUTION_Y;
  }

  return throwIfCreationFailed([&displayMode]() {
    return SDL_CreateWindow(
      "Rigel Engine",
      SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED,
      displayMode.w,
      displayMode.h,
      WINDOW_FLAGS | SDL_WINDOW_OPENGL);
  });
}


void showBanner() {
  cout <<
    "================================================================================\n"
    "                            Welcome to RIGEL ENGINE!\n"
    "\n"
    "  A modern reimplementation of the game Duke Nukem II, originally released in\n"
    "  1993 for MS-DOS by Apogee Software.\n"
    "\n"
    "You need the original game's data files in order to play, e.g. the freely\n"
    "available shareware version.\n"
    "\n"
    "Rigel Engine Copyright (C) 2016, Nikolai Wuttke.\n"
    "Rigel Engine comes with ABSOLUTELY NO WARRANTY. This is free software, and you\n"
    "are welcome to redistribute it under certain conditions.\n"
    "For details, see https://www.gnu.org/licenses/gpl-2.0.html\n"
    "================================================================================\n"
    "\n";
}


void initAndRunGame(const string& gamePath, const GameOptions& gameOptions) {
  SdlInitializer initializeSDL;

  throwIfFailed([]() { return SDL_GL_LoadLibrary(nullptr); });

#ifdef RIGEL_USE_GL_ES
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  Ptr<SDL_Window> pWindow(createWindow());
  OpenGlContext glContext(pWindow.get());
  engine::loadGlFunctions();

  // We don't care if screen saver disabling failed, it's not that important.
  // So no return value checking.
  SDL_DisableScreenSaver();

  // Same for the cursor disabling.
  SDL_ShowCursor(SDL_DISABLE);

  gameMain(gamePath, gameOptions, pWindow.get());
}

}


int main(int argc, char** argv) {
  showBanner();

  string gamePath;
  GameOptions gameOptions;
  bool showHelp = false;

  auto parseLevelToPlay = [&gameOptions](const string& levelToPlay) {
    if (levelToPlay.size() != 2) {
      throw invalid_argument("Invalid level name");
    }

    const auto episode = static_cast<int>(levelToPlay[0] - 'L');
    const auto level = static_cast<int>(levelToPlay[1] - '0') - 1;

    if (episode < 0 || episode >= 4 || level < 0 || level >= 8) {
      throw invalid_argument(string("Invalid level name: ") + levelToPlay);
    }

    gameOptions.mLevelToJumpTo = std::make_pair(episode, level);
  };

  auto parsePlayerPos = [&gameOptions](const string& playerPosString) {
    std::vector<std::string> positionParts;
    ba::split(positionParts, playerPosString, ba::is_any_of(","));

    if (
      positionParts.size() != 2 ||
      positionParts[0].empty() ||
      positionParts[1].empty()
    ) {
      throw invalid_argument(
        "Invalid x/y-position (specify using '<X>,<Y>')");
    }

    const auto position = base::Vector{
      std::stoi(positionParts[0]),
      std::stoi(positionParts[1])
    };
    gameOptions.mPlayerPosition = position;
  };

  using namespace clara;
  auto commandLineParser
    = Help(showHelp)
    + Opt(gameOptions.mSkipIntro, "skip-intro")
      ["-s"]["--skip-intro"]
      ("Skip intro movies/Apogee logo, go straight to main menu")
    + Opt([&parseLevelToPlay](const string& l) { parseLevelToPlay(l); }, "play-level")
      ["-p"]["--play-level"]
      ("Directly jump to given map, skipping intro/menu etc.")
    + Opt([&gameOptions](const bool disableMusic) { gameOptions.mEnableMusic = !disableMusic;}, "no-music")
      ["--no-music"]
      ("Disable music playback")
    + Opt([&parsePlayerPos](const string& p) { parsePlayerPos(p); }, "player-pos")
    //+ Opt(parsePlayerPos, "player-pos")
      ["--player-pos"]
      ("Specify position to place the player at (to be used in conjunction with"
      "\n'play-level')")
    + Arg(gamePath, "game-path")
      ("Path to original game's installation. Can also be given as positional "
      "argument.");

  try
  {
    const auto result = commandLineParser.parse(Args(argc, argv));
    if (!result) {
      std::cerr << result.errorMessage() << '\n';
    }
    if (showHelp) {
      cout << commandLineParser << '\n';
      return 0;
    }

    cout << gamePath << "P\n";

    if (!gamePath.empty() && gamePath.back() != '/') {
      gamePath += "/";
    }

    initAndRunGame(gamePath, gameOptions);
  }
  catch (const std::invalid_argument& err)
  {
    cerr << "ERROR: " << err.what() << "\n\n";
    cerr << commandLineParser << '\n';
    return -1;
  }
  catch (const std::exception& ex)
  {
    cerr << "ERROR: " << ex.what() << '\n';
    return -2;
  }
  catch (...)
  {
    cerr << "UNKNOWN ERROR\n";
    return -3;
  }

  return 0;
}
