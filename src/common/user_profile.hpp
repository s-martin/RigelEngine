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

#include "data/high_score_list.hpp"
#include "data/saved_game.hpp"

#include <string>


namespace rigel {

class UserProfile {
public:
  UserProfile() = default;
  explicit UserProfile(const std::string& profilePath);

  void saveToDisk();
  void loadFromDisk();

  data::SaveSlotArray mSaveSlots;
  std::array<data::HighScoreList, data::NUM_EPISODES> mHighScoreLists;

private:
  std::optional<std::string> mProfilePath;
};


UserProfile loadOrCreateUserProfile(const std::string& gamePath);

}
