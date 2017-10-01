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

#pragma once

#include "engine/collision_checker.hpp"
#include "engine/debugging_system.hpp"
#include "engine/entity_activation_system.hpp"
#include "engine/life_time_system.hpp"
#include "engine/physics_system.hpp"
#include "engine/rendering_system.hpp"
#include "engine/rendering_system.hpp"
#include "game_logic/ai/blue_guard.hpp"
#include "game_logic/ai/hover_bot.hpp"
#include "game_logic/ai/laser_turret.hpp"
#include "game_logic/ai/messenger_drone.hpp"
#include "game_logic/ai/prisoner.hpp"
#include "game_logic/ai/rocket_turret.hpp"
#include "game_logic/ai/security_camera.hpp"
#include "game_logic/ai/simple_walker.hpp"
#include "game_logic/ai/sliding_door.hpp"
#include "game_logic/ai/slime_blob.hpp"
#include "game_logic/ai/slime_pipe.hpp"
#include "game_logic/damage_infliction_system.hpp"
#include "game_logic/interaction/elevator.hpp"
#include "game_logic/item_container.hpp"
#include "game_logic/map_scroll_system.hpp"
#include "game_logic/player/animation_system.hpp"
#include "game_logic/player/attack_system.hpp"
#include "game_logic/player/damage_system.hpp"
#include "game_logic/player_interaction_system.hpp"
#include "game_logic/player_movement_system.hpp"

namespace rigel {

namespace data { struct PlayerModel; }
namespace engine { class RandomNumberGenerator; }
namespace game_logic { class EntityFactory; }

}


namespace rigel { namespace game_logic {

class IngameSystems {
public:
  IngameSystems(
    data::Difficulty difficulty,
    base::Vector* pScrollOffset,
    entityx::Entity playerEntity,
    data::PlayerModel* pPlayerModel,
    data::map::Map* pMap,
    engine::MapRenderer::MapRenderData&& mapRenderData,
    IGameServiceProvider* pServiceProvider,
    EntityFactory* pEntityFactory,
    engine::RandomNumberGenerator* pRandomGenerator,
    engine::Renderer* pRenderer,
    entityx::EntityManager& entities,
    entityx::EventManager& eventManager,
    entityx::SystemManager& systems);

  void update(
    const PlayerInputState& inputState,
    entityx::EntityManager& entities);
  void render();

  void buttonStateChanged(const PlayerInputState& inputState);

  engine::DebuggingSystem& debuggingSystem();

  void switchBackdrops();

  entityx::Entity getAndResetActiveTeleporter();

private:
  entityx::Entity mPlayerEntity;
  base::Vector* mpScrollOffset;
  engine::CollisionChecker mCollisionChecker;
  engine::PhysicsSystem mPhysicsSystem;

  game_logic::MapScrollSystem mMapScrollSystem;
  game_logic::PlayerMovementSystem mPlayerMovementSystem;
  game_logic::player::AttackSystem<EntityFactory> mPlayerAttackSystem;
  game_logic::interaction::ElevatorSystem mElevatorSystem;

  game_logic::NapalmBombSystem mNapalmBombSystem;

  game_logic::ai::BlueGuardSystem mBlueGuardSystem;
  game_logic::ai::HoverBotSystem mHoverBotSystem;
  game_logic::ai::SimpleWalkerSystem mSimpleWalkerSystem;
  game_logic::ai::SlimeBlobSystem mSlimeBlobSystem;

  data::PlayerModel* mpPlayerModel;

  entityx::SystemManager& mSystems;

  entityx::Entity mActiveTeleporter;
};

}}
