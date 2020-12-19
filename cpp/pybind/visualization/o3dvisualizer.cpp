// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018 www.open3d.org
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

#include <open3d/geometry/Geometry3D.h>
#include <open3d/t/geometry/Geometry.h>
#include <open3d/visualization/gui/Window.h>
#include <open3d/visualization/rendering/Open3DScene.h>
#include <open3d/visualization/visualizer/O3DVisualizer.h>
#include <pybind/visualization/visualization.h>

namespace open3d {
namespace visualization {

using namespace visualizer;

void pybind_o3dvisualizer(py::module& m) {
    py::class_<O3DVisualizerSelections::SelectedIndex> selected_index(
            m, "SelectedIndex",
            "Information about a point or vertex that was selected");
    selected_index
            .def("__repr__",
                 [](const O3DVisualizerSelections::SelectedIndex& idx) {
                     std::stringstream s;
                     s << "{ index: " << idx.index << ", order: " << idx.order
                       << ", point: (" << idx.point.x() << ", " << idx.point.y()
                       << ", " << idx.point.z() << ") }";
                     return s.str();
                 })
            .def_readonly("index",
                          &O3DVisualizerSelections::SelectedIndex::index,
                          "The index of this point in the point/vertex "
                          "array")
            .def_readonly("order",
                          &O3DVisualizerSelections::SelectedIndex::order,
                          "A monotonically increasing value that can be "
                          "used to determine in what order the points "
                          "were selected")
            .def_readonly("point",
                          &O3DVisualizerSelections::SelectedIndex::point,
                          "The (x, y, z) value of this point");

    py::class_<O3DVisualizer, UnownedPointer<O3DVisualizer>, gui::Window>
            o3dvis(m, "O3DVisualizer", "Visualization object used by draw()");

    py::enum_<O3DVisualizer::Shader> dv_shader(o3dvis, "Shader",
                                               "Scene-level rendering options");
    dv_shader
            .value("STANDARD", O3DVisualizer::Shader::STANDARD,
                   "Pixel colors from standard lighting model")
            .value("NORMALS", O3DVisualizer::Shader::NORMALS,
                   "Pixel colors correspond to surface normal")
            .value("DEPTH", O3DVisualizer::Shader::DEPTH,
                   "Pixel colors correspond to depth buffer value")
            .export_values();

    py::enum_<O3DVisualizer::TickResult> tick_result(
            o3dvis, "TickResult", "Return value from animation tick callback");
    tick_result
            .value("NO_CHANGE", O3DVisualizer::TickResult::NO_CHANGE,
                   "Signals that no change happened and no redraw is required")
            .value("REDRAW", O3DVisualizer::TickResult::REDRAW,
                   "Signals that a redraw is required");

    py::class_<O3DVisualizer::DrawObject> drawobj(
            o3dvis, "DrawObject",
            "Information about an object that is drawn. Do not modify this, it "
            "can lead to unexpected results.");
    drawobj.def_readonly("name", &O3DVisualizer::DrawObject::name,
                         "The name of the object")
            .def_property_readonly(
                    "geometry",
                    [](const O3DVisualizer::DrawObject& o) {
                        if (o.geometry) {
                            return py::cast(o.geometry);
                        } else {
                            return py::cast(o.tgeometry);
                        }
                    },
                    "The geometry. Modifying this will not "
                    "result in any visible change. Use "
                    "remove_geometry() and then add_geometry()"
                    "to change the geometry")
            .def_readonly("group", &O3DVisualizer::DrawObject::group,
                          "The group that the object belongs to")
            .def_readonly("time", &O3DVisualizer::DrawObject::time,
                          "The object's timestamp")
            .def_readonly("is_visible", &O3DVisualizer::DrawObject::is_visible,
                          "True if the object is checked in the list. "
                          "If the object's group is unchecked or an "
                          "animation is playing, the object's "
                          "visiblity may not correspond with this "
                          "value");

    o3dvis.def(py::init<const std::string, int, int>(), "title"_a = "Open3D",
               "width"_a = 1024, "height"_a = 768,
               "Creates a O3DVisualizer object")
            .def("add_action", &O3DVisualizer::AddAction,
                 "Adds a button to the custom actions section of the UI "
                 "and a corresponding menu item in the \"Actions\" menu. "
                 "add_action(name, callback). The callback will be given "
                 "one parameter, the O3DVisualizer instance, and does not "
                 "return any value.")
            .def("add_geometry",
                 py::overload_cast<const std::string&,
                                   std::shared_ptr<geometry::Geometry3D>,
                                   rendering::Material*, const std::string&,
                                   double, bool>(&O3DVisualizer::AddGeometry),
                 "name"_a, "geometry"_a, "material"_a = nullptr, "group"_a = "",
                 "time"_a = 0.0, "is_visible"_a = true,
                 "Adds a geometry: geometry(name, geometry, material=None, "
                 "group='', time=0.0, is_visible=True). 'name' must be unique.")
            .def(
                    "add_geometry",
                    [](py::object dv, const py::dict& d) {
                        rendering::Material* material = nullptr;
                        std::string group = "";
                        double time = 0;
                        bool is_visible = true;

                        std::string name = py::cast<std::string>(d["name"]);
                        if (d.contains("material")) {
                            material = py::cast<rendering::Material*>(
                                    d["material"]);
                        }
                        if (d.contains("group")) {
                            group = py::cast<std::string>(d["group"]);
                        }
                        if (d.contains("time")) {
                            time = py::cast<double>(d["time"]);
                        }
                        if (d.contains("is_visible")) {
                            is_visible = py::cast<bool>(d["is_visible"]);
                        }
                        py::object g = d["geometry"];
                        // Instead of trying to figure out how to cast 'g' as
                        // the appropriate shared_ptr, if we can call the
                        // function using pybind and let it figure out how to do
                        // everything.
                        dv.attr("add_geometry")(name, g, material, group, time,
                                                is_visible);
                    },
                    "Adds a geometry from a dictionary. The dictionary has the "
                    "following elements:\n"
                    "name: unique name of the object (required)\n"
                    "geometry: the geometry or t.geometry object (required)\n"
                    "material: a visualization.rendering.Material object "
                    "(optional)\n"
                    "group: a string declaring the group it is a member of "
                    "(optional)\n"
                    "time: a time value\n")
            .def("remove_geometry", &O3DVisualizer::RemoveGeometry,
                 "remove_geometry(name): removes the geometry with the "
                 "name.")
            .def("show_geometry", &O3DVisualizer::ShowGeometry,
                 "Checks or unchecks the named geometry in the list. Note that "
                 "even if show_geometry(name, True) is called, the object may "
                 "not actually be visible if its group is unchecked, or if an "
                 "animation is in progress.")
            .def("get_geometry", &O3DVisualizer::GetGeometry,
                 "get_geometry(name): Returns the DrawObject corresponding to "
                 "the name. This should be treated as read-only. Modify "
                 "visibility with show_geometry(), and other values by "
                 "removing the object and re-adding it with the new values")
            .def("reset_camera_to_default",
                 &O3DVisualizer::ResetCameraToDefault,
                 "Sets camera to default position")
            .def("get_selection_sets", &O3DVisualizer::GetSelectionSets,
                 "Returns the selection sets, as [{'obj_name', "
                 "[SelectedIndex]}]")
            .def("set_on_animation_frame", &O3DVisualizer::SetOnAnimationFrame,
                 "set_on_animation(callback): Sets a callback that will be "
                 "called every frame of the animation. The callback will be "
                 "called as callback(o3dvis, current_time).")
            .def("set_on_animation_tick", &O3DVisualizer::SetOnAnimationTick,
                 "set_on_animation(callback): Sets a callback that will be "
                 "called every frame of the animation. The callback will be "
                 "called as callback(o3dvis, time_since_last_tick, "
                 "total_elapsed_since_animation_started). Note that this "
                 "is a low-level callback. If you need to change the current "
                 "timestamp being shown you will need to update the "
                 "o3dvis.current_time property in the callback. The callback "
                 "must return either O3DVisualizer.TickResult.IGNORE if no "
                 "redraw is required or O3DVisualizer.TickResult.REDRAW "
                 "if a redraw is required.")
            .def("export_current_image", &O3DVisualizer::ExportCurrentImage,
                 "export_image(path). Exports a PNG image of what is "
                 "currently displayed to the given path.")
            .def("start_rpc_interface", &O3DVisualizer::StartRPCInterface,
                 "address"_a, "timeout"_a,
                 "Starts the RPC interface.\n"
                 "address: str with the address to listen on.\n"
                 "timeout: int timeout in milliseconds for sending the reply.")
            .def("stop_rpc_interface", &O3DVisualizer::StopRPCInterface,
                 "Stops the RPC interface.")
            .def("set_background", &O3DVisualizer::SetBackground,
                 "set_background(color, image=None): Sets the background color "
                 "and, optionally, the background image. Passing None for the "
                 "background image will clear any image already there.")
            .def_property(
                    "show_settings",
                    [](const O3DVisualizer& dv) {
                        return dv.GetUIState().show_settings;
                    },
                    &O3DVisualizer::ShowSettings,
                    "Gets/sets if settings panel is visible")
            .def_property(
                    "scene_shader",
                    [](const O3DVisualizer& dv) {
                        return dv.GetUIState().scene_shader;
                    },
                    &O3DVisualizer::SetShader,
                    "Gets/sets the shading model for the scene")
            .def_property(
                    "show_axes",
                    [](const O3DVisualizer& dv) {
                        return dv.GetUIState().show_axes;
                    },
                    &O3DVisualizer::ShowAxes, "Gets/sets if axes are visible")
            .def_property(
                    "point_size",
                    [](const O3DVisualizer& dv) {
                        return dv.GetUIState().point_size;
                    },
                    &O3DVisualizer::SetPointSize,
                    "Gets/sets size of points (in units of pixels)")
            .def_property(
                    "line_width",
                    [](const O3DVisualizer& dv) {
                        return dv.GetUIState().line_width;
                    },
                    &O3DVisualizer::SetLineWidth,
                    "Gets/sets width of lines (in units of pixels)")
            .def_property_readonly("scene", &O3DVisualizer::GetScene,
                                   "Returns the rendering.Open3DScene object "
                                   "for low-level manipulation")
            .def_property(
                    "current_time",
                    // MSVC doesn't like this for some reason
                    //&O3DVisualizer::GetCurrentTime,
                    [](const O3DVisualizer& dv) -> double {
                        return dv.GetCurrentTime();
                    },
                    &O3DVisualizer::SetCurrentTime,
                    "Gets/sets the current time. If setting, only the "
                    "objects belonging to the current time-step will "
                    "be displayed")
            .def_property("animation_time_step",
                          &O3DVisualizer::GetAnimationTimeStep,
                          &O3DVisualizer::SetAnimationTimeStep,
                          "Gets/sets the time step for animations. Default is "
                          "1.0 sec")
            .def_property("animation_frame_delay",
                          &O3DVisualizer::GetAnimationFrameDelay,
                          &O3DVisualizer::SetAnimationFrameDelay,
                          "Gets/sets the length of time a frame is visible.")
            .def_property("animation_duration",
                          &O3DVisualizer::GetAnimationDuration,
                          &O3DVisualizer::SetAnimationDuration,
                          "Gets/sets the duration (in seconds) of the "
                          "animation. This is automatically computed to be the "
                          "difference between the minimum and maximum time "
                          "values, but this is useful if no time values have "
                          "been specified (that is, all objects are at the "
                          "default t=0)")
            .def_property("is_animating", &O3DVisualizer::GetIsAnimating,
                          &O3DVisualizer::SetAnimating,
                          "Gets/sets the status of the animation. Changing "
                          "value will start or stop the animating.");
}

}  // namespace visualization
}  // namespace open3d
