// Porte de src/viewport/gl_widget.py: viewport OpenGL 3D.
#include "viewport/gl_widget.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include "viewport/grid.hpp"

#if defined(CNC_HAVE_VTK)
#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCylinderSource.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkLine.h>
#include <vtkNew.h>
#include <vtkOpenGLRenderWindow.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkTriangle.h>
#include <vtkUnsignedCharArray.h>
#endif

namespace cnc::vp {
namespace {

constexpr const char* kVertexShader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
uniform mat4 uMVP;
out vec3 vColor;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
)glsl";

constexpr const char* kFragmentShader = R"glsl(
#version 330 core
in vec3 vColor;
uniform float uAlpha;
out vec4 FragColor;
void main() { FragColor = vec4(vColor, uAlpha); }
)glsl";

constexpr unsigned int kStaticDraw = 0x88E4;
constexpr unsigned int kDynamicDraw = 0x88E8;

#if defined(CNC_HAVE_VTK)

vtkSmartPointer<vtkActor> vtk_line_actor(const std::vector<float>& vertices, float line_width) {
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto lines = vtkSmartPointer<vtkCellArray>::New();
    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("Colors");
    colors->SetNumberOfComponents(3);

    const vtkIdType point_count = static_cast<vtkIdType>(vertices.size() / 6);
    for (vtkIdType index = 0; index < point_count; ++index) {
        const size_t base = static_cast<size_t>(index) * 6;
        points->InsertNextPoint(vertices[base], vertices[base + 1], vertices[base + 2]);
        unsigned char color[3] = {
            static_cast<unsigned char>(std::clamp(vertices[base + 3] * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(std::clamp(vertices[base + 4] * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(std::clamp(vertices[base + 5] * 255.0f, 0.0f, 255.0f))};
        colors->InsertNextTypedTuple(color);
    }
    for (vtkIdType index = 0; index + 1 < point_count; index += 2) {
        vtkIdType ids[2] = {index, index + 1};
        lines->InsertNextCell(2, ids);
    }

    auto data = vtkSmartPointer<vtkPolyData>::New();
    data->SetPoints(points);
    data->SetLines(lines);
    data->GetPointData()->SetScalars(colors);

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(data);
    mapper->SetColorModeToDirectScalars();
    mapper->ScalarVisibilityOn();

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetLineWidth(line_width);
    return actor;
}

void vtk_window_event(vtkObject*, unsigned long event_id, void* client_data, void* call_data) {
    auto* viewport = static_cast<Viewport3D*>(client_data);
    if (!viewport || !viewport->context()) return;
    QOpenGLContext* context = viewport->context();
    switch (event_id) {
    case vtkCommand::WindowMakeCurrentEvent:
        if (context->surface()) context->makeCurrent(context->surface());
        break;
    case vtkCommand::WindowIsCurrentEvent:
        if (call_data) {
            *static_cast<bool*>(call_data) = QOpenGLContext::currentContext() == context;
        }
        break;
    case vtkCommand::WindowFrameEvent:
        viewport->update();
        break;
    default:
        break;
    }
}

#endif

}  // namespace

Viewport3D::Viewport3D(QWidget* parent) : QOpenGLWidget(parent) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
#if defined(CNC_HAVE_VTK)
    format.setSamples(0);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
#else
    format.setSamples(4);
#endif
    QSurfaceFormat::setDefaultFormat(format);
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(true);
}

void Viewport3D::initializeGL() {
#if defined(CNC_HAVE_VTK)
    vtk_render_window_ = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    vtk_renderer_ = vtkSmartPointer<vtkRenderer>::New();
    vtk_render_window_->AddRenderer(vtk_renderer_);
    vtk_render_window_->SetMultiSamples(0);
    vtk_render_window_->SetFrameBlitModeToNoBlit();
    vtk_render_window_->SetUseOffScreenBuffers(false);
    vtk_render_window_->SetOwnContext(0);
    vtk_render_window_->SetForceMaximumHardwareLineWidth(1.0f);
    vtk_render_window_->SetReadyForRendering(true);
    vtk_render_callback_ = vtkSmartPointer<vtkCallbackCommand>::New();
    vtk_render_callback_->SetClientData(this);
    vtk_render_callback_->SetCallback(vtk_window_event);
    vtk_render_window_->AddObserver(vtkCommand::WindowMakeCurrentEvent, vtk_render_callback_);
    vtk_render_window_->AddObserver(vtkCommand::WindowIsCurrentEvent, vtk_render_callback_);
    vtk_render_window_->AddObserver(vtkCommand::WindowFrameEvent, vtk_render_callback_);
    vtk_render_window_->vtkOpenGLRenderWindow::OpenGLInit();
    vtk_render_window_->OpenGLInitContext();
    vtk_renderer_->SetBackground(0.078, 0.106, 0.145);
    vtk_renderer_->SetViewport(0.0, 0.0, 1.0, 1.0);
    update_vtk_axes();
    return;
#else
    gl_ = context()->extraFunctions();
    gl_->initializeOpenGLFunctions();
    gl_->glClearColor(20.0f / 255.0f, 27.0f / 255.0f, 37.0f / 255.0f, 1.0f);
    gl_->glEnable(GL_DEPTH_TEST);
    gl_->glEnable(GL_LINE_SMOOTH);
    gl_->glEnable(GL_BLEND);
    gl_->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader_program_ = compile_shaders();
    mvp_location_ = gl_->glGetUniformLocation(shader_program_, "uMVP");
    alpha_location_ = gl_->glGetUniformLocation(shader_program_, "uAlpha");

    const auto axes = build_axes_vertices();
    axes_ = create_vao(axes);
    const auto origin = build_origin_marker();
    origin_ = create_vao(origin);
#endif
}

void Viewport3D::resizeGL(int width, int height) {
#if defined(CNC_HAVE_VTK)
    if (vtk_render_window_) {
        const QSize device_size = QSize(width, height) * devicePixelRatioF();
        vtk_render_window_->SetSize(device_size.width(), device_size.height());
    }
    camera.aspect = static_cast<double>(width) / std::max(height, 1);
    camera.viewport_height = std::max(this->height(), 1);
#else
    const QSize device_size = QSize(width, height) * devicePixelRatioF();
    gl_->glViewport(0, 0, device_size.width(), device_size.height());
    camera.aspect = static_cast<double>(width) / std::max(height, 1);
    camera.viewport_height = std::max(this->height(), 1);
#endif
}

void Viewport3D::paintGL() {
#if defined(CNC_HAVE_VTK)
    if (!vtk_render_window_ || !vtk_renderer_) return;
    auto* context = QOpenGLContext::currentContext();
    if (!context) return;
    auto* functions = context->extraFunctions();
    if (!functions) return;
    const QSize device_size = size() * devicePixelRatioF();
    functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
    // VTK can leave scissor/viewport state restricted to its render region.
    // Reset it before clearing Qt's FBO; otherwise the unused part of the
    // QOpenGLWidget keeps the default black color.
    functions->glDisable(GL_SCISSOR_TEST);
    functions->glViewport(0, 0, device_size.width(), device_size.height());
    functions->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    functions->glDepthMask(GL_TRUE);
    functions->glClearColor(20.0f / 255.0f, 27.0f / 255.0f, 37.0f / 255.0f, 1.0f);
    functions->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    sync_vtk_camera();
    vtk_render_window_->Render();
    functions->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
    functions->glDisable(GL_SCISSOR_TEST);
    functions->glViewport(0, 0, device_size.width(), device_size.height());
    const GLenum draw_buffers[] = {GL_COLOR_ATTACHMENT0};
    functions->glDrawBuffers(1, draw_buffers);
    vtk_render_window_->BlitDisplayFramebuffer(0, 0, 0, device_size.width(), device_size.height(),
                                               0, 0, device_size.width(), device_size.height(),
                                               GL_COLOR_BUFFER_BIT, GL_LINEAR);
#else
    const QSize device_size = size() * devicePixelRatioF();
    gl_->glDisable(GL_SCISSOR_TEST);
    gl_->glViewport(0, 0, device_size.width(), device_size.height());
    gl_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl_->glUseProgram(shader_program_);
    const QMatrix4x4 mvp = camera.mvp();
    gl_->glUniformMatrix4fv(mvp_location_, 1, GL_FALSE, mvp.constData());
    gl_->glUniform1f(alpha_location_, 1.0f);

    if (simulation_mesh_indices_) {
        gl_->glDepthMask(GL_TRUE);
        gl_->glBindVertexArray(simulation_mesh_vao_);
        gl_->glDrawElements(GL_TRIANGLES, simulation_mesh_indices_, GL_UNSIGNED_INT, nullptr);
    }

    const auto draw_lines = [this](const LineBuffer& buffer) {
        if (!buffer.vao || !buffer.count) return;
        gl_->glBindVertexArray(buffer.vao);
        gl_->glDrawArrays(GL_LINES, 0, buffer.count);
    };
    draw_lines(axes_);
    draw_lines(origin_);
    for (const LineBuffer& buffer : entity_buffers_) draw_lines(buffer);
    draw_lines(simulation_path_);
    draw_lines(simulation_tool_);
    gl_->glBindVertexArray(0);
    gl_->glUseProgram(0);
#endif
}

void Viewport3D::mousePressEvent(QMouseEvent* event) {
    last_mouse_pos_ = event->position().toPoint();
    mouse_button_ = event->button();
}

void Viewport3D::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    const int dx = pos.x() - last_mouse_pos_.x();
    const int dy = pos.y() - last_mouse_pos_.y();
    last_mouse_pos_ = pos;
    if (mouse_button_ == Qt::LeftButton) camera.orbit(dx, dy);
    else if (mouse_button_ == Qt::MiddleButton || mouse_button_ == Qt::RightButton) camera.pan(dx, dy);
    update();
}

void Viewport3D::mouseReleaseEvent(QMouseEvent*) { mouse_button_ = Qt::NoButton; }

void Viewport3D::wheelEvent(QWheelEvent* event) {
    camera.zoom(event->angleDelta().y());
    update();
}

int Viewport3D::add_entity_lines(const std::vector<float>& vertices, float line_width) {
#if defined(CNC_HAVE_VTK)
    if (!vtk_renderer_ || vertices.empty()) return -1;
    auto actor = vtk_line_actor(vertices, line_width);
    vtk_renderer_->AddActor(actor);
    vtk_entity_actors_.push_back(actor);
    update();
    return static_cast<int>(vtk_entity_actors_.size()) - 1;
#else
    makeCurrent();
    entity_buffers_.push_back(create_vao(vertices));
    doneCurrent();
    update();
    return static_cast<int>(entity_buffers_.size()) - 1;
#endif
}

void Viewport3D::clear_entities() {
#if defined(CNC_HAVE_VTK)
    if (vtk_renderer_) {
        for (const auto& actor : vtk_entity_actors_) vtk_renderer_->RemoveActor(actor);
        vtk_entity_actors_.clear();
    }
    update();
#else
    makeCurrent();
    for (const LineBuffer& buffer : entity_buffers_) {
        gl_->glDeleteVertexArrays(1, &buffer.vao);
        gl_->glDeleteBuffers(1, &buffer.vbo);
    }
    entity_buffers_.clear();
    doneCurrent();
    update();
#endif
}

void Viewport3D::set_origin(double x, double y, double z) {
#if defined(CNC_HAVE_VTK)
    vtk_origin_ = QVector3D(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    update_vtk_axes();
    update();
#else
    makeCurrent();
    const auto shifted = [x, y, z](std::vector<float> vertices) {
        for (size_t i = 0; i + 5 < vertices.size(); i += 6) {
            vertices[i] += static_cast<float>(x);
            vertices[i + 1] += static_cast<float>(y);
            vertices[i + 2] += static_cast<float>(z);
        }
        return vertices;
    };
    upload_buffer(origin_.vbo, shifted(build_origin_marker(std::max(1.0, axis_length_ * 0.08))), kStaticDraw);
    upload_buffer(axes_.vbo, shifted(build_axes_vertices(axis_length_)), kStaticDraw);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, 0);
    doneCurrent();
    update();
#endif
}

void Viewport3D::set_workpiece(double width, double height, double z_bottom, double z_top) {
    axis_length_ = std::max(10.0, std::min(width, height) * 0.18);
    scene_bounds_ = std::make_pair(QVector3D(0, 0, static_cast<float>(z_bottom)),
                                   QVector3D(static_cast<float>(width), static_cast<float>(height),
                                             static_cast<float>(z_top)));
    update();
}

void Viewport3D::set_simulation_mesh(const std::vector<float>& vertices,
                                     const std::vector<unsigned int>& indices,
                                     std::optional<std::pair<int, int>> mesh_shape) {
#if defined(CNC_HAVE_VTK)
    if (!vtk_renderer_) return;
    if (!vtk_simulation_mesh_data_) {
        vtk_simulation_mesh_data_ = vtkSmartPointer<vtkPolyData>::New();
        vtk_simulation_normals_ = vtkSmartPointer<vtkPolyDataNormals>::New();
        vtk_simulation_normals_->SetInputData(vtk_simulation_mesh_data_);
        vtk_simulation_normals_->ComputePointNormalsOn();
        vtk_simulation_normals_->SplittingOff();
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(vtk_simulation_normals_->GetOutputPort());
        mapper->SetColorModeToDirectScalars();
        mapper->ScalarVisibilityOn();
        vtk_simulation_mesh_actor_ = vtkSmartPointer<vtkActor>::New();
        vtk_simulation_mesh_actor_->SetMapper(mapper);
        vtk_simulation_mesh_actor_->GetProperty()->SetOpacity(1.0);
        vtk_simulation_mesh_actor_->GetProperty()->SetColor(1.0, 1.0, 1.0);
        vtk_simulation_mesh_actor_->GetProperty()->SetAmbient(0.28);
        vtk_simulation_mesh_actor_->GetProperty()->SetDiffuse(0.72);
        vtk_simulation_mesh_actor_->GetProperty()->SetSpecular(0.18);
        vtk_simulation_mesh_actor_->GetProperty()->SetSpecularPower(18.0);
        vtk_simulation_mesh_actor_->GetProperty()->SetEdgeVisibility(false);
        vtk_renderer_->AddActor(vtk_simulation_mesh_actor_);
    }

    auto points = vtkSmartPointer<vtkPoints>::New();
    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("StockColor");
    colors->SetNumberOfComponents(3);
    const vtkIdType point_count = static_cast<vtkIdType>(vertices.size() / 6);
    points->SetNumberOfPoints(point_count);
    for (vtkIdType index = 0; index < point_count; ++index) {
        const size_t base = static_cast<size_t>(index) * 6;
        points->SetPoint(index, vertices[base], vertices[base + 1], vertices[base + 2]);
        unsigned char color[3] = {
            static_cast<unsigned char>(std::clamp(vertices[base + 3] * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(std::clamp(vertices[base + 4] * 255.0f, 0.0f, 255.0f)),
            static_cast<unsigned char>(std::clamp(vertices[base + 5] * 255.0f, 0.0f, 255.0f))};
        colors->InsertNextTypedTuple(color);
    }
    vtk_simulation_mesh_data_->SetPoints(points);
    vtk_simulation_mesh_data_->GetPointData()->SetScalars(colors);

    if (mesh_shape != simulation_mesh_shape_ || vtk_simulation_mesh_data_->GetNumberOfPolys() == 0) {
        auto polys = vtkSmartPointer<vtkCellArray>::New();
        for (size_t index = 0; index + 2 < indices.size(); index += 3) {
            vtkIdType ids[3] = {static_cast<vtkIdType>(indices[index]),
                                static_cast<vtkIdType>(indices[index + 1]),
                                static_cast<vtkIdType>(indices[index + 2])};
            polys->InsertNextCell(3, ids);
        }
        vtk_simulation_mesh_data_->SetPolys(polys);
    }
    simulation_mesh_indices_ = static_cast<int>(indices.size());
    simulation_mesh_shape_ = mesh_shape;
    vtk_simulation_mesh_actor_->SetVisibility(true);
    vtk_simulation_mesh_data_->Modified();
    update();
#else
    makeCurrent();
    if (!simulation_mesh_vao_) {
        const LineBuffer mesh = create_vao(vertices);
        simulation_mesh_vao_ = mesh.vao;
        simulation_mesh_vbo_ = mesh.vbo;
        gl_->glGenBuffers(1, &simulation_mesh_ebo_);
        gl_->glBindVertexArray(simulation_mesh_vao_);
        gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, simulation_mesh_ebo_);
        gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<ptrdiff_t>(indices.size() * sizeof(unsigned int)),
                          indices.data(), kStaticDraw);
    } else {
        upload_buffer(simulation_mesh_vbo_, vertices, kDynamicDraw);
        if (static_cast<int>(indices.size()) != simulation_mesh_indices_ || mesh_shape != simulation_mesh_shape_) {
            gl_->glBindVertexArray(simulation_mesh_vao_);
            gl_->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, simulation_mesh_ebo_);
            gl_->glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<ptrdiff_t>(indices.size() * sizeof(unsigned int)),
                              indices.data(), kDynamicDraw);
        }
    }
    gl_->glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl_->glBindVertexArray(0);
    simulation_mesh_indices_ = static_cast<int>(indices.size());
    simulation_mesh_shape_ = mesh_shape;
    doneCurrent();
    update();
