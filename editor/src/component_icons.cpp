#include "component_icons.hpp"

#include "polyizon/scene/components.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <cmath>

namespace {

QColor IconColor(ComponentIconKind kind) {
    switch (kind) {
    case ComponentIconKind::Transform: return QColor("#61a9ff");
    case ComponentIconKind::Mesh: return QColor("#a8b2c2");
    case ComponentIconKind::Material: return QColor("#b98cff");
    case ComponentIconKind::Script: return QColor("#58d6c7");
    case ComponentIconKind::DirectionalLight: return QColor("#ffd45c");
    case ComponentIconKind::PointLight: return QColor("#ffcc4d");
    case ComponentIconKind::SpotLight: return QColor("#ff9c52");
    case ComponentIconKind::Environment: return QColor("#65b7ff");
    case ComponentIconKind::LightBake: return QColor("#ffb568");
    case ComponentIconKind::Entity: return QColor("#d3dae5");
    }
    return QColor("#d3dae5");
}

} // namespace

QIcon GetComponentIcon(ComponentIconKind kind, int logicalSize) {
    const int pixelSize = logicalSize * 2;
    QPixmap pixmap(pixelSize, pixelSize);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2.0);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor color = IconColor(kind);
    QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const QRectF r(2.0, 2.0, logicalSize - 4.0, logicalSize - 4.0);
    const QPointF center = r.center();

    switch (kind) {
    case ComponentIconKind::Entity:
    case ComponentIconKind::Mesh: {
        QPainterPath cube;
        cube.moveTo(center.x(), r.top());
        cube.lineTo(r.right(), r.top() + r.height() * 0.28);
        cube.lineTo(r.right(), r.bottom() - r.height() * 0.18);
        cube.lineTo(center.x(), r.bottom());
        cube.lineTo(r.left(), r.bottom() - r.height() * 0.18);
        cube.lineTo(r.left(), r.top() + r.height() * 0.28);
        cube.closeSubpath();
        cube.moveTo(r.left(), r.top() + r.height() * 0.28);
        cube.lineTo(center.x(), r.top() + r.height() * 0.52);
        cube.lineTo(r.right(), r.top() + r.height() * 0.28);
        cube.moveTo(center.x(), r.top() + r.height() * 0.52);
        cube.lineTo(center.x(), r.bottom());
        painter.drawPath(cube);
        break;
    }
    case ComponentIconKind::Transform:
        painter.drawLine(center, QPointF(center.x(), r.top()));
        painter.drawLine(center, QPointF(r.right(), center.y()));
        painter.drawLine(center, QPointF(r.left() + 1.0, r.bottom() - 1.0));
        painter.setBrush(color);
        painter.drawEllipse(QPointF(center.x(), r.top()), 1.7, 1.7);
        painter.drawEllipse(QPointF(r.right(), center.y()), 1.7, 1.7);
        painter.drawEllipse(QPointF(r.left() + 1.0, r.bottom() - 1.0), 1.7, 1.7);
        break;
    case ComponentIconKind::Material:
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 55));
        painter.drawEllipse(r.adjusted(1.0, 1.0, -1.0, -1.0));
        painter.setPen(QPen(QColor("#eadfff"), 1.2));
        painter.drawArc(r.adjusted(4.0, 3.0, -3.0, -5.0), 55 * 16, 80 * 16);
        break;
    case ComponentIconKind::Script:
        painter.drawText(r, Qt::AlignCenter, "{ }");
        break;
    case ComponentIconKind::DirectionalLight:
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 70));
        painter.drawEllipse(QPointF(r.left() + 4.0, center.y()), 2.6, 2.6);
        painter.setBrush(Qt::NoBrush);
        for (int i = 0; i < 3; ++i) {
            const qreal y = r.top() + 3.0 + i * 4.0;
            painter.drawLine(QPointF(center.x(), y), QPointF(r.right(), y));
            painter.drawLine(QPointF(r.right() - 2.0, y - 1.5), QPointF(r.right(), y));
            painter.drawLine(QPointF(r.right() - 2.0, y + 1.5), QPointF(r.right(), y));
        }
        break;
    case ComponentIconKind::PointLight:
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 75));
        painter.drawEllipse(center, 4.0, 4.0);
        painter.setBrush(Qt::NoBrush);
        for (int i = 0; i < 8; ++i) {
            const qreal angle = i * 3.14159265359 / 4.0;
            painter.drawLine(center + QPointF(std::cos(angle) * 5.5, std::sin(angle) * 5.5),
                             center + QPointF(std::cos(angle) * 7.0, std::sin(angle) * 7.0));
        }
        break;
    case ComponentIconKind::SpotLight: {
        QPainterPath cone;
        cone.moveTo(r.left() + 2.0, center.y());
        cone.lineTo(r.right(), r.top() + 2.0);
        cone.lineTo(r.right(), r.bottom() - 2.0);
        cone.closeSubpath();
        painter.setBrush(QColor(color.red(), color.green(), color.blue(), 40));
        painter.drawPath(cone);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(r.left() + 2.0, center.y()), 2.0, 2.0);
        break;
    }
    case ComponentIconKind::Environment:
        painter.drawArc(r.adjusted(1.0, 1.0, -1.0, -1.0), 10 * 16, 160 * 16);
        painter.drawLine(QPointF(r.left() + 1.0, center.y() + 3.0), QPointF(r.right() - 1.0, center.y() + 3.0));
        painter.drawEllipse(QPointF(r.right() - 3.5, r.top() + 3.5), 2.0, 2.0);
        break;
    case ComponentIconKind::LightBake:
        painter.drawRoundedRect(r.adjusted(1.0, 3.0, -1.0, -2.0), 2.0, 2.0);
        painter.drawLine(QPointF(r.left() + 4.0, r.top() + 3.0), QPointF(r.left() + 4.0, r.top()));
        painter.drawLine(QPointF(r.right() - 4.0, r.top() + 3.0), QPointF(r.right() - 4.0, r.top()));
        painter.drawLine(QPointF(center.x(), r.top() + 3.0), QPointF(center.x(), r.top() - 1.0));
        break;
    }

    painter.end();
    return QIcon(pixmap);
}

QIcon GetEntityIcon(const entt::registry& registry, entt::entity entity, int logicalSize) {
    if (registry.all_of<polyizon::DirectionalLightComponent>(entity)) {
        return GetComponentIcon(ComponentIconKind::DirectionalLight, logicalSize);
    }
    if (registry.all_of<polyizon::PointLightComponent>(entity)) {
        return GetComponentIcon(ComponentIconKind::PointLight, logicalSize);
    }
    if (registry.all_of<polyizon::SpotLightComponent>(entity)) {
        return GetComponentIcon(ComponentIconKind::SpotLight, logicalSize);
    }
    if (registry.all_of<polyizon::ScriptComponent>(entity)) {
        return GetComponentIcon(ComponentIconKind::Script, logicalSize);
    }
    if (registry.all_of<polyizon::MeshComponent>(entity)) {
        return GetComponentIcon(ComponentIconKind::Mesh, logicalSize);
    }
    return GetComponentIcon(ComponentIconKind::Entity, logicalSize);
}
