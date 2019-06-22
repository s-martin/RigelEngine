/* Copyright (C) 2019, Nikolai Wuttke. All rights reserved.
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

#include "engine/movement.hpp"
#include "game_logic/global_dependencies.hpp"

#include <variant>

namespace rigel::engine::events {
  struct CollidedWithWorld;
}


namespace rigel::game_logic {

namespace rigelatin_soldier {

struct Ready {};

struct Jumping {
  int mFramesElapsed = 0;
  engine::MovementResult mLastHorizontalMovementResult =
    engine::MovementResult::Failed;
  int mPreviousPosX = 0;
};

struct Waiting {
  int mFramesElapsed = 0;
};


using State = std::variant<Ready, Jumping, Waiting>;

}


namespace behaviors {

struct RigelatinSoldier {
  void update(
    GlobalDependencies& dependencies,
    GlobalState& state,
    bool isOnScreen,
    entityx::Entity entity);

  void onCollision(
    GlobalDependencies& dependencies,
    GlobalState& state,
    const engine::events::CollidedWithWorld& event,
    entityx::Entity entity);

  void updateReadyState(
    GlobalDependencies& dependencies,
    GlobalState& state,
    entityx::Entity entity);

  rigelatin_soldier::State mState;
  int mDecisionCounter = 3;
};

}}
