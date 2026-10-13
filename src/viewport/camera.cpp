// Porte de src/viewport/camera.py: camera orbital 3D.
#include "viewport/camera.hpp"

#include <algorithm>
#include <cmath>

#include <QVector4D>

namespace cnc::vp {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

double radians(double degrees) { return degrees * kPi / 180.0; }

QMatrix4x4 perspective(double fov, double aspect, double near_plane, double far_plane) {
    const float f = static_cast<float>(1.0 / std::tan(radians(fov) / 2.0));
    const float a = static_cast<float>((far_plane + near_plane) / (near_plane - far_plane));
    const float b = static_cast<float>((2.0 * far_plane * near_plane) / (near_plane - far_plane));

    QMatrix4x4 matrix;
    matrix.fill(0.0f);
    matrix.setRow(0, QVector4D(f / static_cast<float>(aspect), 0, 0, 0));
    matrix.setRow(1, QVector4D(0, f, 0, 0));
    matrix.setRow(2, QVector4D(0, 0, a, b));
    matrix.setRow(3, QVector4D(0, 0, -1, 0));
    return matrix;
}

QMatrix4x4 ortho(double left, double right, double bottom, double top,
                 double near_plane, double far_plane) {
    QMatrix4x4 matrix;
    matrix.fill(0.0f);
    matrix.setRow(0, QVector4D(static_cast<float>(2.0 / (right - left)), 0, 0,
                               static_cast<float>(-(right + left) / (right - left))));
    matrix.setRow(1, QVector4D(0, static_cast<float>(2.0 / (top - bottom)), 0,
                               static_cast<float>(-(top + bottom) / (top - bottom))));
    matrix.setRow(2, QVector4D(0, 0, static_cast<float>(-2.0 / (far_plane - near_plane)),
                               static_cast<float>(-(far_plane + near_plane) / (far_plane - near_plane))));
    matrix.setRow(3, QVector4D(0, 0, 0, 1));
    return matrix;
}

}  // namespace

Camera::Camera() : target(0.0f, 0.0f, 0.0f) {}

QVector3D Camera::position() const {
    const double az = radians(azimuth);
    const double el = radians(elevation);
    const double cos_el = std::cos(el);
    return {static_cast<float>(target.x() + distance * cos_el * std::cos(az)),
            static_cast<float>(target.y() + distance * cos_el * std::sin(az)),
            static_cast<float>(target.z() + distance * std::sin(el))};
}

QMatrix4x4 Camera::view_matrix() const {
    const QVector3D pos = position();
    QVector3D forward = target - pos;
    const float length = forward.length();
    if (length < 1e-8f) {
        QMatrix4x4 identity;
        identity.setToIdentity();
        return identity;
    }
    forward /= length;

    QVector3D world_up(0.0f, 0.0f, 1.0f);
    QVector3D right = QVector3D::crossProduct(forward, world_up);
    float right_length = right.length();
    if (right_length < 1e-6f) {
        world_up = QVector3D(0.0f, 1.0f, 0.0f);
        right = QVector3D::crossProduct(forward, world_up);
        right_length = right.length();
    }
    right /= right_length;
    const QVector3D up = QVector3D::crossProduct(right, forward);

    QMatrix4x4 matrix;
    matrix.setRow(0, QVector4D(right, -QVector3D::dotProduct(right, pos)));
    matrix.setRow(1, QVector4D(up, -QVector3D::dotProduct(up, pos)));
    matrix.setRow(2, QVector4D(-forward, QVector3D::dotProduct(forward, pos)));
    matrix.setRow(3, QVector4D(0, 0, 0, 1));
    return matrix;
}

QMatrix4x4 Camera::projection_matrix() const {
    if (use_ortho) {
        const double half_h = distance * std::tan(radians(fov / 2.0));
        const double half_w = half_h * aspect;
        return ortho(-half_w, half_w, -half_h, half_h, near_plane, far_plane);
    }
    return perspective(fov, aspect, near_plane, far_plane);
}

