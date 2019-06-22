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

#include "resource_loader.hpp"

#include "base/container_utils.hpp"
#include "data/game_traits.hpp"
#include "data/unit_conversions.hpp"
#include "loader/ega_image_decoder.hpp"
#include "loader/file_utils.hpp"
#include "loader/movie_loader.hpp"
#include "loader/music_loader.hpp"
#include "loader/voc_decoder.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>


namespace rigel::loader {

using namespace data;
using namespace std;


namespace {

const auto ANTI_PIRACY_SCREEN_FILENAME = "LCR.MNI";

const auto FULL_SCREEN_IMAGE_DATA_SIZE =
  (GameTraits::viewPortWidthPx * GameTraits::viewPortHeightPx) /
  (GameTraits::pixelsPerEgaByte / GameTraits::egaPlanes);

}

// When loading assets, the game will first check if a file with an expected
// name exists at the replacements path, and if it does, it will load this file
// and use it instead of the asset from the original data file (NUKEM2.CMP).
//
// At the moment, this is only implemented for sprites/actors. The expected
// format for replacement files is:
//
//   actor<actor_id>_frame<animation_frame>.png
//
// Where <actor_id> and <animation_frame> should be replaced with the
// corresponding numbers. For example, to replace the images used for the
// "blue guard" enemy, files named "actor_159_frame0.png" up to
// "actor_159_frame12.png" should be provided.
//
// The files can contain full 32-bit RGBA values, there are no limitations.
const auto ASSET_REPLACEMENTS_PATH = "asset_replacements";


ResourceLoader::ResourceLoader(const std::string& gamePath)
  : mFilePackage(gamePath + "NUKEM2.CMP")
  , mActorImagePackage(mFilePackage, gamePath + "/" + ASSET_REPLACEMENTS_PATH)
  , mGamePath(gamePath)
  , mAdlibSoundsPackage(mFilePackage)
{
}


data::Image ResourceLoader::loadTiledFullscreenImage(
  const std::string& name
) const {
  return loadTiledFullscreenImage(name, INGAME_PALETTE);
}


data::Image ResourceLoader::loadTiledFullscreenImage(
  const std::string& name,
  const Palette16& overridePalette
) const {
  return loadTiledImage(
    mFilePackage.file(name),
    data::GameTraits::viewPortWidthTiles,
    overridePalette,
    data::TileImageType::Unmasked);
}


data::Image ResourceLoader::loadStandaloneFullscreenImage(
  const std::string& name
) const {
  const auto& data = mFilePackage.file(name);
  const auto paletteStart = data.cbegin() + FULL_SCREEN_IMAGE_DATA_SIZE;
  const auto palette = load6bitPalette16(
    paletteStart,
    data.cend());

  auto pixels = decodeSimplePlanarEgaBuffer(
    data.cbegin(),
    data.cbegin() + FULL_SCREEN_IMAGE_DATA_SIZE,
    palette);
  return data::Image(
    std::move(pixels),
    GameTraits::viewPortWidthPx,
    GameTraits::viewPortHeightPx);
}


data::Image ResourceLoader::loadAntiPiracyImage() const {
  using namespace std;

  // For some reason, the anti-piracy screen is in a different format than all
  // the other full-screen images. It first defines a 256-color VGA palette,
  // then defines the pixel data in linear format.
  //
  // See http://www.shikadi.net/moddingwiki/Duke_Nukem_II_Full-screen_Images
  const auto& data = mFilePackage.file(ANTI_PIRACY_SCREEN_FILENAME);
  const auto iImageStart = cbegin(data) + 256*3;
  const auto palette = load6bitPalette256(cbegin(data), iImageStart);

  data::PixelBuffer pixels;
  pixels.reserve(GameTraits::viewPortWidthPx * GameTraits::viewPortHeightPx);
  transform(iImageStart, cend(data), back_inserter(pixels),
    [&palette](const auto indexedPixel) { return palette[indexedPixel]; });
  return data::Image(
    move(pixels),
    GameTraits::viewPortWidthPx,
    GameTraits::viewPortHeightPx);
}


loader::Palette16 ResourceLoader::loadPaletteFromFullScreenImage(
  const std::string& imageName
) const {
  const auto& data = mFilePackage.file(imageName);
  const auto paletteStart = data.cbegin() + FULL_SCREEN_IMAGE_DATA_SIZE;
  return load6bitPalette16(paletteStart, data.cend());
}


TileSet ResourceLoader::loadCZone(const std::string& name) const {
  using namespace data;
  using namespace map;
  using T = data::TileImageType;

  const auto& data = mFilePackage.file(name);
  LeStreamReader attributeReader(
    data.cbegin(), data.cbegin() + GameTraits::CZone::attributeBytesTotal);

  vector<uint16_t> attributes;
  attributes.reserve(GameTraits::CZone::numTilesTotal);
  for (TileIndex index=0; index<GameTraits::CZone::numTilesTotal; ++index) {
    attributes.push_back(attributeReader.readU16());

    if (index >= GameTraits::CZone::numSolidTiles) {
      attributeReader.skipBytes(sizeof(uint16_t) * 4);
    }
  }

  Image fullImage(
    tilesToPixels(GameTraits::CZone::tileSetImageWidth),
    tilesToPixels(GameTraits::CZone::tileSetImageHeight));

  const auto tilesBegin =
    data.cbegin() + GameTraits::CZone::attributeBytesTotal;
  const auto maskedTilesBegin =
    tilesBegin + GameTraits::CZone::numSolidTiles*GameTraits::CZone::tileBytes;

  const auto solidTilesImage = loadTiledImage(
    tilesBegin,
    maskedTilesBegin,
    GameTraits::CZone::tileSetImageWidth,
    INGAME_PALETTE,
    T::Unmasked);
  const auto maskedTilesImage = loadTiledImage(
    maskedTilesBegin,
    data.cend(),
    GameTraits::CZone::tileSetImageWidth,
    INGAME_PALETTE,
    T::Masked);
  fullImage.insertImage(0, 0, solidTilesImage);
  fullImage.insertImage(
    0,
    tilesToPixels(GameTraits::CZone::solidTilesImageHeight),
    maskedTilesImage);

  return {move(fullImage), TileAttributeDict{move(attributes)}};
}


data::Movie ResourceLoader::loadMovie(const std::string& name) const {
  return loader::loadMovie(loadFile(mGamePath + name));
}


data::Song ResourceLoader::loadMusic(const std::string& name) const {
  return loader::loadSong(mFilePackage.file(name));
}


data::AudioBuffer ResourceLoader::loadSound(const data::SoundId id) const {
  static const std::map<data::SoundId, const char*> INTRO_SOUND_MAP{
    {data::SoundId::IntroGunShot, "INTRO3.MNI"},
    {data::SoundId::IntroGunShotLow, "INTRO4.MNI"},
    {data::SoundId::IntroEmptyShellsFalling, "INTRO5.MNI"},
    {data::SoundId::IntroTargetMovingCloser, "INTRO6.MNI"},
    {data::SoundId::IntroTargetStopsMoving, "INTRO7.MNI"},
    {data::SoundId::IntroDukeSpeaks1, "INTRO8.MNI"},
    {data::SoundId::IntroDukeSpeaks2, "INTRO9.MNI"}
  };

  const auto introSoundIter = INTRO_SOUND_MAP.find(id);

  if (introSoundIter != INTRO_SOUND_MAP.end()) {
    return loadSound(introSoundIter->second);
  }

  const auto digitizedSoundFileName =
    string("SB_") + to_string(static_cast<int>(id) + 1) + ".MNI";
  if (mFilePackage.hasFile(digitizedSoundFileName)) {
    return loadSound(digitizedSoundFileName);
  } else {
    return mAdlibSoundsPackage.loadAdlibSound(id);
  }
}


data::AudioBuffer ResourceLoader::loadSound(const std::string& name) const {
  return loader::decodeVoc(mFilePackage.file(name));
}


ScriptBundle ResourceLoader::loadScriptBundle(
  const std::string& fileName
) const {
  return loader::loadScripts(mFilePackage.fileAsText(fileName));
}

}