#endif
}

void Viewport3D::set_simulation_path(const std::vector<float>& vertices) {
#if defined(CNC_HAVE_VTK)
    if (!vtk_renderer_) return;
    if (vtk_simulation_path_actor_) vtk_renderer_->RemoveActor(vtk_simulation_path_actor_);
    vtk_simulation_path_actor_ = vertices.empty() ? nullptr : vtk_line_actor(vertices, 2.0f);
    if (vtk_simulation_path_actor_) vtk_renderer_->AddActor(vtk_simulation_path_actor_);
    update();
#else
    makeCurrent();
    set_dynamic_lines(vertices, simulation_path_);
    doneCurrent();
    update();
#endif
}

void Viewport3D::set_simulation_tool(std::optional<QVector3D> position, double diameter) {
#if defined(CNC_HAVE_VTK)
    if (!vtk_renderer_) return;
    if (!position || diameter <= 0.0) {
        for (const auto& actor : vtk_simulation_tool_actors_) actor->SetVisibility(false);
        update();
        return;
    }

    const bool geometry_changed = vtk_simulation_tool_actors_.empty() ||
                                  std::fabs(vtk_simulation_tool_diameter_ - diameter) > 1e-6;
    if (geometry_changed) {
        for (const auto& actor : vtk_simulation_tool_actors_) vtk_renderer_->RemoveActor(actor);
        vtk_simulation_tool_actors_.clear();
        vtk_simulation_tool_diameter_ = diameter;

        const auto add_cylinder = [this](double radius, double height,
                                         const std::array<double, 3>& color) {
            auto source = vtkSmartPointer<vtkCylinderSource>::New();
            source->SetRadius(radius);
            source->SetHeight(height);
            source->SetCenter(0.0, 0.0, 0.0);
            source->SetResolution(40);
            source->CappingOn();
            auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
            mapper->SetInputConnection(source->GetOutputPort());
            auto actor = vtkSmartPointer<vtkActor>::New();
            actor->SetMapper(mapper);
            // vtkCylinderSource nasce no eixo Y; a fresa da CNC trabalha no eixo Z.
            actor->RotateX(90.0);
            actor->GetProperty()->SetColor(color[0], color[1], color[2]);
            actor->GetProperty()->SetAmbient(0.20);
            actor->GetProperty()->SetDiffuse(0.72);
            actor->GetProperty()->SetSpecular(0.62);
            actor->GetProperty()->SetSpecularPower(30.0);
            vtk_renderer_->AddActor(actor);
            vtk_simulation_tool_actors_.push_back(actor);
        };

        const double cutter_height = std::max(diameter * 2.5, 8.0);
        const double shank_height = std::max(diameter * 4.0, 20.0);
        const double holder_height = std::max(diameter * 3.0, 14.0);
        add_cylinder(diameter / 2.0, cutter_height, {0.68, 0.74, 0.82});
        add_cylinder(std::max(diameter / 2.0, 2.5), shank_height, {0.82, 0.86, 0.91});
        add_cylinder(std::max(diameter * 1.2, 6.0), holder_height, {0.24, 0.34, 0.48});
    }

    const double cutter_height = std::max(diameter * 2.5, 8.0);
    const double shank_height = std::max(diameter * 4.0, 20.0);
    const double holder_height = std::max(diameter * 3.0, 14.0);
    const std::array<double, 3> centers = {
        cutter_height / 2.0,
        cutter_height + shank_height / 2.0,
        cutter_height + shank_height + holder_height / 2.0};
    for (size_t index = 0; index < vtk_simulation_tool_actors_.size(); ++index) {
        auto& actor = vtk_simulation_tool_actors_[index];
        actor->SetPosition(position->x(), position->y(), position->z() + centers[index]);
        actor->SetVisibility(true);
    }
    update();
#else
    std::vector<float> vertices;
    if (position && diameter > 0.0) {
        const double x = position->x(), y = position->y(), z = position->z();
        const double radius = diameter / 2.0;
        const double flute_height = std::max(diameter * 1.5, 2.5);
        const double stem_height = std::max(diameter * 3.0, 15.0);
        constexpr float flute_color[] = {0.68f, 0.74f, 0.82f};
        constexpr float stem_color[] = {0.76f, 0.82f, 0.90f};
        const auto add = [&vertices](double px, double py, double pz, const float color[3]) {
            vertices.insert(vertices.end(), {static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz),
                                             color[0], color[1], color[2]});
        };
        const auto ring = [&add, x, y, z](double level, double ring_radius, const float color[3]) {
            constexpr int count = 16;
            for (int index = 0; index < count; ++index) {
                const double a0 = 2.0 * 3.14159265358979323846 * index / count;
                const double a1 = 2.0 * 3.14159265358979323846 * (index + 1) / count;
                add(x + ring_radius * std::cos(a0), y + ring_radius * std::sin(a0), z + level, color);
                add(x + ring_radius * std::cos(a1), y + ring_radius * std::sin(a1), z + level, color);
            }
        };
        ring(0.0, radius, flute_color);
        ring(flute_height, radius, flute_color);
        ring(stem_height, radius * 0.72, stem_color);
        for (int flute = 0; flute < 2; ++flute) {
            std::optional<QVector3D> previous;
            for (int step = 0; step < 9; ++step) {
                const double fraction = static_cast<double>(step) / 8.0;
                const double angle = flute * 3.14159265358979323846 + fraction * 2.2 * 3.14159265358979323846;
                const QVector3D point(static_cast<float>(x + radius * 0.82 * std::cos(angle)),
                                      static_cast<float>(y + radius * 0.82 * std::sin(angle)),
                                      static_cast<float>(z + flute_height * fraction));
                if (previous) {
                    add(previous->x(), previous->y(), previous->z(), flute_color);
                    add(point.x(), point.y(), point.z(), flute_color);
                }
                previous = point;
            }
        }
        for (int index = 0; index < 16; index += 4) {
            const double angle = 2.0 * 3.14159265358979323846 * index / 16.0;
            const double px = x + radius * 0.72 * std::cos(angle);
            const double py = y + radius * 0.72 * std::sin(angle);
            add(px, py, z + flute_height, stem_color);
            add(px, py, z + stem_height, stem_color);
        }
    }
    makeCurrent();
    set_dynamic_lines(vertices, simulation_tool_);
    doneCurrent();
    update();
