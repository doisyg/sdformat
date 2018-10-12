/*
 * Copyright 2018 Open Source Robotics Foundation
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
#include <ignition/math/graph/Graph.hh>
#include <ignition/math/Pose3.hh>
#include "sdf/Error.hh"
#include "sdf/Joint.hh"
#include "sdf/Link.hh"
#include "sdf/Model.hh"
#include "sdf/Types.hh"
#include "Utils.hh"

using namespace sdf;
using namespace ignition::math;

class sdf::ModelPrivate
{
  /// \brief Name of the model.
  public: std::string name = "";

  /// \brief True if this model is specified as static, false otherwise.
  public: bool isStatic = false;

  /// \brief True if this model should self-collide, false otherwise.
  public: bool selfCollide = false;

  /// \brief True if this model is allowed to conserve processing power by not
  /// updating when it's at rest.
  public: bool allowAutoDisable = true;

  /// \brief True if this model should be subject to wind, false otherwise.
  public: bool enableWind = false;

  /// \brief Pose of the model
  public: Pose3d pose = Pose3d::Zero;

  /// \brief Frame of the pose.
  public: std::string poseFrame = "";

  /// \brief The links specified in this model.
  public: std::vector<Link> links;

  /// \brief The joints specified in this model.
  public: std::vector<Joint> joints;

  public: std::shared_ptr<FrameGraph> frameGraph = nullptr;

  /// \brief The SDF element pointer used during load.
  public: sdf::ElementPtr sdf;
};

/////////////////////////////////////////////////
Model::Model()
  : dataPtr(new ModelPrivate)
{
}

/////////////////////////////////////////////////
Model::Model(Model &&_model)
{
  this->dataPtr = _model.dataPtr;
  _model.dataPtr = nullptr;
}

/////////////////////////////////////////////////
Model::~Model()
{
  delete this->dataPtr;
  this->dataPtr = nullptr;
}

/////////////////////////////////////////////////
Errors Model::Load(ElementPtr _sdf,
    std::shared_ptr<FrameGraph> _frameGraph)
{
  this->dataPtr->frameGraph = _frameGraph;
  return this->Load(_sdf);
}

/////////////////////////////////////////////////
Errors Model::Load(ElementPtr _sdf)
{
  if (!this->dataPtr->frameGraph)
    this->dataPtr->frameGraph.reset(new FrameGraph);
  Errors errors;

  this->dataPtr->sdf = _sdf;

  // Check that the provided SDF element is a <model>
  // This is an error that cannot be recovered, so return an error.
  if (_sdf->GetName() != "model")
  {
    errors.push_back({ErrorCode::ELEMENT_INCORRECT_TYPE,
        "Attempting to load a Model, but the provided SDF element is not a "
        "<model>."});
    return errors;
  }

  // Read the models's name
  if (!loadName(_sdf, this->dataPtr->name))
  {
    errors.push_back({ErrorCode::ATTRIBUTE_MISSING,
                     "A model name is required, but the name is not set."});
  }

  this->dataPtr->isStatic = _sdf->Get<bool>("static", false).first;

  this->dataPtr->selfCollide = _sdf->Get<bool>("self_collide", false).first;

  this->dataPtr->allowAutoDisable =
    _sdf->Get<bool>("allow_auto_disable", true).first;

  this->dataPtr->enableWind = _sdf->Get<bool>("enable_wind", false).first;

  // Load the pose.
  std::string frame;
  loadPose(_sdf, this->dataPtr->pose, frame);
  this->dataPtr->frameGraph->AddVertex(this->dataPtr->name,
      Matrix4d(this->dataPtr->pose));

  // Load any additional frames
  sdf::ElementPtr elem = _sdf->GetElement("frame");
  std::vector<std::pair<graph::Vertex<Matrix4d>, std::string>> edgesToAdd;
  while (elem)
  {
    std::string frameName, poseFrame;
    Pose3d poseValue;
    loadPose(elem, poseValue, poseFrame);

    frameName = elem->Get<std::string>("name", "" ).first;
    graph::Vertex<Matrix4d> &vert =
      this->dataPtr->frameGraph->AddVertex(frameName, Matrix4d(poseValue));

    edgesToAdd.push_back(std::make_pair(vert, poseFrame));
    elem = elem->GetNextElement("frame");
  }

  // Load all the links.
  Errors linkLoadErrors = loadUniqueRepeated<Link>(_sdf, "link",
    this->dataPtr->links, this->dataPtr->frameGraph);
  errors.insert(errors.end(), linkLoadErrors.begin(), linkLoadErrors.end());

  // Load all the joints.
  Errors jointLoadErrors = loadUniqueRepeated<Joint>(_sdf, "joint",
    this->dataPtr->joints, this->dataPtr->frameGraph);
  errors.insert(errors.end(), jointLoadErrors.begin(), jointLoadErrors.end());

  for (const std::pair<graph::Vertex<Matrix4d>, std::string> &edge : edgesToAdd)
  {
    const graph::VertexRef_M<Matrix4d> parentVertices =
      this->dataPtr->frameGraph->Vertices(edge.second);

    this->dataPtr->frameGraph->AddEdge(
        {parentVertices.begin()->first, edge.first.Id()}, -1);
    this->dataPtr->frameGraph->AddEdge(
        {edge.first.Id(), parentVertices.begin()->first}, 1);
  }

  return errors;
}

/////////////////////////////////////////////////
std::string Model::Name() const
{
  return this->dataPtr->name;
}

/////////////////////////////////////////////////
void Model::SetName(const std::string &_name)
{
  this->dataPtr->name = _name;
}

/////////////////////////////////////////////////
bool Model::Static() const
{
  return this->dataPtr->isStatic;
}

/////////////////////////////////////////////////
void Model::SetStatic(const bool _static)
{
  this->dataPtr->isStatic = _static;
}

/////////////////////////////////////////////////
bool Model::SelfCollide() const
{
  return this->dataPtr->selfCollide;
}

/////////////////////////////////////////////////
void Model::SetSelfCollide(const bool _selfCollide)
{
  this->dataPtr->selfCollide = _selfCollide;
}

/////////////////////////////////////////////////
bool Model::AllowAutoDisable() const
{
  return this->dataPtr->allowAutoDisable;
}

/////////////////////////////////////////////////
void Model::SetAllowAutoDisable(const bool _allowAutoDisable)
{
  this->dataPtr->allowAutoDisable = _allowAutoDisable;
}

/////////////////////////////////////////////////
bool Model::EnableWind() const
{
  return this->dataPtr->enableWind;
}

/////////////////////////////////////////////////
void Model::SetEnableWind(const bool _enableWind)
{
  this->dataPtr->enableWind =_enableWind;
}

/////////////////////////////////////////////////
uint64_t Model::LinkCount() const
{
  return this->dataPtr->links.size();
}

/////////////////////////////////////////////////
const Link *Model::LinkByIndex(const uint64_t _index) const
{
  if (_index < this->dataPtr->links.size())
    return &this->dataPtr->links[_index];
  return nullptr;
}

/////////////////////////////////////////////////
bool Model::LinkNameExists(const std::string &_name) const
{
  for (auto const &l : this->dataPtr->links)
  {
    if (l.Name() == _name)
    {
      return true;
    }
  }
  return false;
}

/////////////////////////////////////////////////
uint64_t Model::JointCount() const
{
  return this->dataPtr->joints.size();
}

/////////////////////////////////////////////////
const Joint *Model::JointByIndex(const uint64_t _index) const
{
  if (_index < this->dataPtr->joints.size())
    return &this->dataPtr->joints[_index];
  return nullptr;
}

/////////////////////////////////////////////////
bool Model::JointNameExists(const std::string &_name) const
{
  for (auto const &j : this->dataPtr->joints)
  {
    if (j.Name() == _name)
    {
      return true;
    }
  }
  return false;
}

/////////////////////////////////////////////////
const Joint *Model::JointByName(const std::string &_name) const
{
  for (auto const &j : this->dataPtr->joints)
  {
    if (j.Name() == _name)
    {
      return &j;
    }
  }
  return nullptr;
}

/////////////////////////////////////////////////
Pose3d Model::Pose(const std::string &_frame) const
{
  return poseInFrame(
      this->dataPtr->name,
      _frame.empty() ? this->PoseFrame() : _frame,
      *this->dataPtr->frameGraph);
}

/////////////////////////////////////////////////
const std::string &Model::PoseFrame() const
{
  return this->dataPtr->poseFrame;
}

/////////////////////////////////////////////////
void Model::SetPose(const Pose3d &_pose)
{
  this->dataPtr->pose = _pose;
}

/////////////////////////////////////////////////
void Model::SetPoseFrame(const std::string &_frame)
{
  this->dataPtr->poseFrame = _frame;
}

/////////////////////////////////////////////////
const Link *Model::LinkByName(const std::string &_name) const
{
  for (auto const &l : this->dataPtr->links)
  {
    if (l.Name() == _name)
    {
      return &l;
    }
  }
  return nullptr;
}

/////////////////////////////////////////////////
sdf::ElementPtr Model::Element() const
{
  return this->dataPtr->sdf;
}
