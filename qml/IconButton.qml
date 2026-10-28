import QtQuick

// Round icon button with a state layer and a visible keyboard focus ring.
Item {
    id: root
    property string icon: ""
    property real iconSize: 24
    property color tint: Pal.onSurfaceVariant
    property bool active: false          // e.g. shuffle on
    property color activeTint: Pal.primary
    signal clicked()

    implicitWidth: 44
    implicitHeight: 44
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: icon
    Keys.onSpacePressed: clicked()
    Keys.onReturnPressed: clicked()

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Pal.onSurface
        opacity: mouse.pressed ? 0.14 : (mouse.containsMouse ? 0.08 : 0)
        Behavior on opacity { NumberAnimation { duration: Pal.motion ? 100 : 0 } }
    }
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: Pal.primary
        visible: root.activeFocus
    }
    Icon {
        anchors.centerIn: parent
        name: root.icon
        size: root.iconSize
        color: root.active ? root.activeTint : root.tint
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { root.forceActiveFocus(); root.clicked() }
    }
}