#endif
}

void Viewport3D::clear_simulation() {
#if defined(CNC_HAVE_VTK)
    if (vtk_simulation_mesh_actor_) vtk_simulation_mesh_actor_->SetVisibility(false);
    if (vtk_simulation_path_actor_) vtk_simulation_path_actor_->SetVisibility(false);
    for (const auto& actor : vtk_simulation_tool_actors_) actor->SetVisibility(false);
    simulation_mesh_indices_ = 0;
    update();
#else
    simulation_mesh_indices_ = 0;
    simulation_path_.count = 0;
    simulation_tool_.count = 0;
    update();
#endif
}

void Viewport3D::fit_scene() {
    if (scene_bounds_) camera.fit_bounds(scene_bounds_->first, scene_bounds_->second);
    update();
}

void Viewport3D::set_standard_view(const QString& name) {
    if (name == "iso") camera.isometric_view();
    else if (name == "top") camera.top_view();
    else if (name == "bottom") camera.bottom_view();
    else if (name == "front") camera.front_view();
    else if (name == "back") camera.back_view();
    else if (name == "right") camera.right_view();
    else if (name == "left") camera.left_view();
    else throw std::invalid_argument("Vista 3D desconhecida: " + name.toStdString());
    fit_scene();
}

bool Viewport3D::toggle_projection() {
    camera.use_ortho = !camera.use_ortho;
    fit_scene();
    return camera.use_ortho;
}