QMatrix4x4 Camera::mvp() const { return projection_matrix() * view_matrix(); }

void Camera::orbit(double dx, double dy) {
    azimuth -= dx * 0.4;
    elevation = std::clamp(elevation + dy * 0.4, -89.0, 89.0);
}

void Camera::pan(double dx, double dy) {
    const double az = radians(azimuth);
    const double el = radians(elevation);
    const QVector3D right(static_cast<float>(-std::sin(az)), static_cast<float>(std::cos(az)), 0.0f);
    const QVector3D up(static_cast<float>(-std::sin(el) * std::cos(az)),
                       static_cast<float>(-std::sin(el) * std::sin(az)),
                       static_cast<float>(std::cos(el)));
    const double scale = 2.0 * distance * std::tan(radians(fov / 2.0)) /
                         static_cast<double>(std::max(viewport_height, 1));
    target += (-static_cast<float>(dx) * right + static_cast<float>(dy) * up) * static_cast<float>(scale);
}

void Camera::zoom(double delta) {
    distance *= delta > 0 ? 0.9 : 1.1;
    distance = std::clamp(distance, 1.0, 50000.0);
}

void Camera::reset() {
    target = QVector3D(0.0f, 0.0f, 0.0f);
    distance = 200.0;
    azimuth = -60.0;
    elevation = 35.0;
}

void Camera::isometric_view() {
    azimuth = -45.0;
    elevation = 35.264;
    use_ortho = false;
}

void Camera::top_view() {
    azimuth = -90.0;
    elevation = 90.0;
    use_ortho = true;
}

void Camera::front_view() {
    azimuth = -90.0;
    elevation = 0.0;
    use_ortho = true;
}

void Camera::back_view() {
    azimuth = 90.0;
    elevation = 0.0;
    use_ortho = true;
}

void Camera::right_view() {
    azimuth = 0.0;
    elevation = 0.0;
    use_ortho = true;
}

void Camera::left_view() {
    azimuth = 180.0;
    elevation = 0.0;
    use_ortho = true;
}

void Camera::bottom_view() {
    azimuth = 90.0;
    elevation = -90.0;
    use_ortho = true;
}

void Camera::fit_bounds(const QVector3D& minimum, const QVector3D& maximum, double margin) {
    target = (minimum + maximum) * 0.5f;
    const QVector3D corners[] = {
        {minimum.x(), minimum.y(), minimum.z()}, {minimum.x(), minimum.y(), maximum.z()},
        {minimum.x(), maximum.y(), minimum.z()}, {minimum.x(), maximum.y(), maximum.z()},
        {maximum.x(), minimum.y(), minimum.z()}, {maximum.x(), minimum.y(), maximum.z()},
        {maximum.x(), maximum.y(), minimum.z()}, {maximum.x(), maximum.y(), maximum.z()},
    };
    const QMatrix4x4 view = view_matrix();
    double half_w = 0.5, half_h = 0.5, half_d = 0.5;
    for (const QVector3D& corner : corners) {
        const QVector3D relative = corner - target;
        const auto dot_row = [&view, &relative](int row) {
            const QVector4D r = view.row(row);
            return static_cast<double>(r.x() * relative.x() + r.y() * relative.y() + r.z() * relative.z());
        };
        half_w = std::max(half_w, std::abs(dot_row(0)));
        half_h = std::max(half_h, std::abs(dot_row(1)));
        half_d = std::max(half_d, std::abs(dot_row(2)));
    }
    const double tangent = std::tan(radians(fov / 2.0));
    const double by_width = half_w / std::max(tangent * aspect, 1e-6);
    const double by_height = half_h / std::max(tangent, 1e-6);
    distance = std::max(1.0, (std::max(by_width, by_height) + half_d) * margin);
}

}  // namespace cnc::vp
