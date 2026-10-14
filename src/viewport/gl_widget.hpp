// Porte de src/viewport/gl_widget.py: viewport OpenGL 3D.
#pragma once

#include <optional>
#include <vector>

#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QPoint>

#include "viewport/camera.hpp"

#if defined(CNC_HAVE_VTK)
#include <vtkSmartPointer.h>
#endif

#if defined(CNC_HAVE_VTK)
class vtkActor;
class vtkCallbackCommand;
class vtkGenericOpenGLRenderWindow;
class vtkPolyData;
class vtkPolyDataNormals;
class vtkRenderer;
#endif

namespace cnc::vp {

class Viewport3D : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit Viewport3D(QWidget* parent = nullptr);

    int add_entity_lines(const std::vector<float>& vertices, float line_width = 1.5f);
    void clear_entities();
    void set_origin(double x, double y, double z);
    void set_workpiece(double width, double height, double z_bottom, double z_top);
    void set_simulation_mesh(const std::vector<float>& vertices,
                             const std::vector<unsigned int>& indices,
                             std::optional<std::pair<int, int>> mesh_shape = std::nullopt);
    void set_simulation_path(const std::vector<float>& vertices);
    void set_simulation_tool(std::optional<QVector3D> position, double diameter = 0.0);
    void clear_simulation();
    void fit_scene();
    void set_standard_view(const QString& name);
    bool toggle_projection();

    Camera camera;

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct LineBuffer {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int count = 0;
    };

    unsigned int compile_shaders();
    LineBuffer create_vao(const std::vector<float>& data);
    void upload_buffer(unsigned int vbo, const std::vector<float>& data, unsigned int usage);
    void set_dynamic_lines(const std::vector<float>& vertices, LineBuffer& buffer);

#if defined(CNC_HAVE_VTK)
    void sync_vtk_camera();
    void update_vtk_axes();
#endif

    unsigned int shader_program_ = 0;
    int mvp_location_ = -1;
    int alpha_location_ = -1;
    LineBuffer axes_;
    LineBuffer origin_;
    std::vector<LineBuffer> entity_buffers_;
    LineBuffer simulation_path_;
    LineBuffer simulation_tool_;
    unsigned int simulation_mesh_vao_ = 0;
    unsigned int simulation_mesh_vbo_ = 0;
    unsigned int simulation_mesh_ebo_ = 0;
    int simulation_mesh_indices_ = 0;
    std::optional<std::pair<int, int>> simulation_mesh_shape_;

    std::optional<std::pair<QVector3D, QVector3D>> scene_bounds_;
    double axis_length_ = 50.0;
    QPoint last_mouse_pos_;
    Qt::MouseButton mouse_button_ = Qt::NoButton;
    QOpenGLExtraFunctions* gl_ = nullptr;

#if defined(CNC_HAVE_VTK)
    vtkSmartPointer<vtkCallbackCommand> vtk_render_callback_;
    vtkSmartPointer<vtkGenericOpenGLRenderWindow> vtk_render_window_;
    vtkSmartPointer<vtkRenderer> vtk_renderer_;
    vtkSmartPointer<vtkActor> vtk_axes_actor_;
    vtkSmartPointer<vtkActor> vtk_origin_actor_;
    vtkSmartPointer<vtkActor> vtk_simulation_mesh_actor_;
    vtkSmartPointer<vtkPolyData> vtk_simulation_mesh_data_;
    vtkSmartPointer<vtkPolyDataNormals> vtk_simulation_normals_;
    vtkSmartPointer<vtkActor> vtk_simulation_path_actor_;
    std::vector<vtkSmartPointer<vtkActor>> vtk_simulation_tool_actors_;
    double vtk_simulation_tool_diameter_ = 0.0;
    std::vector<vtkSmartPointer<vtkActor>> vtk_entity_actors_;
    QVector3D vtk_origin_;
#endif
};

}  // namespace cnc::vp
