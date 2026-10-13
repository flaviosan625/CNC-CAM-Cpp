// Porte de src/viewport/camera.py: camera orbital 3D.
#pragma once

#include <QMatrix4x4>
#include <QVector3D>

namespace cnc::vp {

class Camera {
public:
    Camera();

    QVector3D position() const;
    QMatrix4x4 view_matrix() const;
    QMatrix4x4 projection_matrix() const;
    QMatrix4x4 mvp() const;

    void orbit(double dx, double dy);
    void pan(double dx, double dy);
    void zoom(double delta);
    void reset();
    void isometric_view();
    void top_view();
    void front_view();
    void back_view();
    void right_view();
    void left_view();
    void bottom_view();
    void fit_bounds(const QVector3D& minimum, const QVector3D& maximum, double margin = 1.18);

    QVector3D target;
    double distance = 200.0;
    double azimuth = -60.0;
    double elevation = 35.0;
    double fov = 45.0;
    double near_plane = 0.1;
    double far_plane = 50000.0;
    double aspect = 1.0;
    int viewport_height = 600;
    bool use_ortho = false;
};

}  // namespace cnc::vp