#if defined(CNC_HAVE_VTK)

void Viewport3D::sync_vtk_camera() {
    if (!vtk_renderer_) return;
    vtkCamera* vtk_camera = vtk_renderer_->GetActiveCamera();
    const QVector3D position = camera.position();
    QVector3D forward = camera.target - position;
    if (forward.lengthSquared() < 1e-8f) return;
    forward.normalize();
    QVector3D world_up(0.0f, 0.0f, 1.0f);
    QVector3D right = QVector3D::crossProduct(forward, world_up);
    if (right.lengthSquared() < 1e-8f) {
        world_up = QVector3D(0.0f, 1.0f, 0.0f);
        right = QVector3D::crossProduct(forward, world_up);
    }
    right.normalize();
    const QVector3D up = QVector3D::crossProduct(right, forward).normalized();

    vtk_camera->SetPosition(position.x(), position.y(), position.z());
    vtk_camera->SetFocalPoint(camera.target.x(), camera.target.y(), camera.target.z());
    vtk_camera->SetViewUp(up.x(), up.y(), up.z());
    vtk_camera->SetClippingRange(camera.near_plane, camera.far_plane);
    if (camera.use_ortho) {
        vtk_camera->ParallelProjectionOn();
        vtk_camera->SetParallelScale(2.0 * camera.distance *
                                     std::tan(camera.fov * 3.14159265358979323846 / 360.0));
    } else {
        vtk_camera->ParallelProjectionOff();
        vtk_camera->SetViewAngle(camera.fov);
    }
}

