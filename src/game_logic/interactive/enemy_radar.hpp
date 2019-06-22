/* Copyright (C) 2018, Nikolai Wuttke. All rights reserved.
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

RIGEL_DISABLE_WARNINGS
#include <entityx/entityx.h>
RIGEL_RESTORE_WARNINGS


namespace rigel::game_logic {

namespace components {

struct RadarDish {};


struct RadarComputer {
  int mAnimationStep = 0;
};

}


class RadarDishCounter : public entityx::Receiver<RadarDishCounter> {
public:
  RadarDishCounter(
    entityx::EntityManager& entities,
    entityx::EventManager& events);

  int numRadarDishes() const;
  bool radarDishesPresent() const;

  void receive(
    const entityx::ComponentAddedEvent<components::RadarDish>& event);
  void receive(
    const entityx::ComponentRemovedEvent<components::RadarDish>& event);

private:
  int mNumRadarDishes = 0;
};


class RadarComputerSystem {
public:
  explicit RadarComputerSystem(const RadarDishCounter* pCounter);

  void update(entityx::EntityManager& es);

private:
  const RadarDishCounter* mpCounter;
  bool mIsOddFrame = false;
};

}
