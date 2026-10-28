import QtQuick

// Material-style slider.
Item {
    id: root
    property real from: 0
    property real to: 100
    property real value: 0
    property real step: 1
    property bool rainbow: false
    signal moved(real value)

    implicitHeight: 32
    implicitWidth: 220
    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Keys.onLeftPressed: root.moved(Math.max(from, value - step))
    Keys.onRightPressed: root.moved(Math.min(to, value + step))

    readonly property real frac: (value - from) / Math.max(1e-6, to - from)

    Rectangle {
        id: track
        x: 8; width: parent.width - 16; height: 6; radius: 3
        anchors.verticalCenter: parent.verticalCenter
        color: Pal.outlineVariant
        gradient: root.rainbow ? hueGradient : null
        Rectangle { visible: !root.rainbow; width: parent.width * root.frac; height: parent.height; radius: 3; color: Pal.primary }
    }
    Gradient {
        id: hueGradient
        orientation: Gradient.Horizontal
        GradientStop { position: 0.00; color: "#e0524f" }
        GradientStop { position: 0.17; color: "#e0b84f" }
        GradientStop { position: 0.33; color: "#6fe04f" }
        GradientStop { position: 0.50; color: "#4fe0d2" }
        GradientStop { position: 0.67; color: "#4f6fe0" }
        GradientStop { position: 0.83; color: "#c24fe0" }
        GradientStop { position: 1.00; color: "#e0524f" }
    }
    Rectangle {
        width: 6; height: 22; radius: 3
        anchors.verticalCenter: parent.verticalCenter
        x: 8 + track.width * root.frac - 3
        color: Pal.primary
        border.width: root.activeFocus ? 2 : 0
        border.color: Pal.onSurface
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        function setv(x) {
            var f = Math.max(0, Math.min(1, (x - 8) / track.width))
            var v = root.from + f * (root.to - root.from)
            root.moved(Math.round(v / root.step) * root.step)
        }
        onPressed: (m) => { root.forceActiveFocus(); setv(m.x) }
        onPositionChanged: (m) => { if (pressed) setv(m.x) }
    }
}
