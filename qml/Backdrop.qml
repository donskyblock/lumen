import QtQuick

// Soft colour wash taken from the cover art: a 48 px copy of the cover,
// stretched with smoothing, fading into the panel colour. No shaders or
// offscreen layers, so it costs a few KB.
Item {
    id: root
    property string key: ""
    property color base: Pal.surfaceLow
    property real strength: 0.5
    property real cornerRadius: 0
    property bool fadeTop: false      // also dissolve into the panel colour at the top
    clip: true

    Image {
        anchors.fill: parent
        asynchronous: true
        smooth: true
        fillMode: Image.Stretch
        visible: status === Image.Ready
        opacity: root.strength
        source: root.key ? "image://cover/" + root.key + "@48@" + Math.round(root.cornerRadius / Math.max(1, root.width) * 100) : ""
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(root.base.r, root.base.g, root.base.b, root.fadeTop ? 0.92 : 0.0) }
            GradientStop { position: root.fadeTop ? 0.4 : 0.5; color: Qt.rgba(root.base.r, root.base.g, root.base.b, root.fadeTop ? 0.15 : 0.1) }
            GradientStop { position: 1.0; color: root.base }
        }
    }
}
