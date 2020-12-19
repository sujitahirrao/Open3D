// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2020 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#pragma once

#include "open3d/visualization/gui/Window.h"
#include "open3d/visualization/rendering/Material.h"
#include "open3d/visualization/visualizer/O3DVisualizerSelections.h"

namespace open3d {

namespace geometry {
class Geometry3D;
class Image;
}  // namespace geometry

namespace t {
namespace geometry {
class Geometry;
}  // namespace geometry
}  // namespace t

namespace visualization {

namespace rendering {
class Open3DScene;
}  // namespace rendering

namespace visualizer {

class O3DVisualizer : public gui::Window {
    using Super = gui::Window;

public:
    enum class Shader { STANDARD, NORMALS, DEPTH };

    struct DrawObject {
        std::string name;
        std::shared_ptr<geometry::Geometry3D> geometry;
        std::shared_ptr<t::geometry::Geometry> tgeometry;
        rendering::Material material;
        std::string group;
        double time = 0.0;
        bool is_visible = true;

        // internal
        bool is_color_default = true;
    };

    struct UIState {
        Shader scene_shader = Shader::STANDARD;
        bool show_settings = false;
        bool show_skybox = false;
        bool show_axes = false;
        bool is_animating = false;
        std::set<std::string> enabled_groups;

        Eigen::Vector4f bg_color = {1.0f, 1.0f, 1.0f, 1.0f};
        int point_size = 3;
        int line_width = 1;

        bool use_ibl = false;
        bool use_sun = true;
        std::string ibl_path = "";  // "" is default path
        int ibl_intensity = 0;
        int sun_intensity = 100000;
        Eigen::Vector3f sun_dir = {0.577f, -0.577f, -0.577f};
        Eigen::Vector3f sun_color = {1.0f, 1.0f, 1.0f};

        double current_time = 0.0;   // seconds
        double time_step = 1.0;      // seconds
        double frame_delay = 0.100;  // seconds
    };

    O3DVisualizer(const std::string& title, int width, int height);
    virtual ~O3DVisualizer();

    void AddAction(const std::string& name,
                   std::function<void(O3DVisualizer&)> callback);

    void SetBackground(const Eigen::Vector4f& bg_color,
                       std::shared_ptr<geometry::Image> bg_image = nullptr);

    void SetShader(Shader shader);

    void AddGeometry(const std::string& name,
                     std::shared_ptr<geometry::Geometry3D> geom,
                     rendering::Material* material = nullptr,
                     const std::string& group = "",
                     double time = 0.0,
                     bool is_visible = true);

    void AddGeometry(const std::string& name,
                     std::shared_ptr<t::geometry::Geometry> tgeom,
                     rendering::Material* material = nullptr,
                     const std::string& group = "",
                     double time = 0.0,
                     bool is_visible = true);

    void RemoveGeometry(const std::string& name);

    void ShowGeometry(const std::string& name, bool show);

    DrawObject GetGeometry(const std::string& name) const;

    void SetupCamera(float fov,
                     const Eigen::Vector3f& center,
                     const Eigen::Vector3f& eye,
                     const Eigen::Vector3f& up);
    void ResetCameraToDefault();

    void ShowSettings(bool show);
    void ShowSkybox(bool show);
    void ShowAxes(bool show);
    void SetPointSize(int point_size);
    void SetLineWidth(int line_width);
    void EnableGroup(const std::string& group, bool enable);

    std::vector<O3DVisualizerSelections::SelectionSet> GetSelectionSets() const;

    double GetAnimationFrameDelay() const;
    void SetAnimationFrameDelay(double secs);

    double GetAnimationTimeStep() const;
    void SetAnimationTimeStep(double time_step);

    double GetAnimationDuration() const;
    void SetAnimationDuration(double sec);

    double GetCurrentTime() const;
    void SetCurrentTime(double t);

    bool GetIsAnimating() const;
    void SetAnimating(bool is_animating);

    void SetOnAnimationFrame(std::function<void(O3DVisualizer&, double)> cb);

    enum class TickResult { NO_CHANGE, REDRAW };
    void SetOnAnimationTick(
            std::function<TickResult(O3DVisualizer&, double, double)> cb);

    void ExportCurrentImage(const std::string& path);

    UIState GetUIState() const;
    rendering::Open3DScene* GetScene() const;

    /// Starts the RPC interface. See io/rpc/ReceiverBase for the parameters.
    void StartRPCInterface(const std::string& address, int timeout);

    void StopRPCInterface();

protected:
    void Layout(const gui::Theme& theme);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace visualizer
}  // namespace visualization
}  // namespace open3d
