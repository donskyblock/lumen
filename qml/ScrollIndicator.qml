import QtQuick

// Slim scroll position indicator (no QtQuick.Controls dependency).
Rectangle {
    id: root
    property Flickable view
    width: 4
    radius: 2
    color: Pal.outline
    anchors.right: parent ? parent.right : undefined
    anchors.rightMargin: 4
    visible: view && view.visibleArea.heightRatio < 0.995
    opacity: view && (view.moving || view.flicking) ? 0.7 : 0.25
    Behavior on opacity { NumberAnimation { duration: Pal.motion ? 250 : 0 } }
    y: view && parent ? 6 + view.visibleArea.yPosition * (parent.height - 12) : 0
    height: view && parent ? Math.max(28, view.visibleArea.heightRatio * (parent.height - 12)) : 0
}
