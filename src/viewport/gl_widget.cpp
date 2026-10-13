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

}  // namespace

Viewport3D::Viewport3D(QWidget* parent) : QOpenGLWidget(parent) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    setMinimumSize(400, 300);
}

void Viewport3D::initializeGL() {
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
}

void Viewport3D::resizeGL(int width, int height) {
    gl_->glViewport(0, 0, width, height);
    camera.aspect = static_cast<double>(width) / std::max(height, 1);
    camera.viewport_height = std::max(this->height(), 1);
}

void Viewport3D::paintGL() {
    gl_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl_->glUseProgram(shader_program_);
    const QMatrix4x4 mvp = camera.mvp();
    gl_->glUniformMatrix4fv(mvp_location_, 1, GL_FALSE, mvp.constData());
    gl_->glUniform1f(alpha_location_, 1.0f);

    if (simulation_mesh_indices_) {
        gl_->glUniform1f(alpha_location_, 0.36f);
        gl_->glDepthMask(GL_FALSE);
        gl_->glBindVertexArray(simulation_mesh_vao_);
        gl_->glDrawElements(GL_TRIANGLES, simulation_mesh_indices_, GL_UNSIGNED_INT, nullptr);
        gl_->glDepthMask(GL_TRUE);
        gl_->glUniform1f(alpha_location_, 1.0f);
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

int Viewport3D::add_entity_lines(const std::vector<float>& vertices, float) {
    makeCurrent();
    entity_buffers_.push_back(create_vao(vertices));
    doneCurrent();
    update();
    return static_cast<int>(entity_buffers_.size()) - 1;
}

void Viewport3D::clear_entities() {
    makeCurrent();
    for (const LineBuffer& buffer : entity_buffers_) {
        gl_->glDeleteVertexArrays(1, &buffer.vao);
        gl_->glDeleteBuffers(1, &buffer.vbo);
    }
    entity_buffers_.clear();
    doneCurrent();
    update();
}

void Viewport3D::set_origin(double x, double y, double z) {
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
}

void Viewport3D::set_simulation_path(const std::vector<float>& vertices) {
    makeCurrent();
    set_dynamic_lines(vertices, simulation_path_);
    doneCurrent();
    update();
}

void Viewport3D::set_simulation_tool(std::optional<QVector3D> position, double diameter) {
    std::vector<float> vertices;
    if (position && diameter > 0.0) {
        const double x = position->x(), y = position->y(), z = position->z();
        const double radius = diameter / 2.0;
        const double flute_height = std::max(diameter * 1.5, 2.5);
        const double stem_height = std::max(diameter * 3.0, 15.0);
        constexpr float flute_color[] = {1.0f, 0.48f, 0.12f};
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
}

void Viewport3D::clear_simulation() {
    simulation_mesh_indices_ = 0;
    simulation_path_.count = 0;
    simulation_tool_.count = 0;
    update();
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
