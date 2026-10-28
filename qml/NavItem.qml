import QtQuick

Item {
    id: root
    property string icon: ""
    property string text: ""
    property bool selected: false
    signal clicked()

    implicitHeight: 48
    activeFocusOnTab: true
    Keys.onSpacePressed: clicked()
    Keys.onReturnPressed: clicked()

    Rectangle {
        anchors.fill: parent
        radius: height * Pal.pf
        color: root.selected ? Pal.secondaryContainer : Pal.onSurface
        opacity: root.selected ? 1 : (mouse.containsMouse ? 0.08 : 0)
        Behavior on opacity { NumberAnimation { duration: Pal.motion ? 100 : 0 } }
    }
    Rectangle {
        anchors.fill: parent
        radius: height * Pal.pf
        color: "transparent"
        border.width: 2
        border.color: Pal.primary
        visible: root.activeFocus
    }
    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 18
        spacing: 14
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.icon
            color: root.selected ? Pal.onSecondaryContainer : Pal.onSurfaceVariant
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font.pixelSize: Math.round(15 * Pal.fs)
            font.weight: root.selected ? Font.DemiBold : Font.Medium
            color: root.selected ? Pal.onSecondaryContainer : Pal.onSurface
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { root.forceActiveFocus(); root.clicked() }
    }
}
