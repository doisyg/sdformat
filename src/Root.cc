/*
 * Copyright 2017 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
#include <string>
#include <vector>
#include <utility>

#include "sdf/Actor.hh"
#include "sdf/Light.hh"
#include "sdf/Model.hh"
#include "sdf/Root.hh"
#include "sdf/Types.hh"
#include "sdf/World.hh"
#include "sdf/parser.hh"
#include "sdf/sdf_config.h"
#include "FrameSemantics.hh"
#include "ScopedGraph.hh"
#include "Utils.hh"

using namespace sdf;

/// \brief Private data for sdf::Root
class sdf::RootPrivate
{
  /// \brief Version string
  public: std::string version = "";

  /// \brief The worlds specified under the root SDF element
  public: std::vector<World> worlds;

  /// \brief The models specified under the root SDF element
  public: std::unique_ptr<Model> model;

  /// \brief The lights specified under the root SDF element
  public: std::unique_ptr<Light> light;

  /// \brief The actors specified under the root SDF element
  public: std::unique_ptr<Actor> actor;

  /// \brief Frame Attached-To Graphs constructed when loading Worlds.
  public: std::vector<sdf::ScopedGraph<FrameAttachedToGraph>>
              worldFrameAttachedToGraphs;

  /// \brief Frame Attached-To Graph constructed when loading Models.
  public: sdf::ScopedGraph<FrameAttachedToGraph> modelFrameAttachedToGraph;

  /// \brief Pose Relative-To Graphs constructed when loading Worlds.
  public: std::vector<sdf::ScopedGraph<PoseRelativeToGraph>>
              worldPoseRelativeToGraphs;

  /// \brief Pose Relative-To Graph constructed when loading Models.
  public: sdf::ScopedGraph<PoseRelativeToGraph> modelPoseRelativeToGraph;

  /// \brief The SDF element pointer generated during load.
  public: sdf::ElementPtr sdf;
};

/////////////////////////////////////////////////
template <typename T>
sdf::ScopedGraph<FrameAttachedToGraph> addFrameAttachedToGraph(
    std::vector<sdf::ScopedGraph<sdf::FrameAttachedToGraph>> &_graphList,
    const T &_domObj, sdf::Errors &_errors)
{
  auto &frameGraph =
      _graphList.emplace_back(std::make_shared<FrameAttachedToGraph>());

  sdf::Errors buildErrors =
      sdf::buildFrameAttachedToGraph(frameGraph, &_domObj);
  _errors.insert(_errors.end(), buildErrors.begin(), buildErrors.end());

  sdf::Errors validateErrors = sdf::validateFrameAttachedToGraph(frameGraph);
  _errors.insert(_errors.end(), validateErrors.begin(), validateErrors.end());

  return frameGraph;
}

/////////////////////////////////////////////////
template <typename T>
ScopedGraph<PoseRelativeToGraph> addPoseRelativeToGraph(
    std::vector<sdf::ScopedGraph<sdf::PoseRelativeToGraph>> &_graphList,
    const T &_domObj, Errors &_errors)
{
  auto &poseGraph =
      _graphList.emplace_back(std::make_shared<sdf::PoseRelativeToGraph>());

  Errors buildErrors = buildPoseRelativeToGraph(poseGraph, &_domObj);
  _errors.insert(_errors.end(), buildErrors.begin(), buildErrors.end());

  Errors validateErrors = validatePoseRelativeToGraph(poseGraph);
  _errors.insert(_errors.end(), validateErrors.begin(), validateErrors.end());

  return poseGraph;
}

/////////////////////////////////////////////////
Root::Root()
  : dataPtr(new RootPrivate)
{
}

/////////////////////////////////////////////////
Root::Root(Root &&_root) noexcept
  : dataPtr(std::exchange(_root.dataPtr, nullptr))
{
}

/////////////////////////////////////////////////
Root &Root::operator=(Root &&_root) noexcept
{
  std::swap(this->dataPtr, _root.dataPtr);
  return *this;
}

/////////////////////////////////////////////////
Root::~Root()
{
  delete this->dataPtr;
  this->dataPtr = nullptr;
}

/////////////////////////////////////////////////
Errors Root::Load(const std::string &_filename)
{
  Errors errors;

  // Read an SDF file, and store the result in sdfParsed.
  SDFPtr sdfParsed = readFile(_filename, errors);

  // Return if we were not able to read the file.
  if (!sdfParsed)
  {
    errors.push_back(
        {ErrorCode::FILE_READ, "Unable to read file:" + _filename});
    return errors;
  }

  Errors loadErrors = this->Load(sdfParsed);
  errors.insert(errors.end(), loadErrors.begin(), loadErrors.end());

  return errors;
}

/////////////////////////////////////////////////
Errors Root::LoadSdfString(const std::string &_sdf)
{
  Errors errors;
  SDFPtr sdfParsed(new SDF());
  init(sdfParsed);

  // Read an SDF string, and store the result in sdfParsed.
  if (!readString(_sdf, sdfParsed, errors))
  {
    errors.push_back(
        {ErrorCode::STRING_READ, "Unable to SDF string: " + _sdf});
    return errors;
  }

  Errors loadErrors = this->Load(sdfParsed);
  errors.insert(errors.end(), loadErrors.begin(), loadErrors.end());

  return errors;
}

/////////////////////////////////////////////////
Errors Root::Load(SDFPtr _sdf)
{
  Errors errors;

  this->dataPtr->sdf = _sdf->Root();

  // Get the SDF version.
  std::pair<std::string, bool> versionPair =
    this->dataPtr->sdf->Get<std::string>("version", SDF_VERSION);

  // Check that the version exists. Exit if the version is missing.
  // readFile will fail if the version is missing, so this
  // check should never be triggered.
  if (!versionPair.second)
  {
    errors.push_back(
        {ErrorCode::ATTRIBUTE_MISSING, "SDF does not have a version."});
    return errors;
  }

  // Check that the version is the latest, since this is assumed by the DOM API
  if (SDF_PROTOCOL_VERSION != versionPair.first)
  {
    errors.push_back(
        {ErrorCode::ATTRIBUTE_INVALID,
        "SDF version attribute[" + versionPair.first + "] should match "
        "the latest version[" + SDF_PROTOCOL_VERSION + "] when loading DOM "
        "objects."});
    return errors;
  }

  this->dataPtr->version = versionPair.first;

  // Read all the worlds
  if (this->dataPtr->sdf->HasElement("world"))
  {
    ElementPtr elem = this->dataPtr->sdf->GetElement("world");
    while (elem)
    {
      World world;

      Errors worldErrors = world.Load(elem);

      // Build the graphs.
      auto frameAttachedToGraph = addFrameAttachedToGraph(
          this->dataPtr->worldFrameAttachedToGraphs, world, worldErrors);
      world.SetFrameAttachedToGraph(frameAttachedToGraph);

      auto poseRelativeToGraph = addPoseRelativeToGraph(
          this->dataPtr->worldPoseRelativeToGraphs, world, worldErrors);
      world.SetPoseRelativeToGraph(poseRelativeToGraph);

      // Attempt to load the world
      if (worldErrors.empty())
      {
        // Check that the world's name does not exist.
        if (this->WorldNameExists(world.Name()))
        {
          errors.push_back({ErrorCode::DUPLICATE_NAME,
                "World with name[" + world.Name() + "] already exists."
                " Each world must have a unique name. Skipping this world."});
        }
      }
      else
      {
        std::move(worldErrors.begin(), worldErrors.end(),
                  std::back_inserter(errors));
        errors.push_back({ErrorCode::ELEMENT_INVALID,
                          "Failed to load a world."});
      }

      this->dataPtr->worlds.push_back(std::move(world));
      elem = elem->GetNextElement("world");
    }
  }

  if (this->dataPtr->sdf->HasElement("model"))
  {
    sdf::ElementPtr model = this->dataPtr->sdf->GetElement("model");
    this->dataPtr->model = std::make_unique<sdf::Model>();
    sdf::Errors loadErrors = this->dataPtr->model->Load(model);
    errors.insert(errors.end(), loadErrors.begin(), loadErrors.end());

    this->dataPtr->modelFrameAttachedToGraph =
      sdf::ScopedGraph(std::make_shared<FrameAttachedToGraph>());

    sdf::Errors buildErrors =
        sdf::buildFrameAttachedToGraph(
          this->dataPtr->modelFrameAttachedToGraph,
          this->dataPtr->model.get());
    errors.insert(errors.end(), buildErrors.begin(), buildErrors.end());

    sdf::Errors validateErrors =
      sdf::validateFrameAttachedToGraph(
        this->dataPtr->modelFrameAttachedToGraph);
    errors.insert(errors.end(), validateErrors.begin(), validateErrors.end());

    this->dataPtr->model->SetFrameAttachedToGraph(
      this->dataPtr->modelFrameAttachedToGraph);

    this->dataPtr->modelPoseRelativeToGraph =
      sdf::ScopedGraph(std::make_shared<PoseRelativeToGraph>());

    Errors buildPoseErrors =
      buildPoseRelativeToGraph(
        this->dataPtr->modelPoseRelativeToGraph,
        this->dataPtr->model.get());
    errors.insert(
      errors.end(), buildPoseErrors.begin(), buildPoseErrors.end());

    Errors validatePoseErrors =
      validatePoseRelativeToGraph(
        this->dataPtr->modelPoseRelativeToGraph);
    errors.insert(
      errors.end(), validatePoseErrors.begin(), validatePoseErrors.end());

    this->dataPtr->model->SetPoseRelativeToGraph(
      this->dataPtr->modelPoseRelativeToGraph);
  }

  // Load all the lights.
  if (this->dataPtr->sdf->HasElement("light"))
  {
    sdf::ElementPtr lightPtr = this->dataPtr->sdf->GetElement("light");
    this->dataPtr->light = std::make_unique<sdf::Light>();
    sdf::Errors buildErrors = this->dataPtr->light->Load(lightPtr);
    errors.insert(errors.end(), buildErrors.begin(), buildErrors.end());
  }

  if (this->dataPtr->sdf->HasElement("actor"))
  {
    sdf::ElementPtr actorPtr = this->dataPtr->sdf->GetElement("actor");
    this->dataPtr->actor = std::make_unique<sdf::Actor>();
    sdf::Errors buildErrors = this->dataPtr->actor->Load(actorPtr);
    errors.insert(errors.end(), buildErrors.begin(), buildErrors.end());
  }

  return errors;
}

/////////////////////////////////////////////////
std::string Root::Version() const
{
  return this->dataPtr->version;
}

/////////////////////////////////////////////////
void Root::SetVersion(const std::string &_version)
{
  this->dataPtr->version = _version;
}

/////////////////////////////////////////////////
uint64_t Root::WorldCount() const
{
  return this->dataPtr->worlds.size();
}

/////////////////////////////////////////////////
const World *Root::WorldByIndex(const uint64_t _index) const
{
  if (_index < this->dataPtr->worlds.size())
    return &this->dataPtr->worlds[_index];
  return nullptr;
}

/////////////////////////////////////////////////
bool Root::WorldNameExists(const std::string &_name) const
{
  for (auto const &w : this->dataPtr->worlds)
  {
    if (w.Name() == _name)
    {
      return true;
    }
  }
  return false;
}

/////////////////////////////////////////////////
const Model *Root::Model() const
{
  return this->dataPtr->model.get();
}

/////////////////////////////////////////////////
const Light *Root::Light() const
{
  return this->dataPtr->light.get();
}

/////////////////////////////////////////////////
const Actor *Root::Actor() const
{
  return this->dataPtr->actor.get();
}

/////////////////////////////////////////////////
sdf::ElementPtr Root::Element() const
{
  return this->dataPtr->sdf;
}
