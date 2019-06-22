/* Copyright (C) 2017, Nikolai Wuttke. All rights reserved.
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

#include "spike_ball.hpp"

#include "common/game_service_provider.hpp"
#include "data/sound_ids.hpp"
#include "engine/collision_checker.hpp"
#include "engine/entity_tools.hpp"
#include "engine/life_time_components.hpp"
#include "engine/physical_components.hpp"
#include "game_logic/damage_components.hpp"


namespace rigel::game_logic::ai {

using namespace engine::components;

namespace {

base::Point<float> JUMP_ARC[] = {
  {0.0f, -2.0f},
  {0.0f, -2.0f},
  {0.0f, -1.0f},
  {0.0f, -1.0f},
  {0.0f, -1.0f}
};


void startJump(entityx::Entity entity) {
  engine::reassign<MovementSequence>(entity, JUMP_ARC, true, false);
}

}


void configureSpikeBall(entityx::Entity entity) {
  using namespace engine::components::parameter_aliases;

  entity.assign<ActivationSettings>(
    ActivationSettings::Policy::AlwaysAfterFirstActivation);
  entity.assign<MovingBody>(Velocity{}, GravityAffected{true});
  entity.assign<components::SpikeBall>();

  startJump(entity);
}


SpikeBallSystem::SpikeBallSystem(
  const engine::CollisionChecker* pCollisionChecker,
  IGameServiceProvider* pServiceProvider,
  entityx::EventManager& events
)
  : mpCollisionChecker(pCollisionChecker)
  , mpServiceProvider(pServiceProvider)
{
  events.subscribe<engine::events::CollidedWithWorld>(*this);
  events.subscribe<events::ShootableDamaged>(*this);
}


void SpikeBallSystem::update(entityx::EntityManager& es) {
  es.each<components::SpikeBall, WorldPosition, BoundingBox, Active>([this](
    entityx::Entity entity,
    components::SpikeBall& state,
    const WorldPosition& position,
    const BoundingBox& bounds,
    const Active&
  ) {
    if (state.mJumpBackCooldown > 0) {
      --state.mJumpBackCooldown;
    }

    const auto onSolidGround = mpCollisionChecker->isOnSolidGround(
      position, bounds);
    if (state.mJumpBackCooldown == 0 && onSolidGround) {
      jump(entity);
    }
  });
}


void SpikeBallSystem::receive(const events::ShootableDamaged& event) {
  auto entity = event.mEntity;
  if (!entity.has_component<components::SpikeBall>()) {
    return;
  }

  auto& body = *entity.component<MovingBody>();
  body.mVelocity.x = event.mInflictorVelocity.x > 0 ? 1.0f : -1.0f;
}


void SpikeBallSystem::receive(const engine::events::CollidedWithWorld& event) {
  auto entity = event.mEntity;
  if (!entity.has_component<components::SpikeBall>()) {
    return;
  }

  auto& body = *entity.component<MovingBody>();
  if (event.mCollidedLeft) {
    body.mVelocity.x = 1.0f;
  } else if (event.mCollidedRight) {
    body.mVelocity.x = -1.0f;
  }

  if (event.mCollidedTop) {
    if (entity.component<Active>()->mIsOnScreen) {
      mpServiceProvider->playSound(data::SoundId::DukeJumping);
    }

    entity.component<components::SpikeBall>()->mJumpBackCooldown = 3;

    engine::removeSafely<MovementSequence>(entity);
    body.mVelocity.y = 0.0f;
  }
}


void SpikeBallSystem::jump(entityx::Entity entity) {
  auto& state = *entity.component<components::SpikeBall>();
  if (state.mJumpBackCooldown > 0) {
    return;
  }

  state.mJumpBackCooldown = 9;
  startJump(entity);

  if (entity.component<Active>()->mIsOnScreen) {
    mpServiceProvider->playSound(data::SoundId::DukeJumping);
  }
}


}