void Viewport3D::update_vtk_axes() {
    if (!vtk_renderer_) return;
    if (vtk_axes_actor_) vtk_renderer_->RemoveActor(vtk_axes_actor_);
    if (vtk_origin_actor_) vtk_renderer_->RemoveActor(vtk_origin_actor_);

    const float x = vtk_origin_.x();
    const float y = vtk_origin_.y();
    const float z = vtk_origin_.z();
    const float length = static_cast<float>(axis_length_);
    const std::vector<float> axes = {
        x, y, z, 0.95f, 0.25f, 0.25f, x + length, y, z, 0.95f, 0.25f, 0.25f,
        x, y, z, 0.30f, 0.90f, 0.45f, x, y + length, z, 0.30f, 0.90f, 0.45f,
        x, y, z, 0.30f, 0.65f, 1.0f, x, y, z + length, 0.30f, 0.65f, 1.0f};
    vtk_axes_actor_ = vtk_line_actor(axes, 2.0f);
    vtk_renderer_->AddActor(vtk_axes_actor_);

    const float marker = std::max(1.0f, length * 0.08f);
    const std::vector<float> origin = {
        x - marker, y, z, 1.0f, 0.75f, 0.15f, x + marker, y, z, 1.0f, 0.75f, 0.15f,
        x, y - marker, z, 1.0f, 0.75f, 0.15f, x, y + marker, z, 1.0f, 0.75f, 0.15f,
        x, y, z - marker, 1.0f, 0.75f, 0.15f, x, y, z + marker, 1.0f, 0.75f, 0.15f};
    vtk_origin_actor_ = vtk_line_actor(origin, 2.5f);
    vtk_renderer_->AddActor(vtk_origin_actor_);
}

