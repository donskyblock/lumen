import QtQuick
import Lumen 1.0

// Seek bar: a waving "played" line while audio plays, flat when paused.
Item {
    id: root
    property real value: 0              // 0..1
    property bool waving: false
    signal seekRequested(real fraction)

    property bool dragging: false
    property real dragValue: 0
    implicitHeight: 28
    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Accessible.name: "Seek"
    Keys.onLeftPressed: root.seekRequested(Math.max(0, root.value - 0.02))
    Keys.onRightPressed: root.seekRequested(Math.min(1, root.value + 0.02))

    WavyProgress {
        id: bar
        anchors.fill: parent
        value: root.dragging ? root.dragValue : root.value
        amplitude: root.waving && Pal.wavy ? 3 : 0
        activeColor: Pal.primary
        trackColor: Pal.outlineVariant
        Behavior on amplitude { NumberAnimation { duration: Pal.motion ? 350 : 0; easing.type: Easing.OutCubic } }
    }
    // ~30 fps, and only while there is actually a wave to animate.
    Timer {
        interval: 33
        repeat: true
        running: root.visible && bar.amplitude > 0.05
        onTriggered: bar.phase += 0.3
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        function frac(x) { return Math.max(0, Math.min(1, (x - 6) / Math.max(1, width - 12))) }
        onPressed: (m) => { root.forceActiveFocus(); root.dragging = true; root.dragValue = frac(m.x) }
        onPositionChanged: (m) => { if (pressed) root.dragValue = frac(m.x) }
        onReleased: { root.dragging = false; root.seekRequested(root.dragValue) }
    }
}
