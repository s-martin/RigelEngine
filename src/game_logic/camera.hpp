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

#include "base/warnings.hpp"
#include "base/spatial_types.hpp"
#include "engine/base_components.hpp"
#include "game_logic/input.hpp"
#include "game_logic/player/components.hpp"

RIGEL_DISABLE_WARNINGS
#include <entityx/entityx.h>
RIGEL_RESTORE_WARNINGS

namespace rigel::data::map { class Map; }
namespace rigel::events { struct PlayerFiredShot; }


namespace rigel::game_logic {


class Player;

class Camera : public entityx::Receiver<Camera> {
public:
  Camera(
    const Player* pPlayer,
    const data::map::Map& map,
    entityx::EventManager& eventManager);

  void update(const PlayerInput& input);
  void centerViewOnPlayer();

  const base::Vector& position() const;

  void receive(const rigel::events::PlayerFiredShot& event);

private:
  void updateManualScrolling(const PlayerInput& input);
  void updateAutomaticScrolling();
  void setPosition(base::Vector position);

  const Player* mpPlayer;
  base::Vector mPosition;
  base::Extents mMaxPosition;
  int mManualScrollCooldown = 0;
};


inline const base::Vector& Camera::position() const {
  return mPosition;
}

}
