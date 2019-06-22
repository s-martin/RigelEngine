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
#include "engine/base_components.hpp"

RIGEL_DISABLE_WARNINGS
#include <entityx/entityx.h>
RIGEL_RESTORE_WARNINGS

namespace rigel { struct IGameServiceProvider; }
namespace rigel::engine {
  class ParticleSystem;
  class RandomNumberGenerator;

  namespace components { struct Sprite; }
}
namespace rigel::game_logic { class EntityFactory; }
namespace rigel::game_logic::events {
  struct ShootableKilled;
}


namespace rigel::game_logic::ai {

namespace components {

struct Prisoner {
  explicit Prisoner(const bool isAggressive)
    : mIsAggressive(isAggressive)
  {
  }

  int mGrabStep = 0;
  bool mIsAggressive;
  bool mIsGrabbing = false;
};

}


class PrisonerSystem : public entityx::Receiver<PrisonerSystem> {
public:
  PrisonerSystem(
    entityx::Entity player,
    EntityFactory* pEntityFactory,
    IGameServiceProvider* pServiceProvider,
    engine::ParticleSystem* pParticles,
    engine::RandomNumberGenerator* pRandomGenerator,
    entityx::EventManager& events);

  void update(entityx::EntityManager& es);

  void receive(const events::ShootableKilled& event);

private:
  void updateAggressivePrisoner(
    entityx::Entity entity,
    const engine::components::WorldPosition& position,
    components::Prisoner& state,
    engine::components::Sprite& sprite
  );

private:
  entityx::Entity mPlayer;
  EntityFactory* mpEntityFactory;
  IGameServiceProvider* mpServiceProvider;
  engine::ParticleSystem* mpParticles;
  engine::RandomNumberGenerator* mpRandomGenerator;
  bool mIsOddFrame = false;
};

}