#endif

unsigned int Viewport3D::compile_shaders() {
    const auto compile = [this](unsigned int type, const char* source, const char* label) {
        const unsigned int shader = gl_->glCreateShader(type);
        gl_->glShaderSource(shader, 1, &source, nullptr);
        gl_->glCompileShader(shader);
        int ok = 0;
        gl_->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            gl_->glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string(label) + " shader error:\n" + log);
        }
        return shader;
    };
    const unsigned int vs = compile(GL_VERTEX_SHADER, kVertexShader, "Vertex");
    const unsigned int fs = compile(GL_FRAGMENT_SHADER, kFragmentShader, "Fragment");
    const unsigned int program = gl_->glCreateProgram();
    gl_->glAttachShader(program, vs);
    gl_->glAttachShader(program, fs);
    gl_->glLinkProgram(program);
    int linked = 0;
    gl_->glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[2048] = {};
        gl_->glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Shader link error:\n") + log);
    }
    gl_->glDeleteShader(vs);
    gl_->glDeleteShader(fs);
    return program;
}

Viewport3D::LineBuffer Viewport3D::create_vao(const std::vector<float>& vertices) {
    LineBuffer result;
    gl_->glGenVertexArrays(1, &result.vao);
    gl_->glGenBuffers(1, &result.vbo);
    gl_->glBindVertexArray(result.vao);
    upload_buffer(result.vbo, vertices, kStaticDraw);
    gl_->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    gl_->glEnableVertexAttribArray(0);
    gl_->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                reinterpret_cast<void*>(3 * sizeof(float)));
    gl_->glEnableVertexAttribArray(1);
    gl_->glBindVertexArray(0);
    result.count = static_cast<int>(vertices.size() / 6);
    return result;
}

void Viewport3D::upload_buffer(unsigned int vbo, const std::vector<float>& vertices, unsigned int usage) {
    gl_->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl_->glBufferData(GL_ARRAY_BUFFER, static_cast<ptrdiff_t>(vertices.size() * sizeof(float)),
                      vertices.empty() ? nullptr : vertices.data(), usage);
}

void Viewport3D::set_dynamic_lines(const std::vector<float>& vertices, LineBuffer& buffer) {
    if (!buffer.vao) buffer = create_vao(vertices);
    else upload_buffer(buffer.vbo, vertices, kDynamicDraw);
    buffer.count = static_cast<int>(vertices.size() / 6);
    gl_->glBindBuffer(GL_ARRAY_BUFFER, 0);
}

}  // namespace cnc::vp
